#!/usr/bin/env bash
# Claim P4: with a rank index, B x R costs Theta(runs) -- INDEPENDENT of run length.
#
# RESEARCH_PLAN.md 4.5 is explicit that B x R timing must be reported against run
# count and run length SEPARATELY, because a single "runs per second" figure
# hides exactly the thing being claimed. So this sweep holds the run COUNT fixed
# at ~16 per row and varies run LENGTH over 256x, by scaling density with length:
#
#     n_runs = density * universe / mean_run    ->   density proportional to mean_run
#
# P4 predicts:
#   rank      flat in ns/run across the whole sweep
#   neon      linear in mean_run (it must touch every word of every run)
set -euo pipefail
BIN=${BIN:-./build/bench_cells}
U=$((1 << 20))          # 1,048,576-bit universe

echo "P4: run-length independence of B x R.  universe=${U}, run count held at ~16/row"
echo
printf "%10s %10s %12s %12s %12s %12s\n" \
       "mean_run" "density" "runs/pair" "neon ns/run" "rank ns/run" "neon/rank"

for L in 64 256 1024 4096 16384; do
  D=$(python3 -c "print(16.0 * $L / $U)")
  OUT=$("$BIN" --cells br --universe "$U" --density "$D" --structure runs \
                --mean-run "$L" --spectrum uniform --rows 96 --pairs 3000 --repeats 5 2>&1)
  RUNS=$(echo "$OUT" | sed -n 's/.*work unit: run, mean \([0-9.]*\) per pair.*/\1/p')
  NEON=$(echo "$OUT" | awk '/^  neon /{print $4}')
  RANK=$(echo "$OUT" | awk '/^  rank /{print $4}')
  RATIO=$(python3 -c "print(f'{$NEON/$RANK:.2f}x')" 2>/dev/null || echo "-")
  printf "%10s %10.6f %12s %12s %12s %12s\n" "$L" "$D" "$RUNS" "$NEON" "$RANK" "$RATIO"
done
