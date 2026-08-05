#!/usr/bin/env python3
"""Simulate a coalescent genomic corpus with msprime and emit it directly to
STORMBIN (variant-major), for StormBitmaps' all-pairs intersection benchmarks.

Why simulate at all (see RESEARCH_PLAN.md 15.4, data/README.md "Candidates
identified but not yet used"): the project's synthetic corpus generator
(kernels/storm_gen.cpp) fakes the 1/i site-frequency spectrum with a
hand-rolled log-uniform draw and a solved upper limit. No open real dataset of
human genetic variants reaches the target regime (universe m >= 1e6 AND mean
density <= 1e-3) at the same time -- human variant corpora are either large
universe (haplotype-major, ~1e6 sites) with density ~3% or small universe
(variant-major, 5,008 haplotypes) with the right shape (data/README.md, Group
3). msprime lets us pick the sample size (universe) independently of that
constraint, while the derived 1/i shape falls out of the coalescent process
itself rather than being hard-coded.

Model
-----
msprime.sim_ancestry(samples=N, ploidy=2, ...) simulates N diploid individuals
(2N haplotypes) under the coalescent with recombination (Hudson's algorithm),
population_size = Ne (diploid effective population size; coalescent time is
scaled in units of 2*Ne generations). msprime.sim_mutations then drops
mutations on the resulting ancestral recombination graph at rate
`mutation_rate` per site per generation, using BinaryMutationModel so every
site is strictly biallelic ("0" ancestral / "1" derived, ancestral state
always "0") -- this matches the STORMBIN row semantic exactly: "the indices of
haplotypes carrying the derived allele", with no multi-allelic ambiguity to
resolve.

Caveat (report, do not paper over): when the number of *sampled* haplotypes
approaches or exceeds 2*Ne, the continuous-time Kingman coalescent that
msprime's default (Hudson) algorithm implements is an approximation to the
discrete Wright-Fisher process that was derived for n << N; near time 0 with
n >~ N one should in principle use `model="dtwf"` for exactness. We do not
switch models here (the task fixes population_size=10000 for all three
corpora including the 1,000,000-diploid one, i.e. n/(2Ne) up to 100x), so the
extreme tip of corpus C's genealogy is a known approximation, not a measured
fact -- flagged here rather than silently assumed correct.

Memory note
-----------
Never call ts.genotype_matrix(): it materialises a dense (num_sites x
num_samples) int8 array, which at 2,000,000 haplotypes and tens of thousands
of sites would need hundreds of GB. Instead this script iterates
`mts.variants()`, which yields one site's genotype vector at a time (a numpy
array of length num_samples), and streams each row straight to disk.

STORMBIN format written (must match tools/gt2bin.py exactly):
    b'STORMBIN' + <IIII little-endian> = (version=1, n_rows, n_bits, pad=0)
    then per row: <I> count, then `count` little-endian uint32 indices,
    strictly ascending, deduplicated (guaranteed here because genotypes are
    binary per haplotype, so numpy .nonzero() on one site's genotype vector
    yields each carrying haplotype's index exactly once, in ascending order).
The header is written with n_rows=n_bits=0 first and patched at offset 8 once
the true row/haplotype counts are known.
"""
import argparse
import resource
import struct
import sys
import time

import msprime


def peak_rss_mb() -> float:
    """Peak resident set size of this process so far, in MiB.

    ru_maxrss is bytes on macOS/BSD, KiB on Linux -- both msprime's C
    extension allocations and Python's own land in the same process, so this
    single number captures the simulation's real memory footprint.
    """
    r = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return r / (1024.0 * 1024.0) if sys.platform == "darwin" else r / 1024.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--diploid-samples", type=int, required=True,
                     help="number of diploid individuals; haplotypes = 2x this")
    ap.add_argument("--sequence-length", type=float, default=10e6)
    ap.add_argument("--recombination-rate", type=float, default=1e-8)
    ap.add_argument("--mutation-rate", type=float, default=1e-8)
    ap.add_argument("--population-size", type=float, default=10000)
    ap.add_argument("--seed", type=int, default=1000,
                     help="ancestry uses this seed; mutations use seed+1")
    ap.add_argument("--max-rows", type=int, default=200000,
                     help="cap on emitted variant sites (STORMBIN rows)")
    ap.add_argument("--max-bytes", type=int, default=0,
                     help="optional disk-footprint cap (bytes of row payload, "
                          "i.e. 4 + 4*count per row). 0 = disabled. Needed "
                          "because the neutral coalescent's mean derived-allele "
                          "count per site is ~n_hap/H(n_hap-1) -- it falls only "
                          "logarithmically with sample size, not linearly, so "
                          "large-n_hap corpora can be far bigger than --max-rows "
                          "alone would suggest.")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    n_hap = args.diploid_samples * 2
    t0 = time.time()
    print(f"[ancestry] {args.diploid_samples} diploid samples ({n_hap} haplotypes), "
          f"L={args.sequence_length:.0f}, r={args.recombination_rate}, "
          f"Ne={args.population_size}, seed={args.seed}", file=sys.stderr, flush=True)

    ts = msprime.sim_ancestry(
        samples=args.diploid_samples,
        ploidy=2,
        sequence_length=args.sequence_length,
        recombination_rate=args.recombination_rate,
        population_size=args.population_size,
        random_seed=args.seed,
    )
    t1 = time.time()
    print(f"[ancestry] done in {t1 - t0:.1f}s: {ts.num_trees} trees, "
          f"{ts.num_edges} edges, {ts.num_nodes} nodes, "
          f"peak RSS {peak_rss_mb():.0f} MiB", file=sys.stderr, flush=True)

    mut_seed = args.seed + 1
    mts = msprime.sim_mutations(
        ts, rate=args.mutation_rate, random_seed=mut_seed,
        model=msprime.BinaryMutationModel(),
    )
    t2 = time.time()
    n_sites_total = mts.num_sites
    print(f"[mutations] done in {t2 - t1:.1f}s (seed={mut_seed}): "
          f"{n_sites_total} sites total, peak RSS {peak_rss_mb():.0f} MiB",
          file=sys.stderr, flush=True)

    n_rows = 0
    total_set = 0
    n_skipped_monomorphic = 0
    bytes_written = 0
    stop_reason = "exhausted all sites"
    spectrum = {}
    with open(args.out, "wb") as f:
        f.write(b"STORMBIN" + struct.pack("<IIII", 1, 0, 0, 0))
        for var in mts.variants():
            gt = var.genotypes  # 0/1 per haplotype, BinaryMutationModel guarantees biallelic
            idx = gt.nonzero()[0]  # ascending, unique by construction
            c = int(idx.shape[0])
            if c == 0 or c == n_hap:
                n_skipped_monomorphic += 1
                continue
            spectrum[c] = spectrum.get(c, 0) + 1
            f.write(struct.pack("<I", c))
            f.write(idx.astype("<u4").tobytes())
            total_set += c
            bytes_written += 4 + 4 * c
            n_rows += 1
            if n_rows % 5000 == 0:
                print(f"  {n_rows} rows written, {bytes_written/1e9:.2f} GB...",
                      file=sys.stderr, flush=True)
            if n_rows >= args.max_rows:
                stop_reason = f"hit --max-rows cap of {args.max_rows}"
                break
            if args.max_bytes and bytes_written >= args.max_bytes:
                stop_reason = (f"hit --max-bytes cap of {args.max_bytes/1e9:.2f} GB "
                                f"(disk-footprint budget, not a simulation limit)")
                break
        f.seek(8)
        f.write(struct.pack("<IIII", 1, n_rows, n_hap, 0))
    print(f"[emit] stopped: {stop_reason}; {n_sites_total} sites existed in the "
          f"mutated tree sequence, {n_rows} rows actually written",
          file=sys.stderr, flush=True)

    t3 = time.time()
    mean_card = total_set / max(1, n_rows)
    print(f"[emit] wrote {args.out}: {n_rows} rows x {n_hap} haplotypes "
          f"({n_sites_total} sites existed, {n_skipped_monomorphic} monomorphic skipped), "
          f"{total_set} set bits, mean |Xi| {mean_card:.1f}, "
          f"density {mean_card / n_hap:.6g}", file=sys.stderr, flush=True)
    print(f"[timing] total wall {t3 - t0:.1f}s, peak RSS {peak_rss_mb():.0f} MiB",
          file=sys.stderr, flush=True)
    # Machine-readable summary line for the calling shell to pick up.
    print(f"SUMMARY out={args.out} n_hap={n_hap} n_sites_total={n_sites_total} "
          f"n_rows={n_rows} total_set={total_set} mean_card={mean_card:.3f} "
          f"density={mean_card / n_hap:.8g} wall_s={t3 - t0:.1f} "
          f"peak_rss_mb={peak_rss_mb():.0f} seed={args.seed} mut_seed={mut_seed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
