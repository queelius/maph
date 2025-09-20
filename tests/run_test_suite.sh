#!/bin/bash
echo "=========================================="
echo "MAPH Perfect Hash Test Suite"
echo "=========================================="
echo

# Basic tests
echo "=== Basic Functionality Tests ==="
./tests/test_maph "[basic]" 2>&1 | grep -E "(All tests passed|test cases)"
./tests/test_maph "[storage]" 2>&1 | grep -E "(All tests passed|test cases)"
echo

# Perfect hash tests
echo "=== Perfect Hash Tests ==="
./tests/test_perfect_hash_comprehensive "[construction]" 2>&1 | grep -E "(All tests passed|test cases)"
./tests/test_perfect_hash_comprehensive "[dual_mode]" 2>&1 | grep -E "(All tests passed|test cases)"
./tests/test_perfect_hash_comprehensive "[json]" 2>&1 | grep -E "(All tests passed|test cases)"
echo

# Performance demonstration
echo "=== Performance Demonstration ==="
./tests/test_dual_mode 2>&1
echo

echo "Test suite completed!"
