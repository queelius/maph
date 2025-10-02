cmake_minimum_required(VERSION 3.14)
project(maph VERSION 2.0.0 LANGUAGES CXX)

# C++23 required for v3 (concepts, std::expected)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find OpenMP
find_package(OpenMP)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address")

# Add OpenMP flags if available
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OpenMP_CXX_FLAGS}")
    message(STATUS "OpenMP enabled with flags: ${OpenMP_CXX_FLAGS}")
    add_compile_definitions(MAPH_HAS_OPENMP)
else()
    message(STATUS "OpenMP not found - parallel optimizations will be disabled")
endif()

# Header-only library (v3)
add_library(maph INTERFACE)
target_include_directories(maph INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(maph INTERFACE cxx_std_23)  # v3 requires C++23 for std::expected

# CLI executable (TODO: update to v3)
# add_executable(maph_cli src/maph_cli.cpp)
# target_link_libraries(maph_cli PRIVATE maph)
# target_include_directories(maph_cli PRIVATE include)
# set_target_properties(maph_cli PROPERTIES OUTPUT_NAME maph)

# Installation
# install(TARGETS maph_cli DESTINATION bin)
install(DIRECTORY include/maph/v3 DESTINATION include/maph)

# Tests (v3)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks (v3)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Examples (v3)
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    # V3 demo
    add_executable(v3_demo examples/v3_demo.cpp)
    target_link_libraries(v3_demo PRIVATE maph)
    target_include_directories(v3_demo PRIVATE include)

    # V3 simple test
    add_executable(v3_simple_test examples/v3_simple_test.cpp)
    target_link_libraries(v3_simple_test PRIVATE maph)
    target_include_directories(v3_simple_test PRIVATE include)
endif()

# Integrations
option(BUILD_REST_API "Build REST API server" OFF)
if(BUILD_REST_API)
    add_subdirectory(integrations/rest_api)
endif()