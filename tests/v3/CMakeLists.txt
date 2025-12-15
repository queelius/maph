# CMakeLists.txt for maph v3 comprehensive test suite
#
# This build configuration includes:
# - All v3 test files with Catch2 framework
# - Code coverage analysis with gcov/lcov
# - Performance benchmarks
# - Property-based testing
# - Integration testing with file I/O

cmake_minimum_required(VERSION 3.16)

# Ensure we have access to the parent project's configuration
if(NOT TARGET maph)
    message(FATAL_ERROR "maph target not found. This CMakeLists.txt should be included from the parent project.")
endif()

# ===== COVERAGE CONFIGURATION =====

# Enable coverage flags when building with coverage
option(ENABLE_COVERAGE "Enable code coverage analysis" OFF)

if(ENABLE_COVERAGE)
    message(STATUS "Enabling code coverage for v3 tests")

    # Check if we're using GCC or Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(COVERAGE_FLAGS "--coverage -fno-inline -fno-inline-small-functions -fno-default-inline -O0 -g")
        set(COVERAGE_LINK_FLAGS "--coverage")
    else()
        message(WARNING "Code coverage requires GCC or Clang")
        set(ENABLE_COVERAGE OFF)
    endif()
endif()

# ===== FETCH DEPENDENCIES =====

# Ensure Catch2 is available
if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# ===== COMPILER REQUIREMENTS =====

# V3 tests require C++20 for concepts and std::expected
set(V3_CXX_STANDARD 20)

# Check if the compiler supports C++20
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++20" COMPILER_SUPPORTS_CXX20)

if(NOT COMPILER_SUPPORTS_CXX20)
    message(FATAL_ERROR "maph v3 tests require C++20 support")
endif()

# ===== DEFINE TEST TARGETS =====

# Core types tests
add_executable(test_v3_core
    test_core.cpp
)

target_link_libraries(test_v3_core
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_core
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_core PRIVATE cxx_std_${V3_CXX_STANDARD})

# Hasher tests
add_executable(test_v3_hashers
    test_hashers.cpp
)

target_link_libraries(test_v3_hashers
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_hashers
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_hashers PRIVATE cxx_std_${V3_CXX_STANDARD})

# Storage tests
add_executable(test_v3_storage
    test_storage.cpp
)

target_link_libraries(test_v3_storage
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_storage
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_storage PRIVATE cxx_std_${V3_CXX_STANDARD})

# Table tests
add_executable(test_v3_table
    test_table.cpp
)

target_link_libraries(test_v3_table
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_table
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_table PRIVATE cxx_std_${V3_CXX_STANDARD})

# Integration tests
add_executable(test_v3_integration
    test_integration.cpp
)

target_link_libraries(test_v3_integration
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_integration
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_integration PRIVATE cxx_std_${V3_CXX_STANDARD})

# Property-based tests
add_executable(test_v3_properties
    test_properties.cpp
)

target_link_libraries(test_v3_properties
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_properties
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_properties PRIVATE cxx_std_${V3_CXX_STANDARD})

# Perfect hash tests
add_executable(test_v3_perfect_hash
    test_perfect_hash.cpp
)

target_link_libraries(test_v3_perfect_hash
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_perfect_hash
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_perfect_hash PRIVATE cxx_std_${V3_CXX_STANDARD})

# Extended perfect hash tests (TDD review)
add_executable(test_v3_perfect_hash_extended
    test_perfect_hash_extended.cpp
)

target_link_libraries(test_v3_perfect_hash_extended
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_perfect_hash_extended
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_perfect_hash_extended PRIVATE cxx_std_${V3_CXX_STANDARD})

# Comprehensive test suite (all tests in one executable)
add_executable(test_v3_comprehensive
    test_core.cpp
    test_hashers.cpp
    test_storage.cpp
    test_table.cpp
    test_integration.cpp
    test_properties.cpp
    test_perfect_hash.cpp
)

target_link_libraries(test_v3_comprehensive
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_comprehensive
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_comprehensive PRIVATE cxx_std_${V3_CXX_STANDARD})

# ===== APPLY COVERAGE FLAGS =====

if(ENABLE_COVERAGE)
    set(V3_TEST_TARGETS
        test_v3_core
        test_v3_hashers
        test_v3_storage
        test_v3_table
        test_v3_integration
        test_v3_properties
        test_v3_perfect_hash
        test_v3_comprehensive
    )

    foreach(target ${V3_TEST_TARGETS})
        target_compile_options(${target} PRIVATE ${COVERAGE_FLAGS})
        target_link_options(${target} PRIVATE ${COVERAGE_LINK_FLAGS})
    endforeach()

    # Also apply coverage to the main library when testing
    if(TARGET maph)
        target_compile_options(maph PRIVATE ${COVERAGE_FLAGS})
        target_link_options(maph PRIVATE ${COVERAGE_LINK_FLAGS})
    endif()
endif()

# ===== COMPILER WARNINGS =====

# Enable comprehensive warnings for test code
set(V3_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wcast-qual
    -Wformat=2
    -Wundef
    -Wshadow
    -Wcast-align
    -Wunused
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
)

# Apply warnings to GCC/Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target ${V3_TEST_TARGETS})
        target_compile_options(${target} PRIVATE ${V3_WARNING_FLAGS})
    endforeach()
endif()

# Additional GCC-specific warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GCC_EXTRA_FLAGS
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wrestrict
        -Wuseless-cast
    )

    foreach(target ${V3_TEST_TARGETS})
        target_compile_options(${target} PRIVATE ${GCC_EXTRA_FLAGS})
    endforeach()
endif()

# ===== OPTIMIZATION FOR TESTS =====

# Debug build for tests (better for debugging and coverage)
foreach(target ${V3_TEST_TARGETS})
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
endforeach()

# ===== CTEST INTEGRATION =====

# Enable CTest
include(CTest)

# Include Catch2 CTest integration if available
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
if(EXISTS ${catch2_SOURCE_DIR}/extras/Catch.cmake)
    include(Catch)

    # Register individual test executables
    catch_discover_tests(test_v3_core
        TEST_PREFIX "v3_core:"
        PROPERTIES TIMEOUT 300
    )

    catch_discover_tests(test_v3_hashers
        TEST_PREFIX "v3_hashers:"
        PROPERTIES TIMEOUT 300
    )

    catch_discover_tests(test_v3_storage
        TEST_PREFIX "v3_storage:"
        PROPERTIES TIMEOUT 300
    )

    catch_discover_tests(test_v3_table
        TEST_PREFIX "v3_table:"
        PROPERTIES TIMEOUT 300
    )

    catch_discover_tests(test_v3_integration
        TEST_PREFIX "v3_integration:"
        PROPERTIES TIMEOUT 600  # Integration tests may take longer
    )

    catch_discover_tests(test_v3_properties
        TEST_PREFIX "v3_properties:"
        PROPERTIES TIMEOUT 600  # Property tests may take longer
    )

    catch_discover_tests(test_v3_perfect_hash
        TEST_PREFIX "v3_perfect_hash:"
        PROPERTIES TIMEOUT 300
    )

    catch_discover_tests(test_v3_comprehensive
        TEST_PREFIX "v3_comprehensive:"
        PROPERTIES TIMEOUT 900  # Comprehensive tests take the longest
    )
else()
    # Fallback: Add tests manually
    add_test(NAME v3_core COMMAND test_v3_core)
    add_test(NAME v3_hashers COMMAND test_v3_hashers)
    add_test(NAME v3_storage COMMAND test_v3_storage)
    add_test(NAME v3_table COMMAND test_v3_table)
    add_test(NAME v3_integration COMMAND test_v3_integration)
    add_test(NAME v3_properties COMMAND test_v3_properties)
    add_test(NAME v3_perfect_hash COMMAND test_v3_perfect_hash)
    add_test(NAME v3_comprehensive COMMAND test_v3_comprehensive)

    # Set timeouts
    set_tests_properties(v3_core v3_hashers v3_storage v3_table v3_perfect_hash PROPERTIES TIMEOUT 300)
    set_tests_properties(v3_integration v3_properties PROPERTIES TIMEOUT 600)
    set_tests_properties(v3_comprehensive PROPERTIES TIMEOUT 900)
endif()

# ===== BENCHMARK TESTS =====

# Add custom test target for benchmarks (excluded from normal test runs)
add_test(NAME v3_benchmarks COMMAND test_v3_comprehensive [benchmark])
set_tests_properties(v3_benchmarks PROPERTIES
    TIMEOUT 1800  # 30 minutes for benchmarks
    LABELS "benchmark"
)

# ===== CUSTOM TARGETS =====

# Target to run all v3 tests
add_custom_target(test_v3_all
    COMMAND ${CMAKE_CTEST_COMMAND} -R "v3_" --output-on-failure
    DEPENDS ${V3_TEST_TARGETS}
    COMMENT "Running all maph v3 tests"
)

# Target to run tests excluding benchmarks
add_custom_target(test_v3_fast
    COMMAND ${CMAKE_CTEST_COMMAND} -R "v3_" -E "benchmark" --output-on-failure
    DEPENDS ${V3_TEST_TARGETS}
    COMMENT "Running fast maph v3 tests (excluding benchmarks)"
)

# Target to run only benchmarks
add_custom_target(test_v3_benchmarks
    COMMAND test_v3_comprehensive [benchmark]
    DEPENDS test_v3_comprehensive
    COMMENT "Running maph v3 performance benchmarks"
)

# ===== COVERAGE TARGETS =====

if(ENABLE_COVERAGE)
    # Find required tools
    find_program(GCOV_PATH gcov)
    find_program(LCOV_PATH lcov)
    find_program(GENHTML_PATH genhtml)

    if(GCOV_PATH AND LCOV_PATH AND GENHTML_PATH)
        # Coverage data directory
        set(COVERAGE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/coverage)
        set(COVERAGE_INFO_FILE ${COVERAGE_OUTPUT_DIR}/coverage.info)
        set(COVERAGE_HTML_DIR ${COVERAGE_OUTPUT_DIR}/html)

        # Target to generate coverage report
        add_custom_target(v3_coverage
            # Create output directory
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}

            # Initialize coverage counters
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters

            # Run comprehensive tests
            COMMAND test_v3_comprehensive

            # Capture coverage data
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --capture --output-file ${COVERAGE_INFO_FILE}

            # Filter out system headers and test files
            COMMAND ${LCOV_PATH} --remove ${COVERAGE_INFO_FILE}
                '*/usr/*'
                '*/tests/*'
                '*/test_*'
                '*/_deps/*'
                '*/catch2/*'
                --output-file ${COVERAGE_INFO_FILE}

            # Generate HTML report
            COMMAND ${GENHTML_PATH} ${COVERAGE_INFO_FILE} --output-directory ${COVERAGE_HTML_DIR}

            # Print summary
            COMMAND ${LCOV_PATH} --list ${COVERAGE_INFO_FILE}

            DEPENDS test_v3_comprehensive
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating v3 code coverage report"
        )

        # Target for quick coverage check (no HTML)
        add_custom_target(v3_coverage_check
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters
            COMMAND test_v3_comprehensive
            COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH} --directory ${CMAKE_BINARY_DIR} --capture --output-file ${COVERAGE_INFO_FILE}
            COMMAND ${LCOV_PATH} --remove ${COVERAGE_INFO_FILE} '*/usr/*' '*/tests/*' '*/test_*' '*/_deps/*' '*/catch2/*' --output-file ${COVERAGE_INFO_FILE}
            COMMAND ${LCOV_PATH} --list ${COVERAGE_INFO_FILE}
            DEPENDS test_v3_comprehensive
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Quick v3 coverage check"
        )

        message(STATUS "Coverage targets available: v3_coverage, v3_coverage_check")
        message(STATUS "Coverage report will be generated in: ${COVERAGE_HTML_DIR}")
    else()
        message(WARNING "Coverage tools not found. Install lcov and gcov for coverage analysis.")
    endif()
endif()

# ===== MEMORY CHECKING =====

# Add support for Valgrind memory checking
find_program(VALGRIND_PATH valgrind)

if(VALGRIND_PATH)
    # Valgrind options for comprehensive checking
    set(VALGRIND_OPTIONS
        --tool=memcheck
        --leak-check=full
        --show-leak-kinds=all
        --track-origins=yes
        --error-exitcode=1
        --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind.supp
    )

    # Target to run tests under Valgrind
    add_custom_target(test_v3_memcheck
        COMMAND ${VALGRIND_PATH} ${VALGRIND_OPTIONS} $<TARGET_FILE:test_v3_comprehensive>
        DEPENDS test_v3_comprehensive
        COMMENT "Running v3 tests under Valgrind"
    )

    message(STATUS "Valgrind target available: test_v3_memcheck")
else()
    message(STATUS "Valgrind not found. Memory checking not available.")
endif()

# ===== SANITIZERS =====

# Add compiler sanitizer options
option(ENABLE_SANITIZERS "Enable compiler sanitizers" OFF)

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_FLAGS
        -fsanitize=address
        -fsanitize=undefined
        -fsanitize=leak
        -fno-omit-frame-pointer
    )

    foreach(target ${V3_TEST_TARGETS})
        target_compile_options(${target} PRIVATE ${SANITIZER_FLAGS})
        target_link_options(${target} PRIVATE ${SANITIZER_FLAGS})
    endforeach()

    message(STATUS "Sanitizers enabled for v3 tests")
endif()

# ===== SUMMARY =====

message(STATUS "")
message(STATUS "=== maph v3 Test Configuration Summary ===")
message(STATUS "Test targets: ${V3_TEST_TARGETS}")
message(STATUS "C++ Standard: C++${V3_CXX_STANDARD}")
message(STATUS "Coverage enabled: ${ENABLE_COVERAGE}")
message(STATUS "Sanitizers enabled: ${ENABLE_SANITIZERS}")
message(STATUS "")
message(STATUS "Available targets:")
message(STATUS "  test_v3_all        - Run all v3 tests")
message(STATUS "  test_v3_fast       - Run tests excluding benchmarks")
message(STATUS "  test_v3_benchmarks - Run only benchmarks")
if(ENABLE_COVERAGE AND GCOV_PATH AND LCOV_PATH AND GENHTML_PATH)
    message(STATUS "  v3_coverage        - Generate coverage report with HTML")
    message(STATUS "  v3_coverage_check  - Quick coverage check")
endif()
if(VALGRIND_PATH)
    message(STATUS "  test_v3_memcheck   - Run tests under Valgrind")
endif()
message(STATUS "")