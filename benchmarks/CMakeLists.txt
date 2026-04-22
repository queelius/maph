# Benchmark suite for maph.
#
# Each benchmark target is self-contained. Benchmarks exist to compare
# implementations against each other along concept-relevant axes (bits/key,
# query latency, build time, false positive rate).

add_library(benchmark_utils INTERFACE)
target_include_directories(benchmark_utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(benchmark_utils INTERFACE maph pthread)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)

function(maph_add_benchmark name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE benchmark_utils)
    target_compile_options(${name} PRIVATE -O3 -march=native)
    if(COMPILER_SUPPORTS_AVX2)
        target_compile_options(${name} PRIVATE -mavx2)
    endif()
endfunction()

# Perfect-hash-function comparison: all PHF implementations, with and without
# perfect_filter composition, across key counts.
maph_add_benchmark(bench_phobic)

# Cross-algorithm PHF comparison (predecessor benchmark; kept for historical
# numbers). bench_phobic is the canonical one.
maph_add_benchmark(bench_perfect_hash_compare)
