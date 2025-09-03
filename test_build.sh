#!/bin/bash

# Quick build test script

set -e  # Exit on error

echo "Creating build directory..."
mkdir -p build
cd build

echo "Configuring with CMake..."
cmake .. \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_PYTHON_BINDINGS=OFF \
    -DBUILD_DOCS=OFF

echo "Building..."
make -j$(nproc)

echo "Running tests..."
ctest --verbose

echo "Build successful!"