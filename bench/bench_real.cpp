/* Real-data anchor: 1000 Genomes phased haplotypes vs the synthetic generators.
 *
 * RESEARCH_PLAN.md 7.2 requires "at minimum one REAL dataset dumped to raw
 * bitmaps as a sanity anchor" and it has been outstanding since the plan was
 * written. Everything measured until now is synthetic, and a generator can
 * accidentally encode the very structure the kernels exploit -- which is the
 * exact failure mode 7.2 warns about.
 *
 * This answers two questions the synthetic corpora cannot:
 *   1. Is the real allele-frequency spectrum actually 1/i-shaped, i.e. is
 *      PROBLEM_STATEMENT.md 2.2's premise true of real data?
 *   2. Do the kernels rank the same way on it as on the generator?
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_allpairs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <time.h>
using namespace storm;

static inline uint64_t ns_now(){
#if defined(__APPLE__)
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
#endif
}
template<class L> static auto pick(L l,const char*nm)->decltype(l.v[0].fn){
  for(size_t i=0;i<l.n;++i) if(std::string(l.v[i].name)==nm) return l.v[i].fn; return l.v[0].fn; }

int main(int argc,char**argv){
  const char* path = "data/chr20.bin";
  uint32_t want_rows = 4096, stride = 1;
  std::string tag="host";
  for(int i=1;i<argc;++i){std::string a=argv[i];auto nx=[&]()->const char*{return i+1<argc?argv[++i]:"";};
    if(a=="--file")path=nx(); else if(a=="--rows")want_rows=atoi(nx());
    else if(a=="--stride")stride=atoi(nx()); else if(a=="--tag")tag=nx();}

  FILE* f=fopen(path,"rb");
  if(!f){std::printf("cannot open %s\n",path);return 1;}
  char magic[9]={0}; uint32_t ver,nr,nb,pad;
  if(fread(magic,1,8,f)!=8||memcmp(magic,"STORMBIN",8)||
     fread(&ver,4,1,f)!=1||fread(&nr,4,1,f)!=1||fread(&nb,4,1,f)!=1||fread(&pad,4,1,f)!=1){
    std::printf("bad header\n");return 1;}
  std::printf("# %s: %u variants x %u haplotypes\n",path,nr,nb);

  // Stride-sample so the subset spans the chromosome rather than one locus --
  // LD structure is local, so the first N variants would be atypically similar.
  std::vector<Row> rows; rows.reserve(want_rows);
  std::vector<uint32_t> pos;
  uint64_t total_set=0; uint32_t kept=0, seen=0;
  std::vector<uint64_t> spectrum(64,0);
  while(kept<want_rows){
    uint32_t c;
    if(fread(&c,4,1,f)!=1) break;
    pos.resize(c);
    if(c && fread(pos.data(),4,c,f)!=c) break;
    // Histogram every variant's allele count, not just the sampled ones.
    if(c) spectrum[std::min<size_t>(63,(size_t)(31-__builtin_clz(c)))]++;
    if(seen % stride == 0 && c > 0){
      rows.emplace_back();
      build_row(rows.back(),pos.data(),pos.size(),nb);
      total_set+=c; ++kept;
    }
    ++seen;
  }
  fclose(f);
  if(rows.size()<2){std::printf("too few rows\n");return 1;}

  std::printf("# sampled %zu variants (stride %u of %u scanned), universe %u bits\n",
              rows.size(),stride,seen,nb);
  std::printf("# mean allele count %.1f  => density %.6f\n",
              (double)total_set/rows.size(),(double)total_set/rows.size()/nb);

  std::printf("\nallele-frequency spectrum (all %u scanned variants, log2 bins)\n",seen);
  std::printf("  %-14s %10s %10s   1/i predicts\n","count","variants","%");
  uint64_t tot=0; for(uint64_t v:spectrum) tot+=v;
  for(size_t b=0;b<12 && b<spectrum.size();++b){
    if(!spectrum[b]) continue;
    std::printf("  %6u-%-7u %10llu %9.2f%%\n",1u<<b,(1u<<(b+1))-1,
                (unsigned long long)spectrum[b],100.0*spectrum[b]/tot);
  }

  // Cell ranking on real data.
  struct K{const char*n;double t;};
  std::vector<std::pair<uint32_t,uint32_t>> pr;
  for(size_t i=0;i<rows.size()&&pr.size()<20000;++i)
    for(size_t j=i+1;j<rows.size()&&pr.size()<20000;++j){
      const bool id=rows[i].meta.cardinality>=rows[j].meta.cardinality;
      pr.push_back({(uint32_t)(id?i:j),(uint32_t)(id?j:i)});}

  const auto f_dense=pick(cell_bb(),"dense");
  const auto f_occ  =pick(cell_bb(),"occ_sel");
  const auto f_bs   =pick(cell_bs(),"ilp8");
  const auto f_br   =pick(cell_br(),"hybrid4");
  const auto f_ss   =pick(cell_ss(),"adaptive2");
  const auto f_rr   =pick(cell_rr(),"adaptive2");

  uint64_t want=0; for(auto&p:pr) want+=f_dense(rows[p.first].B(),rows[p.second].B());
  auto chk=[&](const char*n,uint64_t g){ if(g!=want) std::printf("  !! %s disagrees: %llu vs %llu\n",n,
      (unsigned long long)g,(unsigned long long)want); };
  {uint64_t g=0;for(auto&p:pr)g+=f_occ(rows[p.first].B(),rows[p.second].B());chk("occ_sel",g);}
  {uint64_t g=0;for(auto&p:pr)g+=f_bs(rows[p.first].B(),rows[p.second].S());chk("B x S",g);}
  {uint64_t g=0;for(auto&p:pr)g+=f_br(rows[p.first].B(),rows[p.second].R());chk("B x R",g);}
  {uint64_t g=0;for(auto&p:pr)g+=f_ss(rows[p.first].S(),rows[p.second].S());chk("S x S",g);}
  {uint64_t g=0;for(auto&p:pr)g+=f_rr(rows[p.first].R(),rows[p.second].R());chk("R x R",g);}

  auto t=[&](auto fn){uint64_t b=UINT64_MAX;for(int r=0;r<5;++r){volatile uint64_t s=0;
    uint64_t t0=ns_now(); for(auto&p:pr) s+=fn(p); uint64_t d=ns_now()-t0;(void)s; if(d<b)b=d;}
    return (double)b/pr.size();};

  const double td=t([&](std::pair<uint32_t,uint32_t>&p){return f_dense(rows[p.first].B(),rows[p.second].B());});
  const double to=t([&](std::pair<uint32_t,uint32_t>&p){return f_occ  (rows[p.first].B(),rows[p.second].B());});
  const double tb=t([&](std::pair<uint32_t,uint32_t>&p){return f_bs   (rows[p.first].B(),rows[p.second].S());});
  const double tr=t([&](std::pair<uint32_t,uint32_t>&p){return f_br   (rows[p.first].B(),rows[p.second].R());});
  const double ts=t([&](std::pair<uint32_t,uint32_t>&p){return f_ss   (rows[p.first].S(),rows[p.second].S());});
  const double trr=t([&](std::pair<uint32_t,uint32_t>&p){return f_rr  (rows[p.first].R(),rows[p.second].R());});

  std::printf("\n%zu pairs, host=%s\n%-22s %10s %12s\n",pr.size(),tag.c_str(),"cell","ns/pair","vs all-bitmap");
  auto row=[&](const char*n,double v){std::printf("%-22s %10.2f %11.2fx\n",n,v,td/v);};
  row("B x B all-bitmap",td); row("B x B zone-mapped",to);
  row("B x S",tb); row("B x R",tr); row("S x S",ts); row("R x R",trr);
  double best=std::min({to,tb,tr,ts,trr});
  std::printf("\nBEST: %.2f ns/pair, %.1fx over all-bitmap  [claim P1 on REAL data]\n",best,td/best);
  return 0;
}
