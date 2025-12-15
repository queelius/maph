# Benchmark suite for maph v3

# Common benchmark utilities library
add_library(benchmark_utils INTERFACE)
target_include_directories(benchmark_utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(benchmark_utils INTERFACE maph pthread)

# Single-threaded latency benchmark
add_executable(bench_latency bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE benchmark_utils)
target_compile_options(bench_latency PRIVATE -O3 -march=native)

# Multi-threaded throughput benchmark
add_executable(bench_throughput bench_throughput.cpp)
target_link_libraries(bench_throughput PRIVATE benchmark_utils)
target_compile_options(bench_throughput PRIVATE -O3 -march=native)

# YCSB workload benchmark
add_executable(bench_ycsb bench_ycsb.cpp)
target_link_libraries(bench_ycsb PRIVATE benchmark_utils)
target_compile_options(bench_ycsb PRIVATE -O3 -march=native)

# Comparison benchmark (maph vs std::unordered_map)
add_executable(bench_comparison bench_comparison.cpp)
target_link_libraries(bench_comparison PRIVATE benchmark_utils)
target_compile_options(bench_comparison PRIVATE -O3 -march=native)

# Perfect hash optimization benchmark
add_executable(bench_perfect_hash bench_perfect_hash.cpp)
target_link_libraries(bench_perfect_hash PRIVATE benchmark_utils)
target_compile_options(bench_perfect_hash PRIVATE -O3 -march=native)

# Master benchmark runner
add_executable(bench_all bench_all.cpp)
target_link_libraries(bench_all PRIVATE benchmark_utils)
target_compile_options(bench_all PRIVATE -O3 -march=native)

# Perfect hash comparison benchmark
add_executable(bench_perfect_hash_compare bench_perfect_hash_compare.cpp)
target_link_libraries(bench_perfect_hash_compare PRIVATE benchmark_utils)
target_compile_options(bench_perfect_hash_compare PRIVATE -O3 -march=native)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(bench_throughput PRIVATE OpenMP::OpenMP_CXX)
    target_link_libraries(bench_all PRIVATE OpenMP::OpenMP_CXX)
endif()

# Enable AVX2 if available
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(bench_latency PRIVATE -mavx2)
    target_compile_options(bench_throughput PRIVATE -mavx2)
    target_compile_options(bench_ycsb PRIVATE -mavx2)
    target_compile_options(bench_comparison PRIVATE -mavx2)
    target_compile_options(bench_perfect_hash PRIVATE -mavx2)
    target_compile_options(bench_all PRIVATE -mavx2)
endif()
