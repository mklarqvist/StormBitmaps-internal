#!/usr/bin/env bash
# Cmake-free build. Two of the three remote hosts have no cmake, and CRoaring
# is consumed as the v4.7.2 amalgamation precisely so this stays a plain
# compiler invocation.
#
# -march=native is deliberate and is what makes the cross-ISA comparison mean
# anything: each host compiles for itself (SVE on Neoverse V1, SVE2 on V2,
# AVX-512 on Sapphire Rapids). The kernels' own vector paths are still gated on
# __ARM_NEON, so on x86 they fall back to the portable scalar forms in
# storm_simd.h -- which is itself the measurement, not a defect: it shows what
# the portable path costs on a machine with a dedicated popcount port.
set -euo pipefail
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=${OUT:-build_portable}
mkdir -p "$OUT"

ARCH_FLAGS="-march=native"
if ! echo 'int main(){}' | $CXX -x c++ $ARCH_FLAGS - -o /dev/null 2>/dev/null; then
  ARCH_FLAGS="-mcpu=native"
  echo "note: -march=native rejected, using -mcpu=native"
fi
CXXFLAGS="-O3 -std=c++17 $ARCH_FLAGS -I. -Ithird_party/croaring_amalg"
CFLAGS="-O3 -std=c11 $ARCH_FLAGS -Ithird_party/croaring_amalg"

echo "== $($CXX --version | head -1)"
echo "== flags: $CXXFLAGS"

[ -f "$OUT/roaring.o" ] || $CC $CFLAGS -c third_party/croaring_amalg/roaring.c -o "$OUT/roaring.o"

KSRC="kernels/storm_repr.cpp kernels/storm_gen.cpp kernels/oracle.cpp \
      kernels/cell_bb.cpp kernels/cell_bs.cpp kernels/cell_br.cpp \
      kernels/cell_sparse.cpp kernels/cell_wah.cpp kernels/storm_cost.cpp kernels/storm_allpairs.cpp kernels/cell_comp.cpp"

$CXX $CXXFLAGS $KSRC bench/bench_baseline.cpp "$OUT/roaring.o" -o "$OUT/bench_baseline"
$CXX $CXXFLAGS $KSRC bench/bench_cells.cpp                     -o "$OUT/bench_cells"
$CXX $CXXFLAGS $KSRC bench/bench_allpairs.cpp                  -o "$OUT/bench_allpairs"
$CXX $CXXFLAGS $KSRC bench/bench_occbin.cpp                    -o "$OUT/bench_occbin"
$CXX $CXXFLAGS $KSRC bench/bench_real.cpp                      -o "$OUT/bench_real"
$CXX $CXXFLAGS $KSRC tests/test_cells.cpp                      -o "$OUT/test_cells"
echo "built: $OUT/{bench_baseline,bench_cells,test_cells}"
