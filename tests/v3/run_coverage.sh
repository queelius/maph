#!/bin/bash
# run_coverage.sh - Comprehensive coverage analysis for maph v3
#
# This script provides detailed code coverage analysis with multiple output formats
# and coverage thresholds. It's designed to be used in CI/CD pipelines and
# for local development.

set -euo pipefail

# ===== CONFIGURATION =====

# Default configuration
BUILD_DIR="${BUILD_DIR:-build}"
COVERAGE_DIR="${COVERAGE_DIR:-coverage}"
MIN_COVERAGE_PERCENT="${MIN_COVERAGE_PERCENT:-95}"
VERBOSE="${VERBOSE:-false}"
HTML_REPORT="${HTML_REPORT:-true}"
FAIL_ON_LOW_COVERAGE="${FAIL_ON_LOW_COVERAGE:-true}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ===== HELPER FUNCTIONS =====

log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}" >&2
}

warning() {
    echo -e "${YELLOW}[WARNING] $1${NC}" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS] $1${NC}"
}

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Generate comprehensive code coverage report for maph v3.

OPTIONS:
    -b, --build-dir DIR     Build directory (default: build)
    -c, --coverage-dir DIR  Coverage output directory (default: coverage)
    -m, --min-coverage PCT  Minimum coverage percentage (default: 95)
    -v, --verbose           Verbose output
    -h, --html              Generate HTML report (default: true)
    -f, --fail-on-low       Fail if coverage below minimum (default: true)
    --help                  Show this help message

ENVIRONMENT VARIABLES:
    BUILD_DIR              Build directory
    COVERAGE_DIR           Coverage output directory
    MIN_COVERAGE_PERCENT   Minimum coverage percentage
    VERBOSE                Enable verbose output (true/false)
    HTML_REPORT            Generate HTML report (true/false)
    FAIL_ON_LOW_COVERAGE   Fail on low coverage (true/false)

EXAMPLES:
    # Basic coverage analysis
    $0

    # Custom build directory with verbose output
    $0 -b my_build -v

    # Set minimum coverage to 90% and don't fail on low coverage
    $0 -m 90 --no-fail-on-low

    # Generate only text report (no HTML)
    $0 --no-html
EOF
}

check_dependencies() {
    local missing_deps=()

    # Check for required tools
    for tool in gcov lcov genhtml; do
        if ! command -v "$tool" &> /dev/null; then
            missing_deps+=("$tool")
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        error "Missing required dependencies: ${missing_deps[*]}"
        error "On Ubuntu/Debian: sudo apt-get install lcov"
        error "On CentOS/RHEL: sudo yum install lcov"
        error "On macOS: brew install lcov"
        exit 1
    fi
}

cleanup_coverage_data() {
    log "Cleaning up previous coverage data..."

    if [[ -d "$BUILD_DIR" ]]; then
        find "$BUILD_DIR" -name "*.gcda" -delete 2>/dev/null || true
        find "$BUILD_DIR" -name "*.gcno" -delete 2>/dev/null || true
    fi

    if [[ -d "$COVERAGE_DIR" ]]; then
        rm -rf "$COVERAGE_DIR"
    fi

    mkdir -p "$COVERAGE_DIR"
}

build_with_coverage() {
    log "Building maph v3 with coverage instrumentation..."

    if [[ ! -d "$BUILD_DIR" ]]; then
        mkdir -p "$BUILD_DIR"
    fi

    cd "$BUILD_DIR"

    # Configure with coverage
    cmake .. \
        -DENABLE_COVERAGE=ON \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="--coverage -fno-inline -fno-inline-small-functions -fno-default-inline -O0 -g" \
        -DCMAKE_C_FLAGS="--coverage -fno-inline -fno-inline-small-functions -fno-default-inline -O0 -g"

    # Build v3 tests
    make -j$(nproc) test_v3_comprehensive

    cd ..
}

run_tests() {
    log "Running comprehensive test suite..."

    cd "$BUILD_DIR"

    # Initialize coverage counters
    lcov --gcov-tool gcov --directory . --zerocounters

    # Run the comprehensive test suite
    if [[ "$VERBOSE" == "true" ]]; then
        ./tests/v3/test_v3_comprehensive -d yes
    else
        ./tests/v3/test_v3_comprehensive
    fi

    cd ..
}

generate_coverage_data() {
    log "Generating coverage data..."

    cd "$BUILD_DIR"

    # Capture coverage data
    lcov --gcov-tool gcov --directory . --capture --output-file "../$COVERAGE_DIR/coverage_raw.info"

    # Filter out system headers, test files, and dependencies
    lcov --remove "../$COVERAGE_DIR/coverage_raw.info" \
        '/usr/*' \
        '*/usr/*' \
        '*/tests/*' \
        '*/test_*' \
        '*/_deps/*' \
        '*/catch2/*' \
        '*/Catch2/*' \
        '*/build/*' \
        '*/CMakeFiles/*' \
        --output-file "../$COVERAGE_DIR/coverage_filtered.info"

    # Extract only v3 headers for focused analysis
    lcov --extract "../$COVERAGE_DIR/coverage_filtered.info" \
        '*/include/maph/v3/*' \
        --output-file "../$COVERAGE_DIR/coverage_v3.info"

    cd ..
}

generate_reports() {
    log "Generating coverage reports..."

    # Generate text summary
    lcov --list "$COVERAGE_DIR/coverage_v3.info" > "$COVERAGE_DIR/coverage_summary.txt"

    # Generate detailed text report
    cat > "$COVERAGE_DIR/coverage_detailed.txt" << EOF
maph v3 Code Coverage Report
Generated: $(date)
Build Directory: $BUILD_DIR
Minimum Required Coverage: $MIN_COVERAGE_PERCENT%

=== SUMMARY ===
EOF

    lcov --list "$COVERAGE_DIR/coverage_v3.info" >> "$COVERAGE_DIR/coverage_detailed.txt"

    # Generate HTML report if requested
    if [[ "$HTML_REPORT" == "true" ]]; then
        log "Generating HTML coverage report..."
        genhtml "$COVERAGE_DIR/coverage_v3.info" \
            --output-directory "$COVERAGE_DIR/html" \
            --title "maph v3 Code Coverage" \
            --show-details \
            --highlight \
            --legend \
            --sort \
            --demangle-cpp \
            --function-coverage \
            --branch-coverage
    fi
}

analyze_coverage() {
    log "Analyzing coverage results..."

    # Extract coverage percentage
    local coverage_line
    coverage_line=$(lcov --summary "$COVERAGE_DIR/coverage_v3.info" 2>/dev/null | grep "lines" | tail -n 1)

    if [[ -z "$coverage_line" ]]; then
        error "Could not extract coverage information"
        return 1
    fi

    # Parse coverage percentage
    local coverage_percent
    coverage_percent=$(echo "$coverage_line" | grep -o '[0-9.]*%' | head -n1 | sed 's/%//')

    if [[ -z "$coverage_percent" ]]; then
        error "Could not parse coverage percentage"
        return 1
    fi

    # Display results
    echo
    echo "=========================================="
    echo "     maph v3 Coverage Analysis Results"
    echo "=========================================="
    echo
    echo "Coverage Summary:"
    echo "$coverage_line"
    echo
    echo "Coverage Percentage: $coverage_percent%"
    echo "Minimum Required: $MIN_COVERAGE_PERCENT%"
    echo

    # Check if coverage meets minimum threshold
    if (( $(echo "$coverage_percent >= $MIN_COVERAGE_PERCENT" | bc -l) )); then
        success "Coverage meets minimum threshold! ✓"
        echo
        if [[ "$HTML_REPORT" == "true" ]]; then
            echo "Detailed HTML report: $COVERAGE_DIR/html/index.html"
        fi
        echo "Text reports:"
        echo "  Summary: $COVERAGE_DIR/coverage_summary.txt"
        echo "  Detailed: $COVERAGE_DIR/coverage_detailed.txt"
        echo
        return 0
    else
        local shortfall
        shortfall=$(echo "$MIN_COVERAGE_PERCENT - $coverage_percent" | bc -l)
        warning "Coverage below minimum threshold by $shortfall%"

        # Show uncovered lines for investigation
        echo
        echo "Uncovered areas requiring attention:"
        echo "====================================="
        lcov --list "$COVERAGE_DIR/coverage_v3.info" | grep -E "^/" | sort -k4 -n
        echo

        if [[ "$FAIL_ON_LOW_COVERAGE" == "true" ]]; then
            error "Coverage analysis failed!"
            return 1
        else
            warning "Continuing despite low coverage (FAIL_ON_LOW_COVERAGE=false)"
            return 0
        fi
    fi
}

perform_detailed_analysis() {
    log "Performing detailed coverage analysis..."

    # Function coverage analysis
    echo "Function Coverage Analysis:" >> "$COVERAGE_DIR/coverage_detailed.txt"
    echo "============================" >> "$COVERAGE_DIR/coverage_detailed.txt"
    lcov --list "$COVERAGE_DIR/coverage_v3.info" | grep -E "functions|Total" >> "$COVERAGE_DIR/coverage_detailed.txt"

    # Branch coverage analysis
    echo "" >> "$COVERAGE_DIR/coverage_detailed.txt"
    echo "Branch Coverage Analysis:" >> "$COVERAGE_DIR/coverage_detailed.txt"
    echo "=========================" >> "$COVERAGE_DIR/coverage_detailed.txt"
    lcov --list "$COVERAGE_DIR/coverage_v3.info" | grep -E "branches|Total" >> "$COVERAGE_DIR/coverage_detailed.txt"

    # Per-file coverage breakdown
    echo "" >> "$COVERAGE_DIR/coverage_detailed.txt"
    echo "Per-File Coverage Breakdown:" >> "$COVERAGE_DIR/coverage_detailed.txt"
    echo "============================" >> "$COVERAGE_DIR/coverage_detailed.txt"

    # Generate per-file statistics
    while IFS= read -r file; do
        if [[ -f "$file" ]]; then
            local file_coverage
            file_coverage=$(lcov --list "$COVERAGE_DIR/coverage_v3.info" | grep "$file" || echo "Not found")
            echo "$file_coverage" >> "$COVERAGE_DIR/coverage_detailed.txt"
        fi
    done < <(find include/maph/v3 -name "*.hpp" 2>/dev/null || true)
}

generate_ci_report() {
    log "Generating CI-friendly reports..."

    # Generate JSON report for CI consumption
    cat > "$COVERAGE_DIR/coverage.json" << EOF
{
    "timestamp": "$(date -Iseconds)",
    "build_directory": "$BUILD_DIR",
    "coverage_directory": "$COVERAGE_DIR",
    "minimum_required": $MIN_COVERAGE_PERCENT,
    "actual_coverage": $(lcov --summary "$COVERAGE_DIR/coverage_v3.info" 2>/dev/null | grep "lines" | tail -n 1 | grep -o '[0-9.]*' | head -n1),
    "status": "$(if (( $(echo "$(lcov --summary "$COVERAGE_DIR/coverage_v3.info" 2>/dev/null | grep "lines" | tail -n 1 | grep -o '[0-9.]*' | head -n1) >= $MIN_COVERAGE_PERCENT" | bc -l) )); then echo "PASS"; else echo "FAIL"; fi)",
    "reports": {
        "html": "$COVERAGE_DIR/html/index.html",
        "summary": "$COVERAGE_DIR/coverage_summary.txt",
        "detailed": "$COVERAGE_DIR/coverage_detailed.txt",
        "lcov": "$COVERAGE_DIR/coverage_v3.info"
    }
}
EOF

    # Generate GitHub Actions summary if in GitHub Actions
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        local coverage_percent
        coverage_percent=$(lcov --summary "$COVERAGE_DIR/coverage_v3.info" 2>/dev/null | grep "lines" | tail -n 1 | grep -o '[0-9.]*%' | head -n1 | sed 's/%//')

        cat >> "$GITHUB_STEP_SUMMARY" << EOF
## maph v3 Coverage Report

| Metric | Value |
|--------|-------|
| Coverage | $coverage_percent% |
| Minimum Required | $MIN_COVERAGE_PERCENT% |
| Status | $(if (( $(echo "$coverage_percent >= $MIN_COVERAGE_PERCENT" | bc -l) )); then echo "✅ PASS"; else echo "❌ FAIL"; fi) |

### Files
$(lcov --list "$COVERAGE_DIR/coverage_v3.info" | grep -E "^/" | head -20)
EOF
    fi
}

# ===== ARGUMENT PARSING =====

while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -c|--coverage-dir)
            COVERAGE_DIR="$2"
            shift 2
            ;;
        -m|--min-coverage)
            MIN_COVERAGE_PERCENT="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="true"
            shift
            ;;
        -h|--html)
            HTML_REPORT="true"
            shift
            ;;
        --no-html)
            HTML_REPORT="false"
            shift
            ;;
        -f|--fail-on-low)
            FAIL_ON_LOW_COVERAGE="true"
            shift
            ;;
        --no-fail-on-low)
            FAIL_ON_LOW_COVERAGE="false"
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# ===== MAIN EXECUTION =====

main() {
    log "Starting maph v3 coverage analysis..."
    log "Configuration:"
    log "  Build directory: $BUILD_DIR"
    log "  Coverage directory: $COVERAGE_DIR"
    log "  Minimum coverage: $MIN_COVERAGE_PERCENT%"
    log "  HTML report: $HTML_REPORT"
    log "  Fail on low coverage: $FAIL_ON_LOW_COVERAGE"
    echo

    # Check dependencies
    check_dependencies

    # Clean up previous data
    cleanup_coverage_data

    # Build with coverage
    build_with_coverage

    # Run tests
    run_tests

    # Generate coverage data
    generate_coverage_data

    # Generate reports
    generate_reports

    # Perform detailed analysis
    perform_detailed_analysis

    # Generate CI reports
    generate_ci_report

    # Analyze results and determine success/failure
    if analyze_coverage; then
        success "Coverage analysis completed successfully!"
        exit 0
    else
        error "Coverage analysis failed!"
        exit 1
    fi
}

# Ensure bc is available for floating point arithmetic
if ! command -v bc &> /dev/null; then
    error "bc (basic calculator) is required for coverage percentage calculations"
    error "On Ubuntu/Debian: sudo apt-get install bc"
    error "On CentOS/RHEL: sudo yum install bc"
    error "On macOS: brew install bc"
    exit 1
fi

# Run main function
main "$@"