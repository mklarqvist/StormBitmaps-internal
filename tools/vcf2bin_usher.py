#!/usr/bin/env python3
"""Stream the UShER SARS-CoV-2 public MAT VCF into STORMBIN.

Source: https://hgdownload.soe.ucsc.edu/goldenPath/wuhCor1/UShER_SARS-CoV-2/
        public-latest.all.masked.vcf.gz
This is the only open genomic dataset that reaches the project's target
regime (universe >= 1e6 AND density <= 1e-3): ~8.45M genome columns, one row
per variant site. See data/README.md "Candidates identified but not yet used".

Layout is VARIANT-MAJOR: one row per site, one column per genome. Genotypes
are haploid single-character tokens: '0' (reference), '1'/'2'/'3'/... (ALT
allele index for multiallelic sites), or '.' (missing). Per the task spec,
any token that is not '0' and not '.' counts as carrying the variant.

Does NOT materialize a dense matrix -- there are ~8.45M columns, so a Python
per-sample loop (200k rows x 8.45M columns = ~1.7e12 token inspections) is not
viable. Instead the post-FORMAT genotype blob for each line is treated as a
fixed-stride byte array: since genotype tokens are almost always exactly one
byte, the blob "0\\t0\\t3\\t...\\t0" has tokens at even byte offsets and tabs
at odd offsets, so a single numpy stride-2 slice + comparison replaces the
per-sample loop. Rows where that assumption fails (any token longer than one
byte, e.g. a site with >=10 ALT alleles) are detected via a tab-position check
and re-parsed exactly with a plain split() fallback, so correctness never
depends on the fast path.

STORMBIN layout (must match tools/gt2bin.py and bench/bench_real.cpp exactly):
    b'STORMBIN' <IIII> little-endian = (version=1, n_rows, n_bits, pad=0)
    then per row: <I> count, followed by `count` little-endian uint32
    positions, STRICTLY ASCENDING and deduplicated. Header n_rows/n_bits are
    placeholders on first write, patched by seeking back to offset 8 once the
    true counts are known.
"""
import argparse
import gzip
import struct
import sys
import time

import numpy as np

DOT = ord('.')
ZERO = ord('0')
TAB = ord('\t')


def parse_fast(remainder, n_samples):
    """Vectorized path: assumes every genotype token is exactly one byte.

    Returns (positions, ok). ok=False means the one-byte assumption failed
    for this row (checked via the tab-parity test) and the caller must
    re-parse with parse_slow.
    """
    arr = np.frombuffer(remainder, dtype=np.uint8)
    if arr.size != 2 * n_samples - 1:
        return None, False
    toks = arr[0::2]
    tabs = arr[1::2]
    if tabs.size and not np.all(tabs == TAB):
        return None, False
    mask = (toks != ZERO) & (toks != DOT)
    return np.nonzero(mask)[0].astype('<u4'), True


def parse_slow(remainder):
    """Exact fallback: plain split, for rows with >=2-byte genotype tokens."""
    toks = remainder.split(b'\t')
    pos = [i for i, g in enumerate(toks) if g != b'0' and g != b'.']
    return np.array(pos, dtype='<u4')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('vcf_gz', help='path to public-latest.all.masked.vcf.gz')
    ap.add_argument('out', help='output STORMBIN path')
    ap.add_argument('--max-rows', type=int, default=200000,
                     help='cap on number of variant sites emitted (default 200000, 0=unlimited)')
    ap.add_argument('--progress-every', type=int, default=5000)
    args = ap.parse_args()

    n_samples = None
    n_rows = 0
    n_scanned_data_lines = 0
    total_set = 0
    max_card = 0
    slow_fallbacks = 0
    t0 = time.time()

    with open(args.out, 'wb') as out_f:
        out_f.write(b'STORMBIN' + struct.pack('<IIII', 1, 0, 0, 0))  # patched at the end

        with gzip.open(args.vcf_gz, 'rb') as f:
            for line in f:
                if line.startswith(b'#'):
                    if line.startswith(b'#CHROM'):
                        ncols = line.count(b'\t') + 1
                        n_samples = ncols - 9
                        print(f'# header: {ncols} columns -> {n_samples} genomes (universe)',
                              file=sys.stderr, flush=True)
                    continue

                if n_samples is None:
                    raise RuntimeError('hit a data line before #CHROM header')

                n_scanned_data_lines += 1

                if args.max_rows and n_rows >= args.max_rows:
                    # Keep scanning (without parsing genotypes) only far enough
                    # to report a total count is NOT done here -- that would
                    # require decompressing the whole 678MB file twice. The
                    # caller gets the true total from a separate full scan.
                    break

                line = line.rstrip(b'\r\n')
                # fields: CHROM POS ID REF ALT QUAL FILTER INFO FORMAT <samples...>
                remainder = line.split(b'\t', 9)[9]

                pos, ok = parse_fast(remainder, n_samples)
                if not ok:
                    slow_fallbacks += 1
                    pos = parse_slow(remainder)

                out_f.write(struct.pack('<I', pos.size))
                if pos.size:
                    out_f.write(pos.tobytes())

                total_set += int(pos.size)
                if pos.size > max_card:
                    max_card = int(pos.size)
                n_rows += 1

                if n_rows % args.progress_every == 0:
                    dt = time.time() - t0
                    print(f'  {n_rows} variants converted ({dt:.1f}s, '
                          f'{n_rows/dt:.1f} rows/s, {slow_fallbacks} slow-path rows)',
                          file=sys.stderr, flush=True)

        out_f.seek(8)
        out_f.write(struct.pack('<IIII', 1, n_rows, n_samples, 0))

    dt = time.time() - t0
    mean_card = total_set / max(1, n_rows)
    density = mean_card / max(1, n_samples)
    print(f'wrote {args.out}: {n_rows} rows x {n_samples} genomes, '
          f'{total_set} set bits, mean cardinality {mean_card:.2f} '
          f'(density {density:.8f}), max cardinality {max_card}, '
          f'{slow_fallbacks} slow-path rows, {dt:.1f}s total',
          file=sys.stderr)


if __name__ == '__main__':
    main()
