#!/usr/bin/env python3
"""Generate a STORMBIN corpus from gnomAD's EMPIRICAL per-site allele-frequency
distribution, for the regime question in RESEARCH_PLAN.md §17 (C25): does
real demography (as opposed to a neutral-constant-Ne coalescent) let a
synthetic genotype corpus reach the project's target regime (universe m >= 1e6
AND mean density <= 1e-3)?

Input: a TSV with columns POS, AC, AN, AF, one row per gnomAD PASS site,
e.g. produced by:

    bcftools query -r chr21 -i 'FILTER="PASS"' \\
      -f '%POS\\t%INFO/AC\\t%INFO/AN\\t%INFO/AF\\n' \\
      https://storage.googleapis.com/gcp-public-data--gnomad/release/4.1/vcf/exomes/gnomad.exomes.v4.1.sites.chr21.vcf.bgz

This script does NOT read individual genotypes -- gnomAD publishes none (it is
aggregate-only by design/consent). For each surviving site it draws a
synthetic carrier count c ~ Binomial(n_hap, AF) and then c carrier indices
uniformly at random without replacement from [0, n_hap), matching the
STORMBIN row semantic used by tools/gt2bin.py and tools/msprime2bin.py: "the
ascending, deduplicated indices of haplotypes carrying the derived allele".
The real AC/AN at the site are used only to select which sites survive an
optional rarity filter (--ac-max); the emitted carrier SET is a fresh draw,
not the real (unpublished) carrier list.

Why redraw instead of reusing the site's own AC directly: AN varies site to
site (coverage/missingness), so AC alone does not fit a single global n_hap
STORMBIN universe. Binomial(n_hap, AF) is the standard way to project a
population AF estimate onto an arbitrary sample size -- the same modeling
step tools/msprime2bin.py's simulator performs implicitly via the coalescent.

STORMBIN format (must match tools/gt2bin.py exactly):
    b'STORMBIN' + <IIII little-endian> = (version=1, n_rows, n_bits, pad=0)
    then per row: <I> count, then `count` little-endian uint32 indices,
    strictly ascending, deduplicated (guaranteed by np.random.choice without
    replacement + sort).
The header is written with n_rows=0 first and patched at offset 8 once the
true row count is known.
"""
import argparse
import struct
import sys
import time

import numpy as np


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="inp", required=True,
                     help="TSV path (POS, AC, AN, AF) or '-' for stdin")
    ap.add_argument("--n-hap", type=int, required=True,
                     help="synthetic universe size (n_bits); carriers are "
                          "drawn Binomial(n_hap, AF) per site")
    ap.add_argument("--ac-max", type=int, default=0,
                     help="rarity filter: keep only sites with real AC <= "
                          "this (0 = no filter). Matches the 'rare-variant "
                          "burden testing' workload named in the task.")
    ap.add_argument("--af-max", type=float, default=0.0,
                     help="alternative rarity filter: keep only sites with "
                          "real AF < this (0 = no filter)")
    ap.add_argument("--max-rows", type=int, default=0,
                     help="cap on emitted rows, 0 = no cap")
    ap.add_argument("--seed", type=int, default=1000)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    n_hap = args.n_hap

    f_in = sys.stdin if args.inp == "-" else open(args.inp)

    n_read = 0
    n_filtered_out = 0
    n_skipped_monomorphic = 0
    n_rows = 0
    total_set = 0
    bytes_written = 0
    t0 = time.time()

    with open(args.out, "wb") as f_out:
        f_out.write(b"STORMBIN" + struct.pack("<IIII", 1, 0, 0, 0))
        for line in f_in:
            line = line.rstrip("\n")
            if not line:
                continue
            pos, ac_s, an_s, af_s = line.split("\t")
            n_read += 1
            if af_s == ".":
                continue
            ac = int(ac_s)
            af = float(af_s)

            if args.ac_max and ac > args.ac_max:
                n_filtered_out += 1
                continue
            if args.af_max and af >= args.af_max:
                n_filtered_out += 1
                continue

            c = int(rng.binomial(n_hap, min(af, 1.0)))
            if c == 0 or c == n_hap:
                n_skipped_monomorphic += 1
                continue

            idx = rng.choice(n_hap, size=c, replace=False)
            idx.sort()
            idx = idx.astype("<u4")

            f_out.write(struct.pack("<I", c))
            f_out.write(idx.tobytes())
            total_set += c
            bytes_written += 4 + 4 * c
            n_rows += 1

            if n_rows % 100000 == 0:
                print(f"  {n_rows} rows written, {bytes_written/1e6:.1f} MB, "
                      f"{time.time()-t0:.1f}s...", file=sys.stderr, flush=True)
            if args.max_rows and n_rows >= args.max_rows:
                break

        f_out.seek(8)
        f_out.write(struct.pack("<IIII", 1, n_rows, n_hap, 0))

    if f_in is not sys.stdin:
        f_in.close()

    t1 = time.time()
    mean_card = total_set / max(1, n_rows)
    density = mean_card / n_hap
    print(f"[gnomad2bin] read {n_read} sites, filtered out {n_filtered_out} "
          f"(ac_max={args.ac_max or 'none'}, af_max={args.af_max or 'none'}), "
          f"{n_skipped_monomorphic} monomorphic draws skipped",
          file=sys.stderr)
    print(f"[gnomad2bin] wrote {args.out}: {n_rows} rows x {n_hap} haplotypes, "
          f"{total_set} set bits, mean |Xi| {mean_card:.3f}, density "
          f"{density:.8g}, wall {t1-t0:.1f}s", file=sys.stderr)
    print(f"SUMMARY out={args.out} n_hap={n_hap} n_rows={n_rows} "
          f"total_set={total_set} mean_card={mean_card:.4f} "
          f"density={density:.8g} ac_max={args.ac_max} af_max={args.af_max} "
          f"seed={args.seed} wall_s={t1-t0:.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
