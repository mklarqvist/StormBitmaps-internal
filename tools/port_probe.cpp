// Isolated singleton-probe loop, the converged 1KGP3 kernel, for disassembly
// and static port analysis. W=6 pairs in flight, exactly as shipped.
#include <cstdint>
#include <cstddef>
extern "C" uint64_t probe6(const uint64_t* const* bw, const uint16_t* pos, size_t n) {
    uint64_t a0=0,a1=0,a2=0,a3=0,a4=0,a5=0; size_t k=0;
    for (; k+6<=n; k+=6) {
        const uint16_t p0=pos[k],p1=pos[k+1],p2=pos[k+2],p3=pos[k+3],p4=pos[k+4],p5=pos[k+5];
        a0+=(bw[k  ][p0>>6]>>(p0&63))&1u; a1+=(bw[k+1][p1>>6]>>(p1&63))&1u;
        a2+=(bw[k+2][p2>>6]>>(p2&63))&1u; a3+=(bw[k+3][p3>>6]>>(p3&63))&1u;
        a4+=(bw[k+4][p4>>6]>>(p4&63))&1u; a5+=(bw[k+5][p5>>6]>>(p5&63))&1u;
    }
    uint64_t c=(a0+a1)+(a2+a3)+(a4+a5);
    for (; k<n; ++k){ const uint16_t p=pos[k]; c+=(bw[k][p>>6]>>(p&63))&1u; }
    return c;
}
