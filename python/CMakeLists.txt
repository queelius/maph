find_package(pybind11 REQUIRED)

pybind11_add_module(rd_ph_filter_py
    src/bindings.cpp
    src/ph_wrapper.hpp
)

pybind11_add_module(approximate_filters_py
    src/bindings_v2.cpp
    src/ph_wrapper.hpp
)

target_link_libraries(rd_ph_filter_py
    PRIVATE
    rd_ph_filter
)

target_link_libraries(approximate_filters_py
    PRIVATE
    rd_ph_filter
)

target_compile_features(rd_ph_filter_py PRIVATE cxx_std_17)
target_compile_features(approximate_filters_py PRIVATE cxx_std_17)

# Set proper module names
set_target_properties(rd_ph_filter_py PROPERTIES
    OUTPUT_NAME rd_ph_filter
)

set_target_properties(approximate_filters_py PROPERTIES
    OUTPUT_NAME approximate_filters
)

# Install the Python modules
install(TARGETS rd_ph_filter_py
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/site-packages
)