/**
 * @file speed_demo.cpp
 * @brief Demonstration of maph speed
 */

#include "maph/maph.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>

using namespace maph;
using namespace std::chrono;

void benchmark_single_thread() {
    std::cout << "=== Single Thread Performance ===\n";
    
    // Create maph with 1M slots
    auto m = Maph::create("bench.maph", 1000000);
    
    // Generate test data
    std::vector<std::pair<std::string, std::string>> data;
    for (int i = 0; i < 100000; ++i) {
        std::string key = "{\"id\":" + std::to_string(i) + "}";
        std::string val = "{\"score\":" + std::to_string(i * 10) + "}";
        data.push_back({key, val});
    }
    
    // Benchmark writes
    auto start = high_resolution_clock::now();
    for (const auto& [k, v] : data) {
        m->set(k, v);
    }
    auto end = high_resolution_clock::now();
    
    auto write_ns = duration_cast<nanoseconds>(end - start).count();
    double write_ops_per_sec = data.size() * 1e9 / write_ns;
    
    std::cout << "Writes: " << (write_ops_per_sec / 1e6) << " M ops/sec\n";
    std::cout << "  Latency: " << (write_ns / data.size()) << " ns/op\n";
    
    // Benchmark reads
    start = high_resolution_clock::now();
    for (int iter = 0; iter < 10; ++iter) {
        for (const auto& [k, v] : data) {
            volatile auto result = m->get(k);
        }
    }
    end = high_resolution_clock::now();
    
    auto read_ns = duration_cast<nanoseconds>(end - start).count();
    double read_ops_per_sec = (data.size() * 10) * 1e9 / read_ns;
    
    std::cout << "Reads: " << (read_ops_per_sec / 1e6) << " M ops/sec\n";
    std::cout << "  Latency: " << (read_ns / (data.size() * 10)) << " ns/op\n";
    
    m->close();
}

void benchmark_multi_reader() {
    std::cout << "\n=== Multi-Reader Performance ===\n";
    
    // Open same file from multiple threads
    const int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::atomic<uint64_t> total_ops{0};
    
    auto worker = [&total_ops]() {
        auto m = Maph::open("bench.maph", true);
        
        // Each thread does 1M reads
        for (int i = 0; i < 1000000; ++i) {
            std::string key = "{\"id\":" + std::to_string(i % 100000) + "}";
            volatile auto result = m->get(key);
            total_ops++;
        }
    };
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto ns = duration_cast<nanoseconds>(end - start).count();
    
    double ops_per_sec = total_ops * 1e9 / ns;
    
    std::cout << "Threads: " << num_threads << "\n";
    std::cout << "Total: " << (ops_per_sec / 1e6) << " M ops/sec\n";
    std::cout << "Per thread: " << (ops_per_sec / num_threads / 1e6) << " M ops/sec\n";
}

void demonstrate_mmap_sharing() {
    std::cout << "\n=== mmap Sharing (Parent/Child) ===\n";
    
    // Parent creates and writes
    auto m = Maph::create("shared.maph", 1000);
    m->set("{\"user\":\"parent\"}", "{\"data\":\"original\"}");
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process - can immediately access parent's data
        auto child_m = Maph::open("shared.maph");
        
        // Read parent's data
        std::string val = child_m->get("{\"user\":\"parent\"}");
        std::cout << "[Child] Read parent data: " << val << "\n";
        
        // Write child data
        child_m->set("{\"user\":\"child\"}", "{\"data\":\"from_child\"}");
        
        exit(0);
    } else {
        // Parent waits
        wait(nullptr);
        
        // Parent can see child's writes!
        std::string val = m->get("{\"user\":\"child\"}");
        std::cout << "[Parent] Read child data: " << val << "\n";
    }
}

int main() {
    std::cout << "maph Speed Demonstration\n";
    std::cout << "========================\n\n";
    
    benchmark_single_thread();
    benchmark_multi_reader();
    demonstrate_mmap_sharing();
    
    std::cout << "\nKey insights:\n";
    std::cout << "- Direct memory access (no syscalls after mmap)\n";
    std::cout << "- Lock-free reads scale linearly\n";
    std::cout << "- Zero-copy between processes\n";
    std::cout << "- Fixed slots = predictable performance\n";
    
    // Cleanup
    std::remove("bench.maph");
    std::remove("shared.maph");
    
    return 0;
}