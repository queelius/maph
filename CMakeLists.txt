cmake_minimum_required(VERSION 3.14)
project(rd_ph_filter VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_CLI "Build command-line tool" ON)

# Include directories
include_directories(include)

# Create interface library for header-only library
add_library(rd_ph_filter INTERFACE)
target_include_directories(rd_ph_filter INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(rd_ph_filter INTERFACE cxx_std_17)

# Installation rules
include(GNUInstallDirs)
install(TARGETS rd_ph_filter
    EXPORT rd_ph_filterTargets
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT rd_ph_filterTargets
    FILE rd_ph_filterTargets.cmake
    NAMESPACE rd_ph_filter::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rd_ph_filter
)

# Create package configuration file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rd_ph_filterConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rd_ph_filterConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rd_ph_filterConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rd_ph_filter
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rd_ph_filterConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/rd_ph_filterConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rd_ph_filter
)

# Build tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build Python bindings
if(BUILD_PYTHON_BINDINGS)
    add_subdirectory(python)
endif()

# Build examples
if(BUILD_EXAMPLES)
    add_executable(approximate_map_examples examples/approximate_map_examples.cpp)
    target_link_libraries(approximate_map_examples PRIVATE rd_ph_filter)
    target_compile_features(approximate_map_examples PRIVATE cxx_std_17)
    
    add_executable(lazy_iterator_examples examples/lazy_iterator_examples.cpp)
    target_link_libraries(lazy_iterator_examples PRIVATE rd_ph_filter)
    target_compile_features(lazy_iterator_examples PRIVATE cxx_std_17)
endif()

# Build CLI tool
if(BUILD_CLI)
    add_executable(maph_cli src/maph_cli.cpp)
    target_link_libraries(maph_cli PRIVATE rd_ph_filter)
    target_compile_features(maph_cli PRIVATE cxx_std_17)
    set_target_properties(maph_cli PROPERTIES OUTPUT_NAME "maph")
    
    # Install CLI tool
    install(TARGETS maph_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Build documentation
if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()