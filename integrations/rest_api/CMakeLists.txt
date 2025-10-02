cmake_minimum_required(VERSION 3.14)
project(maph_rest_api VERSION 3.0.0 LANGUAGES CXX)

# C++23 required for v3
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies
find_package(Threads REQUIRED)

# Find libmicrohttpd
find_library(MHD_LIBRARY microhttpd)
if(NOT MHD_LIBRARY)
    message(WARNING "libmicrohttpd not found - REST API server will not be built")
    message(WARNING "Install with: sudo apt-get install libmicrohttpd-dev")
    return()
endif()

# Build v3 REST API server
add_executable(maph_server_v3 maph_server_v3.cpp)

target_include_directories(maph_server_v3 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(maph_server_v3 PRIVATE
    maph
    ${MHD_LIBRARY}
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Optimization flags
target_compile_options(maph_server_v3 PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto -DNDEBUG>
    $<$<CONFIG:Debug>:-g -O0 -fsanitize=address>
    -Wall -Wextra -Wpedantic
)

# Enable AVX2 if available
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(maph_server_v3 PRIVATE -mavx2)
endif()

# Installation
install(TARGETS maph_server_v3 DESTINATION bin)

# Documentation
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
    install(FILES README.md DESTINATION share/doc/maph)
endif()