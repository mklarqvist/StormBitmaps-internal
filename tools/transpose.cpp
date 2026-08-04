/* Transpose the 1000 Genomes matrix to haplotype-major.
 *
 * RESEARCH_PLAN.md 14, claim C8: the 10^7-bit universe that PROBLEM_STATEMENT.md
 * 2 motivates has never been MEASURED -- only extrapolated -- and the one real
 * measurement (variant-major chr20, 5,008-bit universe, 632 B/row) gives 2.15x,
 * because the universe is far too small for the asymptotics.
 *
 * The same file supplies the large regime for free by transposing it. A ROW
 * becomes one haplotype and the UNIVERSE becomes the variants it carries:
 *
 *   variant-major:   1,739,315 rows x     5,008 bits  =    632 B/row
 *   haplotype-major:     5,008 rows x 1,739,315 bits  = 217 kB/row
 *
 * 344x larger rows, from real data, with no extrapolation -- and this
 * orientation is the biologically meaningful one for relatedness/IBD, where the
 * question is which variants two haplotypes share.
 *
 * Output is the same STORMBIN format so every existing tool reads it.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
using namespace std;

int main(int argc, char** argv) {
    const char* in  = argc > 1 ? argv[1] : "data/chr20.bin";
    const char* out = argc > 2 ? argv[2] : "data/chr20_hap.bin";
    const uint32_t max_var = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;   // 0 = all

    FILE* f = fopen(in, "rb");
    if (!f) { printf("cannot open %s\n", in); return 1; }
    char magic[9] = {0}; uint32_t ver, nr, nb, pad;
    if (fread(magic,1,8,f)!=8 || memcmp(magic,"STORMBIN",8) ||
        fread(&ver,4,1,f)!=1 || fread(&nr,4,1,f)!=1 ||
        fread(&nb,4,1,f)!=1 || fread(&pad,4,1,f)!=1) { printf("bad header\n"); return 1; }

    const uint32_t nvar = (max_var && max_var < nr) ? max_var : nr;
    printf("transposing %u variants x %u haplotypes -> %u rows x %u bits (%.0f kB/row)\n",
           nvar, nb, nb, nvar, nvar / 8.0 / 1024.0);

    // One pass, accumulating each haplotype's variant list. Memory is the
    // transposed matrix itself; at 5,008 x 1.74M bits that is ~1.1 GB of
    // positions, so the lists are built as bitmaps and emitted as positions.
    const uint32_t vw = (nvar + 63) / 64;
    vector<uint64_t> bm((size_t)nb * vw, 0);
    vector<uint32_t> pos;
    uint64_t total = 0;
    for (uint32_t v = 0; v < nvar; ++v) {
        uint32_t c;
        if (fread(&c,4,1,f) != 1) break;
        pos.resize(c);
        if (c && fread(pos.data(),4,c,f) != c) break;
        for (uint32_t k = 0; k < c; ++k) {
            const uint32_t h = pos[k];
            if (h < nb) { bm[(size_t)h * vw + (v >> 6)] |= 1ull << (v & 63); ++total; }
        }
        if ((v & 0xFFFFF) == 0xFFFFF) { printf("  %u variants...\n", v + 1); fflush(stdout); }
    }
    fclose(f);

    FILE* g = fopen(out, "wb");
    fwrite("STORMBIN",1,8,g);
    uint32_t one = 1, z = 0;
    fwrite(&one,4,1,g); fwrite(&nb,4,1,g); fwrite(&nvar,4,1,g); fwrite(&z,4,1,g);
    vector<uint32_t> row;
    for (uint32_t h = 0; h < nb; ++h) {
        row.clear();
        const uint64_t* w = &bm[(size_t)h * vw];
        for (uint32_t k = 0; k < vw; ++k) {
            uint64_t x = w[k];
            while (x) { row.push_back(k * 64 + (uint32_t)__builtin_ctzll(x)); x &= x - 1; }
        }
        const uint32_t n = (uint32_t)row.size();
        fwrite(&n,4,1,g);
        if (n) fwrite(row.data(),4,n,g);
    }
    fclose(g);
    printf("wrote %s: %u haplotypes x %u variants, %llu set bits, mean card %.0f (density %.4f)\n",
           out, nb, nvar, (unsigned long long)total, (double)total/nb, (double)total/nb/nvar);
    return 0;
}
