# Find or fetch Catch2
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Maph comprehensive tests
add_executable(test_maph
    test_maph.cpp
)

target_link_libraries(test_maph
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

# Perfect hash comprehensive tests
add_executable(test_perfect_hash_comprehensive
    test_perfect_hash_comprehensive.cpp
)

target_link_libraries(test_perfect_hash_comprehensive
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

# Benchmark performance tests
add_executable(test_benchmark_performance
    test_benchmark_performance.cpp
)

target_link_libraries(test_benchmark_performance
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

# Ultra hash benchmarks
add_executable(bench_ultra_hash
    bench_ultra_hash.cpp
)

target_link_libraries(bench_ultra_hash
    PRIVATE
    maph
    pthread
)

# Enable OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(bench_ultra_hash PRIVATE OpenMP::OpenMP_CXX)
endif()

target_compile_options(bench_ultra_hash PRIVATE -march=native -O3 -mavx2)
target_include_directories(bench_ultra_hash PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(bench_ultra_hash PRIVATE cxx_std_17)

target_include_directories(test_maph PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_maph PRIVATE cxx_std_17)

target_include_directories(test_perfect_hash_comprehensive PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_perfect_hash_comprehensive PRIVATE cxx_std_17)

target_include_directories(test_benchmark_performance PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_benchmark_performance PRIVATE cxx_std_17)

# Enable coverage flags if building with coverage
if(CMAKE_CXX_FLAGS MATCHES ".*--coverage.*")
    target_compile_options(test_maph PRIVATE --coverage)
    target_link_options(test_maph PRIVATE --coverage)
    target_compile_options(test_perfect_hash_comprehensive PRIVATE --coverage)
    target_link_options(test_perfect_hash_comprehensive PRIVATE --coverage)
    target_compile_options(test_benchmark_performance PRIVATE --coverage)
    target_link_options(test_benchmark_performance PRIVATE --coverage)
endif()

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(test_maph)
catch_discover_tests(test_perfect_hash_comprehensive)
catch_discover_tests(test_benchmark_performance)

# Temporarily disable old tests until migration is complete
# add_executable(rd_ph_filter_tests
#     test_main.cpp
#     test_rd_ph_filter.cpp
#     # test_builder.cpp  # Commented out - uses old API
#     test_approximate_map.cpp
#     test_lazy_iterators.cpp
#     test_mock_ph.hpp
# )

# target_link_libraries(rd_ph_filter_tests
#     PRIVATE
#     rd_ph_filter
#     Catch2::Catch2WithMain
# )

# target_compile_features(rd_ph_filter_tests PRIVATE cxx_std_17)

# # Register tests with CTest
# include(CTest)
# include(Catch)
# catch_discover_tests(rd_ph_filter_tests)