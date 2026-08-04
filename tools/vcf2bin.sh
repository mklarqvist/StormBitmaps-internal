#!/usr/bin/env bash
# 1000 Genomes phased VCF -> the flat binary corpus format read by bench_real.
#
# RESEARCH_PLAN.md 7.2 requires "at minimum one REAL dataset dumped to raw
# bitmaps as a sanity anchor". Everything measured until now is synthetic, and a
# generator can accidentally encode the very structure the kernels exploit --
# which is exactly the failure mode 7.2 warns about. This is that anchor.
#
# Each VCF record becomes one ROW: a bitmap over 2N haplotypes, bit set where
# that haplotype carries the ALT allele. Phased data (|) gives two independent
# haplotype bits per sample, which is the shape PROBLEM_STATEMENT.md 2.1
# describes.
#
# Format (little-endian, the layout kernels/storm_repr.h wants):
#   magic   "STORMBIN"          8 bytes
#   version uint32 = 1
#   n_rows  uint32
#   n_bits  uint32              universe = 2 * n_samples
#   pad     uint32
#   then n_rows records: uint32 popcount, then popcount x uint32 positions
#
# Sorted distinct positions per row, which is build_row()'s contract.
set -euo pipefail
VCF=${1:?usage: vcf2bin.sh IN.vcf.gz OUT.bin [MAX_ROWS]}
OUT=${2:?}
MAX=${3:-0}

command -v bcftools >/dev/null || { echo "bcftools required"; exit 1; }
echo "samples: $(bcftools query -l "$VCF" | wc -l | tr -d ' ')"

# bcftools emits one line per record: the GT of every sample, tab separated.
# Only biallelic SNVs -- multiallelic records would need a per-ALT bitmap each
# and that is a different (and larger) matrix than the one being claimed.
bcftools view -m2 -M2 -v snps "$VCF" 2>/dev/null \
  | bcftools query -f '[%GT\t]\n' 2>/dev/null \
  | python3 tools/gt2bin.py "$OUT" "$MAX"
