# Benchmark suite for maph.
#
# Each benchmark target exists to compare implementations along
# concept-relevant axes (bits/key, query latency, build time, FPR).
# bench_harness.hpp provides shared measurement utilities.

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)

function(maph_add_benchmark name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE maph pthread)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(${name} PRIVATE cxx_std_23)
    target_compile_options(${name} PRIVATE -O3 -march=native)
    if(COMPILER_SUPPORTS_AVX2)
        target_compile_options(${name} PRIVATE -mavx2)
    endif()
endfunction()

# Core perfect_hash_function benchmark: all PHF algorithms, all key scales.
maph_add_benchmark(bench_phf)
