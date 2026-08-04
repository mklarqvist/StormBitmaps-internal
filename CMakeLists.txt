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

macro(use_cxx11)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
    endif ()
  else ()
    set (CMAKE_CXX_STANDARD 11)
    # C11, not C99: CMAKE_C_STANDARD is inherited by add_subdirectory(), and
    # CRoaring's reference counting needs C11 <stdatomic.h>. Under C99 its
    # atomic detection falls through to "#error Unknown atomic implementation".
    # storm.c is plain C99 and builds unchanged as C11.
    set (CMAKE_C_STANDARD 11)
  endif ()
endmacro(use_cxx11)

use_cxx11()

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
	add_executable(benchmark storm.c benchmark.cpp)
	target_include_directories(benchmark PUBLIC "${PROJECT_SOURCE_DIR}")
	if(TARGET roaring)
		target_link_libraries(benchmark PUBLIC roaring)
	else()
		target_link_libraries(benchmark PUBLIC ${LM_ROARING_LIBRARY})
		target_include_directories(benchmark PUBLIC ${LM_ROARING_INCLUDE_DIR})
	endif()
endif()

# Correctness tests. No third-party dependencies: these must always build.
enable_testing()
add_executable(test_storm storm.c tests/test_storm.c)
target_include_directories(test_storm PUBLIC "${PROJECT_SOURCE_DIR}")
add_test(NAME storm_correctness COMMAND test_storm)