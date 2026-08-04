/* Oracle regret for every selection policy, including the ones that pass Gate 1.
 *
 * RESEARCH_PLAN.md 5.1: "Report the model's regret -- how much slower than an
 * oracle that always picks correctly. Regret, not raw speed, is the metric that
 * makes this a contribution." It has only ever been measured for the per-pair
 * MODEL policy (94.1%), which FAILS Gate 1. The policies that pass -- tile
 * hoisting and probe-and-commit -- have no regret number at all.
 *
 * The oracle here times every cell on every pair and keeps the best, so it is
 * unattainable by construction: it pays no selection cost and cannot be
 * implemented. That is the point -- it is the denominator.
 */
#include "kernels/storm_allpairs.h"
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <time.h>
using namespace storm;
static inline uint64_t nsn(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
template<class L> static auto pick(L l,const char*n)->decltype(l.v[0].fn){
  for(size_t i=0;i<l.n;++i) if(std::string(l.v[i].name)==n) return l.v[i].fn; return l.v[0].fn; }

int main(int argc,char**argv){
  CorpusSpec sp; sp.n_rows=192; sp.universe=65536; sp.density=0.01;
  sp.structure=Structure::Clustered; sp.spectrum=Spectrum::Inverse;
  std::string tag="host";
  for(int i=1;i<argc;++i){std::string a=argv[i];auto nx=[&]()->const char*{return i+1<argc?argv[++i]:"";};
    if(a=="--rows")sp.n_rows=atoi(nx()); else if(a=="--density")sp.density=atof(nx());
    else if(a=="--tag")tag=nx();
    else if(a=="--structure"){std::string v=nx();sp.structure=v=="runs"?Structure::Runs:v=="uniform"?Structure::Uniform:Structure::Clustered;}
    else if(a=="--spectrum"){std::string v=nx();sp.spectrum=v=="uniform"?Spectrum::Uniform:Spectrum::Inverse;}}
  Corpus c; generate(c,sp);
  CostModel m; calibrate(m);

  const auto f_bb=pick(cell_bb(),"occ_sel"); const auto f_bs=pick(cell_bs(),"ilp8");
  const auto f_br=pick(cell_br(),"hybrid4"); const auto f_bw=pick(cell_bw(),"skip");
  const auto f_ss=pick(cell_ss(),"adaptive2"); const auto f_sr=pick(cell_sr(),"adaptive2");
  const auto f_rr=pick(cell_rr(),"adaptive2"); const auto f_ww=pick(cell_ww(),"skip2");

  struct P{uint32_t d,s;}; std::vector<P> pr;
  for(size_t i=0;i<c.rows.size();++i)for(size_t j=i+1;j<c.rows.size();++j){
    bool id=c.rows[i].meta.cardinality>=c.rows[j].meta.cardinality;
    pr.push_back({(uint32_t)(id?i:j),(uint32_t)(id?j:i)});}

  /* BUCKET oracle, not a per-pair oracle.
   *
   * A per-pair oracle is not measurable here: a single pair costs a few ns
   * against a clock granularity of ~41 ns, so per-pair timings floor at zero and
   * the "oracle" collapses to noise. The first version of this program reported
   * 0.10 ns/pair -- faster than one L1 load -- and regrets in the tens of
   * thousands of percent, which is what that failure looks like.
   *
   * Instead: bucket pairs by shape, time each bucket under each cell IN BULK,
   * and take the per-bucket minimum. Buckets are large enough to time honestly.
   * This is a WEAKER oracle than per-pair -- it cannot exploit variation inside
   * a bucket -- so the regret it yields is a LOWER BOUND on the true regret, and
   * is reported as such.
   *
   * Bucketing is by (sparse cardinality decile, sparse run-count decile), which
   * is the shape the selection model itself keys on. */
  auto bucket_of=[&](const P& q){
    const uint32_t card=c.rows[q.s].meta.cardinality, runs=c.rows[q.s].meta.n_runs;
    auto lg=[](uint32_t x){ return x?(uint32_t)(31-__builtin_clz(x)):0u; };
    return std::min(lg(card),15u)*16u + std::min(lg(runs),15u); };
  std::vector<std::vector<P>> buckets(256);
  for(auto&q:pr) buckets[bucket_of(q)].push_back(q);
  size_t nonempty=0; for(auto&b:buckets) nonempty+=!b.empty();

  double oracle_total=0;
  for(auto&bk:buckets){
    if(bk.empty()) continue;
    auto T=[&](auto f){uint64_t b=~0ull;
      const int reps = bk.size()<200 ? 200 : (bk.size()<2000?20:5);
      for(int r=0;r<5;++r){volatile uint64_t v=0;uint64_t t0=nsn();
        for(int z=0;z<reps;++z) for(auto&q:bk) v+=f(q);
        uint64_t dd=nsn()-t0;(void)v;if(dd<b)b=dd;}
      return (double)b/(double)reps; };
    double best=1e300;
    best=std::min(best,T([&](const P&q){return f_bb(c.rows[q.d].B(),c.rows[q.s].B());}));
    best=std::min(best,T([&](const P&q){return f_bs(c.rows[q.d].B(),c.rows[q.s].S());}));
    best=std::min(best,T([&](const P&q){return f_br(c.rows[q.d].B(),c.rows[q.s].R());}));
    best=std::min(best,T([&](const P&q){return f_bw(c.rows[q.d].B(),c.rows[q.s].W());}));
    best=std::min(best,T([&](const P&q){return f_ss(c.rows[q.d].S(),c.rows[q.s].S());}));
    best=std::min(best,T([&](const P&q){return f_sr(c.rows[q.s].S(),c.rows[q.d].R());}));
    best=std::min(best,T([&](const P&q){return f_rr(c.rows[q.d].R(),c.rows[q.s].R());}));
    best=std::min(best,T([&](const P&q){return f_ww(c.rows[q.d].W(),c.rows[q.s].W());}));
    oracle_total+=best; }
  const double oracle=oracle_total/pr.size();

  printf("# host=%s %s/%s d=%g rows=%u  %zu pairs\n",tag.c_str(),
         name_of(sp.structure),name_of(sp.spectrum),sp.density,sp.n_rows,pr.size());
  printf("%-12s %11s %10s %9s\n","policy","ns/pair","regret","sel %");
  printf("# bucket oracle over %zu non-empty shape buckets (lower bound on regret)\n",nonempty);
  printf("%-12s %11.2f %9s %9s   (unattainable denominator)\n","oracle",oracle,"--","--");
  for(Policy p : {Policy::AllBitmap,Policy::PerPair,Policy::PerTile,Policy::Probe}){
    AllPairsStats st=allpairs_sum(c.rows,m,p,64);
    double nsp=st.ns_total/st.pairs;
    printf("%-12s %11.2f %8.1f%% %8.2f%%\n",name_of(p),nsp,
           100.0*(nsp/oracle-1.0),100.0*st.ns_selection/st.ns_total);
  }
  return 0;
}
