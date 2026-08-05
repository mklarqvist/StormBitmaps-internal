#!/usr/bin/env bash
# Evaluate the filter gate across every corpus, with medians over independent
# processes. Single runs of bench_bloom swing up to 2x, enough to invert a
# verdict, so no gate decision should be read from one.
#
# Self-check built in: where the gate says BYPASS, AUTO executes exactly the
# unfiltered kernel, so its median MUST be 1.00. Any other value means the
# harness is lying and no other column can be trusted.
set -u
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
N=${N:-7}; BITS=${BITS:-16384}; REP=${REP:-9}
printf "%-24s %7s %8s %8s %10s %10s\n" corpus gate surv touch "always(med)" "AUTO(med)"
while read -r name rows stride; do
  [ -z "$name" ] && continue
  a=""; u=""; g=""; sv=""; tc=""
  for i in $(seq 1 "$N"); do
    o=$(./build_portable/bench_bloom --file "$SP/stormbin/$name.bin" --rows "$rows" \
        --stride "$stride" --bits "$BITS" --repeats "$REP" --tag "$name" 2>/dev/null)
    a="$a $(echo "$o" | grep -E '^coarse fixed  ' | awk '{print $NF}' | tr -d x)"
    u="$u $(echo "$o" | grep -E '^AUTO ' | awk '{print $NF}' | tr -d x)"
    g=$(echo "$o" | grep -o 'USE FILTER\|bypass' | head -1)
    sv=$(echo "$o" | sed -n 's/.*GATE survival=\([0-9.]*\).*/\1/p' | head -1)
    tc=$(echo "$o" | sed -n 's/.*touch=\([0-9.]*\).*/\1/p' | head -1)
  done
  echo "$name|$g|$sv|$tc|$a|$u" | .venv/bin/python -c "
import sys, statistics
n,g,sv,tc,a,u = sys.stdin.read().strip().split('|')
med = lambda s: statistics.median([float(x) for x in s.split()]) if s.split() else float('nan')
print('%-24s %7s %8s %8s %10.2f %10.2f' % (n, g, sv, tc, med(a), med(u)))
"
done
