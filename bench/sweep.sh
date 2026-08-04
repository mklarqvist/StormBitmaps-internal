#!/usr/bin/env bash
# Standard corpora for per-cell optimization.
#
# RESEARCH_PLAN.md 7.2 requires both generator axes to be swept, and standing
# rule 4 requires the 1/i spectrum to appear alongside the uniform one in every
# report. These five points are the working set used while iterating on kernels;
# the full grid of 7.2 is for the paper, not for a tuning loop.
#
# Usage: bench/sweep.sh [cells] [extra bench_cells args...]
set -euo pipefail

BIN=${BIN:-./build/bench_cells}
CELLS=${1:-all}
shift || true

run() {
  local label="$1"; shift
  echo
  echo "=============================================================================="
  echo "  $label"
  echo "=============================================================================="
  "$BIN" --cells "$CELLS" "$@" "${EXTRA[@]:-}"
}

EXTRA=("$@")

# U1 -- uniform structure, uniform spectrum. The misleading classic: every row
# the same density, no skew. Included precisely so the gap against S1 is visible.
run "U1  uniform structure / uniform spectrum / d=0.01" \
    --rows 192 --universe 65536 --density 0.01 --structure uniform --spectrum uniform --pairs 8000

# S1 -- uniform structure, 1/i spectrum. Same mean cardinality as U1 by
# construction (the 1/i limit is solved for it), so the ONLY difference is skew.
run "S1  uniform structure / 1-over-i spectrum / d=0.01" \
    --rows 192 --universe 65536 --density 0.01 --structure uniform --spectrum inverse --pairs 8000

# C1 -- clustered, 1/i. Realistic: skewed AND locally clustered.
run "C1  clustered(0.9) / 1-over-i spectrum / d=0.01" \
    --rows 192 --universe 65536 --density 0.01 --structure clustered --spectrum inverse --pairs 8000

# L1 -- long runs. The regime R and W exist for, and the one that tests P4
# (run length must drop out of B x R's cost).
run "L1  runs(mean=4096) / uniform spectrum / d=0.05" \
    --rows 192 --universe 262144 --density 0.05 --structure runs --mean-run 4096 --spectrum uniform --pairs 8000

# D1 -- dense. Where B x B is genuinely the right kernel and its ceiling matters.
run "D1  uniform structure / uniform spectrum / d=0.35" \
    --rows 192 --universe 65536 --density 0.35 --structure uniform --spectrum uniform --pairs 8000
