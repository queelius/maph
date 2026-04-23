# Test targets for maph.
#
# Each header has its own test file. test_all aggregates them into a single
# executable for fast full-suite runs and coverage collection.

cmake_minimum_required(VERSION 3.16)

if(NOT TARGET maph)
    message(FATAL_ERROR "maph target not found. Include this from the parent project.")
endif()

if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# ===== COVERAGE / SANITIZERS =====

option(ENABLE_COVERAGE "Enable code coverage analysis" OFF)
option(ENABLE_SANITIZERS "Enable ASan + UBSan + LSan" OFF)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(COVERAGE_FLAGS "--coverage" "-fno-inline" "-fno-default-inline" "-O0" "-g")
    set(COVERAGE_LINK_FLAGS "--coverage")
endif()

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_FLAGS -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-omit-frame-pointer)
endif()

# ===== TEST TARGETS =====

set(MAPH_TEST_SOURCES
    test_core.cpp
    test_phf_concept.cpp
    test_phobic.cpp
    test_perfect_hash.cpp
    test_perfect_hash_extended.cpp
    test_perfect_filter.cpp
    test_membership.cpp
    test_partitioned.cpp
    test_retrieval.cpp
    test_encoded_retrieval.cpp
    test_binary_fuse.cpp
    test_padded_phf.cpp
    test_shock_hash.cpp
)

set(MAPH_TEST_TARGETS "")
foreach(src ${MAPH_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)  # e.g. test_core
    string(REPLACE "test_" "test_v3_" target ${name})  # test_v3_core

    add_executable(${target} ${src})
    target_link_libraries(${target} PRIVATE maph Catch2::Catch2WithMain pthread)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wnull-dereference -Wimplicit-fallthrough
    )

    if(COVERAGE_FLAGS)
        target_compile_options(${target} PRIVATE ${COVERAGE_FLAGS})
        target_link_options(${target} PRIVATE ${COVERAGE_LINK_FLAGS})
    endif()
    if(SANITIZER_FLAGS)
        target_compile_options(${target} PRIVATE ${SANITIZER_FLAGS})
        target_link_options(${target} PRIVATE ${SANITIZER_FLAGS})
    endif()

    list(APPEND MAPH_TEST_TARGETS ${target})
endforeach()

# Aggregated target: compile all test sources into one executable. Useful for
# coverage, single-binary runs, and umbrella test discovery.
add_executable(test_v3_all_in_one ${MAPH_TEST_SOURCES})
target_link_libraries(test_v3_all_in_one PRIVATE maph Catch2::Catch2WithMain pthread)
target_compile_features(test_v3_all_in_one PRIVATE cxx_std_23)
if(COVERAGE_FLAGS)
    target_compile_options(test_v3_all_in_one PRIVATE ${COVERAGE_FLAGS})
    target_link_options(test_v3_all_in_one PRIVATE ${COVERAGE_LINK_FLAGS})
endif()
if(SANITIZER_FLAGS)
    target_compile_options(test_v3_all_in_one PRIVATE ${SANITIZER_FLAGS})
    target_link_options(test_v3_all_in_one PRIVATE ${SANITIZER_FLAGS})
endif()

# ===== CTEST REGISTRATION =====

include(CTest)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
if(EXISTS ${catch2_SOURCE_DIR}/extras/Catch.cmake)
    include(Catch)
    foreach(target ${MAPH_TEST_TARGETS})
        string(REPLACE "test_v3_" "v3_" prefix ${target})
        catch_discover_tests(${target}
            TEST_PREFIX "${prefix}:"
            PROPERTIES TIMEOUT 300
        )
    endforeach()
else()
    foreach(target ${MAPH_TEST_TARGETS})
        string(REPLACE "test_v3_" "v3_" name ${target})
        add_test(NAME ${name} COMMAND ${target})
        set_tests_properties(${name} PROPERTIES TIMEOUT 300)
    endforeach()
endif()

# ===== COVERAGE TARGET =====

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(GCOV_PATH gcov)
    find_program(LCOV_PATH lcov)
    find_program(GENHTML_PATH genhtml)

    if(GCOV_PATH AND LCOV_PATH AND GENHTML_PATH)
        set(COVERAGE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/coverage)
        set(COVERAGE_INFO_FILE ${COVERAGE_OUTPUT_DIR}/coverage.info)
        set(COVERAGE_HTML_DIR ${COVERAGE_OUTPUT_DIR}/html)

        add_custom_target(v3_coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters
            COMMAND test_v3_all_in_one
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --capture --output-file ${COVERAGE_INFO_FILE}
            COMMAND ${LCOV_PATH} --remove ${COVERAGE_INFO_FILE}
                '*/usr/*' '*/tests/*' '*/test_*' '*/_deps/*' '*/catch2/*'
                --output-file ${COVERAGE_INFO_FILE}
            COMMAND ${GENHTML_PATH} ${COVERAGE_INFO_FILE} --output-directory ${COVERAGE_HTML_DIR}
            COMMAND ${LCOV_PATH} --list ${COVERAGE_INFO_FILE}
            DEPENDS test_v3_all_in_one
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating maph coverage report in ${COVERAGE_HTML_DIR}"
        )
    else()
        message(WARNING "Coverage tools not found. Install lcov and gcov for coverage analysis.")
    endif()
endif()
