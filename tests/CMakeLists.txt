# Find or fetch Catch2
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Create test executable
add_executable(rd_ph_filter_tests
    test_main.cpp
    test_rd_ph_filter.cpp
    # test_builder.cpp  # Commented out - uses old API
    test_approximate_map.cpp
    test_lazy_iterators.cpp
    test_mock_ph.hpp
)

target_link_libraries(rd_ph_filter_tests
    PRIVATE
    rd_ph_filter
    Catch2::Catch2WithMain
)

target_compile_features(rd_ph_filter_tests PRIVATE cxx_std_17)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(rd_ph_filter_tests)