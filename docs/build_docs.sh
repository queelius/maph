#!/bin/bash

# Build documentation script for maph project
# This script generates Doxygen documentation locally

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "========================================="
echo "Building maph Documentation"
echo "========================================="

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: Doxygen is not installed."
    echo "Please install it with: sudo apt-get install doxygen graphviz"
    exit 1
fi

# Create output directory
echo "Creating documentation directories..."
mkdir -p "$PROJECT_ROOT/docs/doxygen/html"
mkdir -p "$PROJECT_ROOT/docs/images"

# Generate Doxygen documentation
echo "Generating Doxygen documentation..."
cd "$PROJECT_ROOT"
doxygen Doxyfile

# Check if generation was successful
if [ -f "$PROJECT_ROOT/docs/doxygen/html/index.html" ]; then
    echo "✅ Documentation generated successfully!"
    echo ""
    echo "View documentation at:"
    echo "  file://$PROJECT_ROOT/docs/doxygen/html/index.html"
    echo ""
    echo "To serve locally:"
    echo "  cd $PROJECT_ROOT/docs/doxygen/html"
    echo "  python3 -m http.server 8000"
    echo "  Then open: http://localhost:8000"
    echo ""
    echo "To publish to GitHub Pages:"
    echo "  1. Push to main branch"
    echo "  2. Enable GitHub Pages in repository settings"
    echo "  3. Set source to 'GitHub Actions'"
    echo "  4. Documentation will be available at:"
    echo "     https://yourusername.github.io/rd_ph_filter/"
else
    echo "❌ Error: Documentation generation failed!"
    exit 1
fi