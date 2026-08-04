# CMake 4.x removed compatibility with < 3.5, so the previous `2.8` made the
# project unconfigurable on any current toolchain. 3.15 is the floor imposed by
# the vendored CRoaring.
cmake_minimum_required(VERSION 3.15)
project(StormBitmaps C CXX)
set (CMAKE_BUILD_TYPE Release)
set (CMAKE_VERBOSE_MAKEFILE on) # Debug verbosity

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if (POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif ()

# Language standards.
#
# The library is implemented in C++17 (storm.cpp) but exposes a pure C ABI via
# the extern "C" block in storm.h, so callers in C, Python/cffi, Rust, Go, Julia
# and R can bind it without a C++ toolchain. C++17 is for the implementation
# only -- it buys compile-time specialisation of the representation-pairing
# kernels (see PROBLEM_STATEMENT.md), which is macro soup in C.
#
# C11, not C99: CMAKE_C_STANDARD is inherited by add_subdirectory(), and
# CRoaring's reference counting needs C11 <stdatomic.h>. Under C99 its atomic
# detection falls through to "#error Unknown atomic implementation".
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_C_STANDARD 11)
set (CMAKE_C_STANDARD_REQUIRED ON)

option(STORM_ENABLE_SIMD_AVX512 "Enable AVX512 optimizations" OFF)
option(STORM_ENABLE_SIMD_AVX2 "Enable AVX2 optimizations" OFF)
option(STORM_ENABLE_SIMD_SSE4_2 "Enable SSE 4.2 optimizations" OFF)
option(STORM_DISABLE_NATIVE "Force disable native compilaton" OFF)

if(STORM_ENABLE_SIMD_AVX512)
	if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
		# `-mavx512` is not a valid GCC/Clang flag and failed the build outright.
		# AVX-512BW is what storm/libalgebra actually require; VPOPCNTDQ is
		# requested separately because it is a distinct feature bit.
		add_compile_options(-mavx512f -mavx512bw -mavx512vl -mavx512vpopcntdq)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
		add_compile_options(/QxAVX512)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		add_compile_options(/arch:AVX512)
	endif()
	message(STATUS "STORM: AVX2 instruction set")
elseif(STORM_ENABLE_SIMD_AVX2)
	if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
		add_compile_options(-mavx2)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
		add_compile_options(/QxAVX2)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		add_compile_options(/arch:AVX2)
	endif()
	message(STATUS "STORM: AVX2 instruction set")
elseif(STORM_ENABLE_SIMD_SSE4_2)
	if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
		add_compile_options(-msse4.2)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
		add_compile_options(/QxSSE4.2)
	elseif((CMAKE_CXX_COMPILER_ID MATCHES "MSVC") AND NOT CMAKE_CL_64)
		# VC doesn't support SSE4.2
    # See https://docs.microsoft.com/en-us/cpp/build/reference/arch-x64
    add_compile_options(/arch:AVX)
	endif()
	message(STATUS "STORM: SSE4.2 instruction set")
endif()

message(STATUS "CXX_FLAGS=${CMAKE_CXX_FLAGS}")
message(STATUS "C_FLAGS=${CMAKE_C_FLAGS}")

if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    SET(CMAKE_FIND_LIBRARY_PREFIXES "lib")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a" ".dylib")
    message(STATUS "Searching for .so and .a")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    SET(CMAKE_FIND_LIBRARY_PREFIXES "")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" ".dll")
    message(STATUS "Searching for .lib and .dll")
endif()

if(NOT MSVC)
if(NOT STORM_DISABLE_NATIVE)
add_compile_options(-march=native)
endif()
endif()

if (MSVC)
add_compile_options(/O2)
endif()

# ---------------------------------------------------------------- CRoaring
#
# Previously this was `find_path(... NAMES REQUIRED roaring/roaring.h)`, which
# both mis-uses the NAMES list and made CRoaring a hard requirement for the
# whole project -- including the library and tests, which do not use it. Only
# the benchmark needs Roaring, and it needs a *known* version for results to be
# comparable, so it is vendored as a submodule pinned to a release tag.
set(STORM_CROARING_DIR "${PROJECT_SOURCE_DIR}/third_party/CRoaring")

if(EXISTS "${STORM_CROARING_DIR}/CMakeLists.txt")
	set(ENABLE_ROARING_TESTS OFF CACHE BOOL "" FORCE)
	set(ROARING_BUILD_STATIC ON  CACHE BOOL "" FORCE)
	set(ROARING_USE_CPM      OFF CACHE BOOL "" FORCE)
	add_subdirectory("${STORM_CROARING_DIR}" EXCLUDE_FROM_ALL)
	set(STORM_HAVE_ROARING ON)
	message(STATUS "STORM: using vendored CRoaring (${STORM_CROARING_DIR})")
else()
	# Fall back to a system install if the submodule was not initialised.
	find_path(LM_ROARING_INCLUDE_DIR NAMES roaring/roaring.h)
	find_library(LM_ROARING_LIBRARY NAMES roaring libroaring)
	if(LM_ROARING_INCLUDE_DIR AND LM_ROARING_LIBRARY)
		set(STORM_HAVE_ROARING ON)
		message(STATUS "STORM: using system CRoaring (${LM_ROARING_LIBRARY})")
	else()
		set(STORM_HAVE_ROARING OFF)
		message(STATUS "STORM: CRoaring not found -- skipping 'benchmark'. "
		               "Run: git submodule update --init --recursive")
	endif()
endif()

# ---------------------------------------------------------------- targets
if(STORM_HAVE_ROARING)
	add_executable(benchmark storm.cpp benchmark.cpp)
	target_include_directories(benchmark PUBLIC "${PROJECT_SOURCE_DIR}")
	if(TARGET roaring)
		target_link_libraries(benchmark PUBLIC roaring)
	else()
		target_link_libraries(benchmark PUBLIC ${LM_ROARING_LIBRARY})
		target_include_directories(benchmark PUBLIC ${LM_ROARING_INCLUDE_DIR})
	endif()
endif()

# ---------------------------------------------------------------- kernels
#
# The pairing-matrix kernel layer: representations, generators, and one
# translation unit per group of cells. This is C++17-internal (PROBLEM_STATEMENT.md,
# AGENTS.md "Language and ABI") and is NOT exposed through storm.h -- it has no
# C ABI surface and is not meant to have one yet.
set(STORM_KERNEL_SOURCES
	kernels/storm_repr.cpp
	kernels/storm_gen.cpp
	kernels/oracle.cpp
	kernels/cell_bb.cpp
	kernels/cell_bs.cpp
	kernels/cell_br.cpp
	kernels/cell_sparse.cpp
	kernels/cell_wah.cpp
	kernels/storm_cost.cpp
	kernels/storm_allpairs.cpp
	kernels/cell_comp.cpp)

add_library(storm_kernels STATIC ${STORM_KERNEL_SOURCES})
target_include_directories(storm_kernels PUBLIC "${PROJECT_SOURCE_DIR}")

# Per-cell microbenchmark. Races every variant of a cell over one corpus.
add_executable(bench_cells bench/bench_cells.cpp)
target_link_libraries(bench_cells PUBLIC storm_kernels)

# Correctness tests. No third-party dependencies: these must always build.
#
# test_storm.c is deliberately compiled as C, not C++. Linking a C translation
# unit against the C++-compiled library is the ABI regression test: if the
# extern "C" boundary in storm.h ever breaks, this target fails to link.
enable_testing()
add_executable(test_storm storm.cpp tests/test_storm.c)
target_include_directories(test_storm PUBLIC "${PROJECT_SOURCE_DIR}")
set_target_properties(test_storm PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME storm_correctness COMMAND test_storm)

# Differential test for the kernel layer: every variant of every cell against
# the oracle, across representations, structures and spectra. Standing rule 6.
add_executable(test_cells tests/test_cells.cpp)
target_link_libraries(test_cells PUBLIC storm_kernels)
add_test(NAME cells_correctness COMMAND test_cells)
# M4 cost-model validation: oracle regret and selection overhead (Gate 1).
add_executable(bench_select bench/bench_select.cpp)
target_link_libraries(bench_select PUBLIC storm_kernels)
