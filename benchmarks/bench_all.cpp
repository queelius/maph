/**
 * @file bench_all.cpp
 * @brief Master benchmark runner - runs all benchmarks
 */

#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    std::cout << "=== maph v3 Complete Benchmark Suite ===\n\n";

    // Parse arguments
    size_t num_keys = 100000;
    size_t num_queries = 100000;

    if (argc > 1) num_keys = std::stoull(argv[1]);
    if (argc > 2) num_queries = std::stoull(argv[2]);

    std::cout << "Configuration:\n";
    std::cout << "  Keys:    " << num_keys << "\n";
    std::cout << "  Queries: " << num_queries << "\n\n";

    // Run latency benchmark
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Running Latency Benchmark\n";
    std::cout << std::string(60, '=') << "\n\n";
    std::system((std::string("./bench_latency ") +
                std::to_string(num_keys) + " " +
                std::to_string(num_queries)).c_str());

    // Run throughput benchmark
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Running Throughput Benchmark\n";
    std::cout << std::string(60, '=') << "\n\n";
    std::system((std::string("./bench_throughput ") +
                std::to_string(num_keys) + " " +
                std::to_string(num_queries)).c_str());

    // Run comparison benchmark
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Running Comparison Benchmark\n";
    std::cout << std::string(60, '=') << "\n\n";
    std::system((std::string("./bench_comparison ") +
                std::to_string(num_keys) + " " +
                std::to_string(num_queries)).c_str());

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "All benchmarks complete!\n";
    std::cout << std::string(60, '=') << "\n";

    return 0;
}
