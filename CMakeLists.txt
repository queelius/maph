cmake_minimum_required(VERSION 3.14)
project(maph VERSION 2.0.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
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

# Header-only library
add_library(maph INTERFACE)
target_include_directories(maph INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# CLI executable
add_executable(maph_cli src/maph_cli.cpp)
target_link_libraries(maph_cli PRIVATE maph)
target_include_directories(maph_cli PRIVATE include)
set_target_properties(maph_cli PROPERTIES OUTPUT_NAME maph)

# Installation
install(TARGETS maph_cli DESTINATION bin)
install(FILES include/maph.hpp DESTINATION include)

# Tests
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    
    # Benchmark executable
    add_executable(bench_maph tests/bench_maph.cpp)
    target_link_libraries(bench_maph PRIVATE maph pthread)
    target_include_directories(bench_maph PRIVATE include)

    # Link OpenMP if found
    if(OpenMP_CXX_FOUND)
        target_link_libraries(bench_maph PRIVATE OpenMP::OpenMP_CXX)
    endif()

    # Enable AVX2 if available
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        target_compile_options(bench_maph PRIVATE -mavx2)
    endif()
endif()

# Integrations
option(BUILD_REST_API "Build REST API server" OFF)
if(BUILD_REST_API)
    add_subdirectory(integrations/rest_api)
endif()