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

# Parameter sweep for PHOBIC bucket_size, RecSplit leaf_size, BBHash gamma.
maph_add_benchmark(bench_phf_sweep)

# membership_oracle benchmark: xor_filter, ribbon_filter at 8/16/32 bits.
maph_add_benchmark(bench_filter)

# approximate_map benchmark: perfect_filter<PHF, FPBits> variants.
maph_add_benchmark(bench_approximate_map)

# PHOBIC parallel build scaling: threads=1,2,4,8 across phobic3/4/5.
maph_add_benchmark(bench_phobic_parallel)

# Large-scale partitioned PHOBIC (1M, 10M keys).
maph_add_benchmark(bench_scale)

# Shard-count x thread-count sweep for partitioned_phf<phobic5>.
maph_add_benchmark(bench_partitioned_sweep)

# Compare inner PHF algorithms under partitioned_phf.
maph_add_benchmark(bench_partitioned_algos)

# Retrieval grid: ribbon vs phf_value_array across value widths.
maph_add_benchmark(bench_retrieval)

# Bloomier composition grid: retrieval x oracle.
maph_add_benchmark(bench_bloomier)
