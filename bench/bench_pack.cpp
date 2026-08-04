/* Read a STORMPACK file and pair rows in whatever representation they were
 * stored as -- the query-time half of the storage/compute split. */
#include "kernels/storm_cells.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace storm;
enum Tag:uint8_t{T_BITMAP=0,T_ARRAY16=1,T_RLE16=2,T_FULL=3,T_EMPTY=4};
static inline uint64_t nsn(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}

int main(int argc,char**argv){
  const char* path=argc>1?argv[1]:"data/chr20.pack";
  uint32_t want=argc>2?(uint32_t)atoi(argv[2]):4000, stride=argc>3?(uint32_t)atoi(argv[3]):400;
  int fd=open(path,O_RDONLY); if(fd<0){printf("open fail\n");return 1;}
  struct stat st; fstat(fd,&st);
  const uint8_t* M=(const uint8_t*)mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  if(M==MAP_FAILED){printf("mmap fail\n");return 1;}
  if(memcmp(M,"STRMPACK",8)){printf("bad magic\n");return 1;}
  uint32_t ver,nr,nb,flags; memcpy(&ver,M+8,4);memcpy(&nr,M+12,4);memcpy(&nb,M+16,4);memcpy(&flags,M+20,4);
  const uint32_t* off =(const uint32_t*)(M+24);
  const uint32_t* rlen=(const uint32_t*)(M+24+4ull*nr);
  const uint8_t*  tg  =(const uint8_t*) (M+24+8ull*nr);
  size_t hdr=24+8ull*nr+nr; while(hdr&7)++hdr;
  const uint8_t* blob=M+hdr;
  const uint32_t nw=(nb+63)/64;

  // Materialize only the sampled rows; positions widen u16 -> u32 for the u32 kernels.
  struct PR{uint8_t t;const uint64_t*bm;const uint16_t*u16;uint32_t n16;
            std::vector<uint32_t> v,rs,re;uint32_t card;};
  std::vector<PR> rows; std::vector<uint64_t> bmstore;
  uint64_t bytes_used=0;
  for(uint32_t i=0;i<nr&&rows.size()<want;i+=stride){
    PR r; r.t=tg[i]; const uint8_t* p=blob+off[i]; const uint32_t len=rlen[i];
    bytes_used+=len; r.bm=nullptr; r.card=0;
    r.u16=nullptr; r.n16=0;
    if(r.t==T_BITMAP){ r.bm=(const uint64_t*)p; for(uint32_t k=0;k<nw;++k) r.card+=__builtin_popcountll(r.bm[k]); }
    else if(r.t==T_ARRAY16){ const uint16_t* a=(const uint16_t*)p; const uint32_t n=len/2;
      r.u16=a; r.n16=n; r.v.resize(n); for(uint32_t k=0;k<n;++k) r.v[k]=a[k]; r.card=n; }
    else if(r.t==T_RLE16){ const uint16_t* a=(const uint16_t*)p; const uint32_t n=len/4;
      for(uint32_t k=0;k<n;++k){uint32_t s=a[2*k],l=a[2*k+1];r.rs.push_back(s);r.re.push_back(s+l);r.card+=l;} }
    else continue;
    if(r.card==0) continue;
    rows.push_back(std::move(r));
  }
  // bitmaps for rows stored as arrays/runs, so B x S / B x R have a dense side
  bmstore.assign((size_t)rows.size()*nw,0);
  for(size_t i=0;i<rows.size();++i){ uint64_t* b=&bmstore[i*nw];
    if(rows[i].t==T_BITMAP) memcpy(b,rows[i].bm,(size_t)nw*8);
    else if(rows[i].t==T_ARRAY16) for(uint32_t v:rows[i].v) b[v>>6]|=1ull<<(v&63);
    else for(size_t k=0;k<rows[i].rs.size();++k) for(uint32_t q=rows[i].rs[k];q<rows[i].re[k];++q) b[q>>6]|=1ull<<(q&63);
  }
  std::vector<uint16_t> arena; std::vector<uint32_t> aoff(rows.size()+1,0);
  for(size_t i=0;i<rows.size();++i){ aoff[i]=(uint32_t)arena.size();
    if(rows[i].t==T_ARRAY16) arena.insert(arena.end(),rows[i].u16,rows[i].u16+rows[i].n16); }
  aoff[rows.size()]=(uint32_t)arena.size();

  std::vector<BitmapView> BV(rows.size()); std::vector<ListView> SV(rows.size()); std::vector<RunView> RV(rows.size());
  for(size_t i=0;i<rows.size();++i){ BV[i]=BitmapView{&bmstore[i*nw],nw,nullptr,nullptr,0,8};
    SV[i]=ListView{rows[i].v.data(),(uint32_t)rows[i].v.size()};
    RV[i]=RunView{rows[i].rs.data(),rows[i].re.data(),(uint32_t)rows[i].rs.size()}; }

  std::vector<std::pair<uint32_t,uint32_t>> pr;
  for(size_t i=0;i<rows.size()&&pr.size()<20000;++i)for(size_t j=i+1;j<rows.size()&&pr.size()<20000;++j){
    bool id=rows[i].card>=rows[j].card; pr.push_back({(uint32_t)(id?i:j),(uint32_t)(id?j:i)});}

  auto L=cell_bs(); auto get=[&](const char*n){auto r=L.v[0].fn;for(size_t k=0;k<L.n;++k)if(std::string(L.v[k].name)==n)r=L.v[k].fn;return r;};
  auto bs_small=get("small"); auto bs_ilp8=get("ilp8");
  auto LB=cell_bb(); auto getb=[&](const char*n){auto r=LB.v[0].fn;for(size_t k=0;k<LB.n;++k)if(std::string(LB.v[k].name)==n)r=LB.v[k].fn;return r;};
  auto bb=getb("dense");
  auto T=[&](auto fn){uint64_t b=~0ull;for(int r=0;r<7;++r){volatile uint64_t s=0;uint64_t t0=nsn();
    for(auto&q:pr)s+=fn(q);uint64_t d=nsn()-t0;(void)s;if(d<b)b=d;}return (double)b/pr.size();};

  // Query-time dispatch on the STORED tags -- the whole point of packing.
  /* Probe straight from the stored uint16 array.
   *
   * The universe is 5,008 bits, so a position needs 13 bits and the packed file
   * already stores uint16. Widening to uint32 at load time -- which the first
   * version did purely so the existing u32 kernels could be reused -- doubles
   * the bytes the probe loop streams for no benefit. At |S| = 1..4, which is 89%
   * of pairs, that traffic is a real fraction of the work. */
  auto bs_u16=[&](const BitmapView& b,const uint16_t* v,uint32_t n)->uint64_t{
    switch(n){
      case 0: return 0;
      case 1: return (b.w[v[0]>>6]>>(v[0]&63))&1u;
      case 2: return ((b.w[v[0]>>6]>>(v[0]&63))&1u)+((b.w[v[1]>>6]>>(v[1]&63))&1u);
      case 3: return ((b.w[v[0]>>6]>>(v[0]&63))&1u)+((b.w[v[1]>>6]>>(v[1]&63))&1u)
                    +((b.w[v[2]>>6]>>(v[2]&63))&1u);
      case 4: return ((b.w[v[0]>>6]>>(v[0]&63))&1u)+((b.w[v[1]>>6]>>(v[1]&63))&1u)
                    +((b.w[v[2]>>6]>>(v[2]&63))&1u)+((b.w[v[3]>>6]>>(v[3]&63))&1u);
      default: break;
    }
    uint64_t a0=0,a1=0,a2=0,a3=0; uint32_t i=0;
    for(;i+4<=n;i+=4){
      a0+=(b.w[v[i  ]>>6]>>(v[i  ]&63))&1u; a1+=(b.w[v[i+1]>>6]>>(v[i+1]&63))&1u;
      a2+=(b.w[v[i+2]>>6]>>(v[i+2]&63))&1u; a3+=(b.w[v[i+3]>>6]>>(v[i+3]&63))&1u; }
    uint64_t c=(a0+a1)+(a2+a3);
    for(;i<n;++i) c+=(b.w[v[i]>>6]>>(v[i]&63))&1u;
    return c; };

  auto dispatch16=[&](std::pair<uint32_t,uint32_t>&q)->uint64_t{
    const uint32_t d=q.first,s=q.second;
    if(rows[s].t==T_ARRAY16) return bs_u16(BV[d],rows[s].u16,rows[s].n16);
    if(rows[s].t==T_RLE16){ uint64_t c=0; for(uint32_t k=0;k<RV[s].n;++k)
        for(uint32_t x=RV[s].start[k];x<RV[s].end[k];++x) c+=(BV[d].w[x>>6]>>(x&63))&1u; return c; }
    return bb(BV[d],BV[s]); };

  auto dispatch_arena=[&](std::pair<uint32_t,uint32_t>&q)->uint64_t{
    const uint32_t d=q.first,s=q.second;
    if(rows[s].t==T_ARRAY16) return bs_u16(BV[d],arena.data()+aoff[s],rows[s].n16);
    if(rows[s].t==T_RLE16){ uint64_t c=0; for(uint32_t k=0;k<RV[s].n;++k)
        for(uint32_t x=RV[s].start[k];x<RV[s].end[k];++x) c+=(BV[d].w[x>>6]>>(x&63))&1u; return c; }
    return bb(BV[d],BV[s]); };

  auto dispatch=[&](std::pair<uint32_t,uint32_t>&q)->uint64_t{
    const uint32_t d=q.first,s=q.second;
    if(rows[s].t==T_ARRAY16) return bs_small(BV[d],SV[s]);
    if(rows[s].t==T_RLE16)   { uint64_t c=0; for(uint32_t k=0;k<RV[s].n;++k){
        for(uint32_t x=RV[s].start[k];x<RV[s].end[k];++x) c+=(BV[d].w[x>>6]>>(x&63))&1u;} return c; }
    return bb(BV[d],BV[s]);
  };
  uint64_t w=0; for(auto&q:pr) w+=bb(BV[q.first],BV[q.second]);
  uint64_t g=0; for(auto&q:pr) g+=dispatch(q);
  printf("%-10s rows=%zu pairs=%zu  packed=%.1f B/row  correct=%s\n",
     path,rows.size(),pr.size(),(double)bytes_used/rows.size(),w==g?"yes":"NO");
  uint64_t g16=0; for(auto&q:pr) g16+=dispatch16(q);
  if(g16!=w) printf("   !! u16 dispatch WRONG\n");
  uint64_t ga=0; for(auto&q:pr) ga+=dispatch_arena(q);
  if(ga!=w) printf("   !! arena dispatch WRONG\n");
  printf("   u16-dispatch    %7.3f   u16-arena %7.3f   (arena %.1f kB)\n",
         T(dispatch16),T(dispatch_arena),arena.size()*2/1024.0);
  printf("   all-bitmap %7.3f   packed-dispatch %7.3f   speedup %.2fx\n",
     T([&](std::pair<uint32_t,uint32_t>&q){return bb(BV[q.first],BV[q.second]);}),
     T(dispatch), T([&](std::pair<uint32_t,uint32_t>&q){return bb(BV[q.first],BV[q.second]);})/T(dispatch));
  return 0;
}
