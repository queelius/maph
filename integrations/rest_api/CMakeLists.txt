cmake_minimum_required(VERSION 3.14)
project(maph_rest_api VERSION 1.0.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies
find_package(Threads REQUIRED)

# Build REST API server
add_executable(maph_rest_server maph_server_simple.cpp)

target_include_directories(maph_rest_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(maph_rest_server PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Optimization flags
target_compile_options(maph_rest_server PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto -DNDEBUG>
    $<$<CONFIG:Debug>:-g -O0 -fsanitize=address>
    -Wall -Wextra -Wpedantic
)

# Enable AVX2 if available
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(maph_rest_server PRIVATE -mavx2)
endif()

# Installation
install(TARGETS maph_rest_server DESTINATION bin)
install(FILES index.html DESTINATION share/maph/web)

# Copy index.html to build directory for convenience
configure_file(index.html ${CMAKE_CURRENT_BINARY_DIR}/index.html COPYONLY)

# Documentation
install(FILES README.md DESTINATION share/doc/maph)