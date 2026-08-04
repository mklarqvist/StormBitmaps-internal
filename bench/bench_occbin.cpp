/* Zone-map bin-width sweep. The width was a hardcoded constexpr 8 (512 bits)
 * from the day the mechanism was invented, so every figure published for it --
 * "0.195% overhead", "up to 25x" -- described one unswept parameterization. */
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"
#include <cstdio>
#include <cstdlib>
#include <string>
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
int main(int argc,char**argv){
  CorpusSpec spec; spec.n_rows=112; spec.universe=262144; spec.density=0.01;
  spec.structure=Structure::Clustered; spec.spectrum=Spectrum::Inverse;
  std::string tag="host";
  for(int i=1;i<argc;++i){std::string a=argv[i];auto nx=[&]()->const char*{return i+1<argc?argv[++i]:"";};
    if(a=="--universe")spec.universe=atoi(nx()); else if(a=="--density")spec.density=atof(nx());
    else if(a=="--rows")spec.n_rows=atoi(nx()); else if(a=="--tag")tag=nx();
    else if(a=="--structure"){std::string v=nx();spec.structure=v=="runs"?Structure::Runs:v=="uniform"?Structure::Uniform:Structure::Clustered;}}
  auto f_occ=[&]{auto l=cell_bb();for(size_t i=0;i<l.n;++i)if(std::string(l.v[i].name)=="occ_sel")return l.v[i].fn;return l.v[0].fn;}();
  auto f_dense=[&]{auto l=cell_bb();for(size_t i=0;i<l.n;++i)if(std::string(l.v[i].name)=="dense")return l.v[i].fn;return l.v[0].fn;}();
  std::printf("# host=%s universe=%u rows=%u d=%g\n",tag.c_str(),spec.universe,spec.n_rows,spec.density);
  std::printf("%10s %12s %12s %10s %10s\n","bin(bits)","overhead%","ns/pair","vs dense","zone kB");
  double dense_ns=0;
  for(uint32_t bw : {1u,2u,4u,8u,16u,32u,64u,128u,512u}){
    spec.occ_bin=bw; Corpus c; generate(c,spec);
    std::vector<std::pair<uint32_t,uint32_t>> pr;
    for(size_t i=0;i<c.rows.size()&&pr.size()<3000;++i)for(size_t j=i+1;j<c.rows.size()&&pr.size()<3000;++j)pr.push_back({(uint32_t)i,(uint32_t)j});
    uint64_t want=0; for(auto&p:pr) want+=f_dense(c.rows[p.first].B(),c.rows[p.second].B());
    auto t=[&](auto f){uint64_t b=UINT64_MAX;for(int r=0;r<5;++r){volatile uint64_t s=0;uint64_t t0=ns_now();
      for(auto&p:pr)s+=f(c.rows[p.first].B(),c.rows[p.second].B());uint64_t d=ns_now()-t0;(void)s;if(d<b)b=d;}
      return (double)b/pr.size();};
    uint64_t got=0; for(auto&p:pr) got+=f_occ(c.rows[p.first].B(),c.rows[p.second].B());
    if(got!=want){std::printf("%10u  WRONG\n",bw*64);continue;}
    if(bw==8u) dense_ns=t(f_dense);
    const double occ_ns=t(f_occ);
    std::printf("%10u %11.4f%% %12.2f %9.2fx %10.1f\n",bw*64,100.0*c.bytes_occ/c.bytes_B,occ_ns,
                (dense_ns>0?dense_ns:t(f_dense))/occ_ns, c.bytes_occ/1024.0);
  }
  return 0;
}
