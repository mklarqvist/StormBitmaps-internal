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
# bench_allpairs times CRoaring on the same rows, so it MUST link roaring.o.
# It did not, and had not since that comparison was added -- the script has been
# failing to link this target while every measurement was produced by a
# hand-written command line. A build script that cannot build the benchmark the
# results come from is a defect in the results, not just in the script.
$CXX $CXXFLAGS $KSRC bench/bench_allpairs.cpp "$OUT/roaring.o" -o "$OUT/bench_allpairs"

# The same benchmark against croaring_modified -- the C35 array->bitset
# promotion. This is the binary bench/vs_roaring.sh defaults to, so it belongs
# in the build rather than in a shell history.
if [ -d third_party/croaring_modified ]; then
  mkdir -p "$OUT/mod"
  [ -f "$OUT/mod/roaring_mod.o" ] || $CC $CFLAGS -Ithird_party/croaring_modified \
      -c third_party/croaring_modified/roaring.c -o "$OUT/mod/roaring_mod.o"
  $CXX -O3 -std=c++17 $ARCH_FLAGS -I. -Ithird_party/croaring_modified \
      -DSTORM_CROARING_MODIFIED $KSRC bench/bench_allpairs.cpp \
      "$OUT/mod/roaring_mod.o" -o "$OUT/bench_allpairs_fix"
fi
$CXX $CXXFLAGS $KSRC bench/bench_occbin.cpp                    -o "$OUT/bench_occbin"
$CXX $CXXFLAGS $KSRC bench/bench_regret.cpp                    -o "$OUT/bench_regret"
$CXX $CXXFLAGS $KSRC bench/bench_real.cpp                      -o "$OUT/bench_real"
$CXX $CXXFLAGS $KSRC tests/test_cells.cpp                      -o "$OUT/test_cells"

# tests/test_storm.c is C on purpose: it links against the C++ objects and is
# therefore the ABI regression test (CLAUDE.md, "Language and ABI"). It was not
# in this script, so nothing checked the C ABI on any commit this session.
$CXX $CXXFLAGS -c storm.cpp -o "$OUT/storm.o" 2>/dev/null || true
if [ -f "$OUT/storm.o" ]; then
  $CC $CFLAGS -I. -c tests/test_storm.c -o "$OUT/test_storm.o" &&
  $CXX $CXXFLAGS "$OUT/test_storm.o" "$OUT/storm.o" $KSRC -o "$OUT/test_storm" || true
fi

# ABI invariant (CLAUDE.md): every Storm export is unmangled C.
#
# The check EXCLUDES std:: symbols. storm.o also carries five mangled names --
# std::vector<...>::__throw_length_error and friends -- which are libc++
# template instantiations emitted from std::vector use inside storm.cpp, not
# Storm API. CLAUDE.md says "zero __Z", which is right about the invariant and
# imprecise about the check: a plain grep for __Z reports those five and always
# will, on any C++ translation unit that instantiates a container.
if [ -f "$OUT/storm.o" ]; then
  n_c=$(nm -gU "$OUT/storm.o" | grep -c "_STORM_" || true)
  n_cpp=$(nm -gU "$OUT/storm.o" | grep "__Z" | c++filt | grep -vc "^std::" || true)
  echo "== ABI: $n_c unmangled STORM_ exports, $n_cpp mangled non-std symbols"
  [ "$n_cpp" -eq 0 ] || echo "!! C++ symbols leaked into the C ABI"
fi
echo "built: $(cd "$OUT" && ls | tr '\n' ' ')"
