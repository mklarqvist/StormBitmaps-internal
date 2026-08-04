// Correctness tests for StormBitmaps kernels.
//
// Every kernel is checked against an independent scalar oracle. Several cases
// are regression tests for specific Phase 0 defects; each is annotated with the
// defect it pins down AND with the condition required to reach it. Reaching the
// buggy path is the hard part here -- a naive random test exercises none of
// them (see tests/README.md).
//
// Build: cc -O2 -std=c99 -I. storm.c tests/test_storm.c -o test_storm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../storm.h"

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK_EQ(got, want, ...)                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        uint64_t _g = (uint64_t)(got), _w = (uint64_t)(want);                 \
        if (_g != _w) {                                                       \
            ++g_failures;                                                     \
            printf("  FAIL %s:%d: got %llu want %llu | ", __func__, __LINE__, \
                   (unsigned long long)_g, (unsigned long long)_w);           \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* ---------------------------------------------------------------- oracles */

// |A n B| over two dense bitmaps. Uses the compiler builtin, which is
// independent of every code path under test.
static uint64_t oracle_and_popcnt(const uint64_t* a, const uint64_t* b, size_t n_words) {
    uint64_t c = 0;
    for (size_t i = 0; i < n_words; ++i) c += (uint64_t)__builtin_popcountll(a[i] & b[i]);
    return c;
}

static uint64_t oracle_merge16(const uint16_t* a, uint32_t na, const uint16_t* b, uint32_t nb) {
    uint64_t c = 0; uint32_t i = 0, j = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j]) ++i;
        else if (b[j] < a[i]) ++j;
        else { ++c; ++i; ++j; }
    }
    return c;
}

/* ------------------------------------------------------------- utilities */

static uint32_t rng_state = 12345;
static uint32_t rng(void) { // xorshift32, deterministic across platforms
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

// Sorted and deduplicated.
static uint32_t make_set(uint32_t* out, uint32_t n_set, uint32_t universe) {
    for (uint32_t i = 0; i < n_set; ++i) out[i] = rng() % universe;
    qsort(out, n_set, sizeof(uint32_t), cmp_u32);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n_set; ++i)
        if (i == 0 || out[i] != out[i-1]) out[k++] = out[i];
    return k;
}

// Sorted, duplicates RETAINED. STORM_contig_add accepts equal neighbours and
// collapses them; this is what exercises the deduplicating copy.
static uint32_t make_multiset(uint32_t* out, uint32_t n_set, uint32_t universe) {
    for (uint32_t i = 0; i < n_set; ++i) out[i] = rng() % universe;
    qsort(out, n_set, sizeof(uint32_t), cmp_u32);
    return n_set;
}

static void set_bits(uint64_t* bm, const uint32_t* v, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) bm[v[i] >> 6] |= 1ULL << (v[i] & 63);
}

/* ------------------------------------------------- B x S : the mixed cell */

// Regression: MOD(x) was ((x)*64)>>6, i.e. the identity, leaving the shift
// count unmasked (UB for x >= 64) and using a 32-bit `1L` literal.
//
// NOTE: this is NOT behaviourally observable on x86-64 or AArch64, because both
// mask a 64-bit shift count to 6 bits -- which is exactly the intended modulo.
// The fix removes real undefined behaviour (and a genuine LLP64 bug from `1L`),
// but no test can distinguish it on these ISAs. Kept for coverage of the kernel.
static void test_bitmap_x_list(void) {
    printf("B x S  (STORM_intersect_bitmaps_scalar_list)\n");
    const uint32_t universe = 4096;
    const size_t   n_words  = universe / 64;

    for (int trial = 0; trial < 400; ++trial) {
        uint32_t va[512], vb[512];
        uint32_t na = make_set(va, 1 + rng() % 64,  universe);
        uint32_t nb = make_set(vb, 1 + rng() % 256, universe);

        uint64_t* ba = calloc(n_words, sizeof(uint64_t));
        uint64_t* bb = calloc(n_words, sizeof(uint64_t));
        set_bits(ba, va, na);
        set_bits(bb, vb, nb);

        uint64_t want = oracle_and_popcnt(ba, bb, n_words);
        CHECK_EQ(STORM_intersect_bitmaps_scalar_list(ba, bb, va, vb, na, nb), want,
                 "trial=%d na=%u nb=%u", trial, na, nb);
        // The kernel picks its loop by which list is shorter; check both orders.
        CHECK_EQ(STORM_intersect_bitmaps_scalar_list(bb, ba, vb, va, nb, na), want,
                 "reversed trial=%d", trial);

        free(ba); free(bb);
    }

    for (uint32_t bit = 0; bit < 64; ++bit) {
        uint64_t bm_a[2] = {0, 0}, bm_b[2] = {0, 0};
        uint32_t pos = 64 + bit;
        bm_a[1] = 1ULL << bit;
        bm_b[1] = 1ULL << bit;
        uint32_t la[1] = { pos }, lb[1] = { pos };
        CHECK_EQ(STORM_intersect_bitmaps_scalar_list(bm_a, bm_b, la, lb, 1, 1), 1,
                 "single shared bit at position %u", pos);
    }
}

/* -------------------------------------------------------- S x S : uint16 */

static void test_list_x_list(void) {
    printf("S x S  (STORM_intersect_vector16_cardinality)\n");
    for (int trial = 0; trial < 400; ++trial) {
        uint32_t ta[600], tb[600];
        uint32_t na = make_set(ta, 1 + rng() % 300, 65535);
        uint32_t nb = make_set(tb, 1 + rng() % 300, 65535);

        uint16_t a[600], b[600];
        for (uint32_t i = 0; i < na; ++i) a[i] = (uint16_t)ta[i];
        for (uint32_t i = 0; i < nb; ++i) b[i] = (uint16_t)tb[i];

        CHECK_EQ(STORM_intersect_vector16_cardinality(a, b, na, nb),
                 oracle_merge16(a, na, b, nb), "trial=%d na=%u nb=%u", trial, na, nb);
    }
}

/* ----------------------------------------------- contiguous end-to-end */

// `dups` selects make_multiset, which is what reaches the deduplicating scalar
// copy in STORM_contig_add. With deduplicated input, n_values_used == n_values
// and the buggy and fixed copies are byte-identical.
static void test_contig(uint32_t n_vec, uint32_t universe, uint32_t max_set,
                        int dups, const char* label) {
    const size_t n_words = (universe + 63) / 64;

    uint64_t** ref = malloc(n_vec * sizeof(uint64_t*));
    STORM_contiguous_t* cont = STORM_contig_new(universe);
    uint32_t* vals = malloc((max_set + 1) * sizeof(uint32_t));

    for (uint32_t i = 0; i < n_vec; ++i) {
        uint32_t n = dups ? make_multiset(vals, 1 + rng() % max_set, universe)
                          : make_set(vals, 1 + rng() % max_set, universe);
        ref[i] = calloc(n_words, sizeof(uint64_t));
        set_bits(ref[i], vals, n); // set semantics: duplicates are idempotent
        STORM_contig_add(cont, vals, n);
    }

    uint64_t want = 0;
    for (uint32_t i = 0; i < n_vec; ++i)
        for (uint32_t j = i + 1; j < n_vec; ++j)
            want += oracle_and_popcnt(ref[i], ref[j], n_words);

    CHECK_EQ(STORM_contig_pairw_intersect_cardinality(cont), want, "%s unblocked", label);
    CHECK_EQ(STORM_contig_pairw_intersect_cardinality_blocked(cont, 8), want, "%s blocked(8)", label);

    for (uint32_t i = 0; i < n_vec; ++i) free(ref[i]);
    free(ref); free(vals);
    STORM_contig_free(cont);
}

/* ------------------------------------------------- STORM_t end-to-end */

// max_set controls which representation STORM_bitmap_cont_add picks: a run of
// >= STORM_DEFAULT_SCALAR_THRESHOLD (4096) values inside one 65536-wide block
// becomes a dense bitmap, otherwise scalar-only. Mixing both in one test is the
// only way to reach the bitmap-vs-scalar branches of
// STORM_bitmap_intersect_cardinality[_func].
static void test_storm(uint32_t n_vec, uint32_t universe, uint32_t min_set, uint32_t max_set,
                       const char* label) {
    const size_t n_words = (universe + 63) / 64;

    uint64_t** ref = malloc(n_vec * sizeof(uint64_t*));
    STORM_t* s = STORM_new();
    uint32_t* vals = malloc((max_set + 1) * sizeof(uint32_t));

    for (uint32_t i = 0; i < n_vec; ++i) {
        uint32_t want_n = min_set + (max_set > min_set ? rng() % (max_set - min_set) : 0);
        uint32_t n = make_set(vals, want_n, universe);
        ref[i] = calloc(n_words, sizeof(uint64_t));
        set_bits(ref[i], vals, n);
        STORM_add(s, vals, n);
    }

    uint64_t want = 0;
    for (uint32_t i = 0; i < n_vec; ++i)
        for (uint32_t j = i + 1; j < n_vec; ++j)
            want += oracle_and_popcnt(ref[i], ref[j], n_words);

    CHECK_EQ(STORM_pairw_intersect_cardinality(s), want, "%s", label);

    for (uint32_t i = 0; i < n_vec; ++i) free(ref[i]);
    free(ref); free(vals);
    STORM_free(s);
}

// Dense and sparse vectors over the SAME block, so pairs of mixed
// representation actually occur.
static void test_storm_mixed_representation(void) {
    printf("STORM_t mixed dense/scalar (bitmap-vs-scalar branch)\n");
    const uint32_t universe = 65536;          // exactly one block
    const size_t   n_words  = universe / 64;
    const uint32_t n_vec    = 12;

    uint64_t** ref = malloc(n_vec * sizeof(uint64_t*));
    STORM_t* s = STORM_new();
    uint32_t* vals = malloc(20000 * sizeof(uint32_t));

    for (uint32_t i = 0; i < n_vec; ++i) {
        // Alternate: >= 4096 set bits -> dense bitmap; < 4096 -> scalar only.
        uint32_t want_n = (i % 2 == 0) ? 9000 : 50;
        uint32_t n = make_set(vals, want_n, universe);
        ref[i] = calloc(n_words, sizeof(uint64_t));
        set_bits(ref[i], vals, n);
        STORM_add(s, vals, n);
    }

    uint64_t want = 0;
    for (uint32_t i = 0; i < n_vec; ++i)
        for (uint32_t j = i + 1; j < n_vec; ++j)
            want += oracle_and_popcnt(ref[i], ref[j], n_words);

    CHECK_EQ(STORM_pairw_intersect_cardinality(s), want, "mixed dense/scalar");

    for (uint32_t i = 0; i < n_vec; ++i) free(ref[i]);
    free(ref); free(vals);
    STORM_free(s);
}

int main(void) {
    printf("StormBitmaps correctness tests\n\n");

    test_bitmap_x_list();
    test_list_x_list();

    printf("contiguous all-pairs\n");
    test_contig(40,  1024,  400, 0, "dense/1024");
    test_contig(40,  4096,  3,   0, "sparse/4096");
    test_contig(60,  8192,  200, 0, "mixed/8192");
    // Duplicated input reaches the deduplicating scalar copy.
    test_contig(60,  8192,  150, 1, "duplicates/8192");
    // > 512 vectors forces the data/bitmaps realloc; > 16384 stored scalars
    // forces the scalar-buffer realloc. Both rebuild every bitmap pointer.
    test_contig(600, 40000, 150, 0, "realloc/40000");
    test_contig(600, 40000, 150, 1, "realloc+dups/40000");

    printf("STORM_t all-pairs\n");
    test_storm(30, 200000, 1, 300, "multi-block sparse");
    test_storm(30, 100000, 1, 3,   "very sparse");
    test_storm_mixed_representation();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
