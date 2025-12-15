/**
 * @file bench_maph.cpp
 * @brief Comprehensive benchmarks for maph performance
 */

#include <maph/maph.hpp>
#include <iostream>
#include <chrono>
#include <random>
#include <unordered_map>
#include <iomanip>
#include <sstream>

using namespace maph;
using namespace std::chrono;

class Benchmark {
private:
    std::vector<std::string> keys_;
    std::vector<std::string> values_;
    std::mt19937 rng_{42};
    
    std::string random_json(size_t size) {
        std::stringstream ss;
        ss << "{\"id\":" << (rng_() % 1000000)
           << ",\"data\":\"";
        for (size_t i = 0; i < size - 30; ++i) {
            ss << char('a' + (rng_() % 26));
        }
        ss << "\"}";
        return ss.str();
    }
    
    template<typename F>
    double measure_ns(F&& f, size_t iterations = 1) {
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < iterations; ++i) {
            f();
        }
        auto end = high_resolution_clock::now();
        return duration_cast<nanoseconds>(end - start).count() / double(iterations);
    }
    
    void print_result(const std::string& name, double ns, double baseline_ns = 0) {
        double ops_per_sec = 1e9 / ns;
        std::cout << std::setw(30) << std::left << name 
                  << std::setw(12) << std::right << std::fixed << std::setprecision(1) << ns << " ns"
                  << std::setw(15) << std::right << std::fixed << std::setprecision(2) 
                  << (ops_per_sec / 1e6) << " M ops/s";
        
        if (baseline_ns > 0) {
            double speedup = baseline_ns / ns;
            std::cout << std::setw(10) << std::right << std::fixed << std::setprecision(1) 
                      << speedup << "x";
        }
        std::cout << "\n";
    }
    
public:
    void setup(size_t count, size_t value_size = 100) {
        keys_.clear();
        values_.clear();
        keys_.reserve(count);
        values_.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            keys_.push_back("{\"id\":" + std::to_string(i) + "}");
            values_.push_back(random_json(value_size));
        }
    }
    
    void bench_single_ops(size_t count = 100000) {
        std::cout << "\n=== Single Operation Latency (n=" << count << ") ===\n";
        setup(count);
        
        // Baseline: std::unordered_map
        std::unordered_map<std::string, std::string> umap;
        for (size_t i = 0; i < count; ++i) {
            umap[keys_[i]] = values_[i];
        }
        
        double baseline_write = measure_ns([&]() {
            size_t idx = rng_() % count;
            umap[keys_[idx]] = values_[idx];
        }, 10000);
        
        double baseline_read = measure_ns([&]() {
            size_t idx = rng_() % count;
            volatile auto v = umap.find(keys_[idx]);
        }, 10000);
        
        // maph
        auto m = Maph::create("bench.maph", count * 2);
        for (size_t i = 0; i < count; ++i) {
            m->set(keys_[i], values_[i]);
        }
        
        double maph_write = measure_ns([&]() {
            size_t idx = rng_() % count;
            m->set(keys_[idx], values_[idx]);
        }, 10000);
        
        double maph_read = measure_ns([&]() {
            size_t idx = rng_() % count;
            volatile auto v = m->get(keys_[idx]);
        }, 10000);
        
        std::cout << "Operation                     Latency         Throughput     Speedup\n";
        std::cout << "----------------------------------------------------------------\n";
        print_result("unordered_map write", baseline_write);
        print_result("maph write", maph_write, baseline_write);
        print_result("unordered_map read", baseline_read);
        print_result("maph read", maph_read, baseline_read);
    }
    
    void bench_batch_ops(size_t count = 100000) {
        std::cout << "\n=== Batch Operations (n=" << count << ") ===\n";
        setup(count);
        
        auto m = Maph::create("bench_batch.maph", count * 2);
        
        // Build key-value pairs
        std::vector<std::pair<JsonView, JsonView>> kvs;
        for (size_t i = 0; i < count; ++i) {
            kvs.emplace_back(keys_[i], values_[i]);
        }
        
        // Sequential batch
        auto start = high_resolution_clock::now();
        m->mset(kvs);
        auto end = high_resolution_clock::now();
        double seq_time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double seq_ops_per_sec = (count / seq_time_ms) * 1000;
        
        // Clear and retry with parallel
        m = Maph::create("bench_batch2.maph", count * 2);
        
        // Parallel batch with different thread counts
        std::cout << "Threads    Time(ms)    Throughput    Speedup\n";
        std::cout << "----------------------------------------------\n";
        
        for (size_t threads : {1, 2, 4, 8, 16}) {
            if (threads > std::thread::hardware_concurrency()) break;
            
            m = Maph::create("bench_batch_t.maph", count * 2);
            
            start = high_resolution_clock::now();
            m->parallel_mset(kvs, threads);
            end = high_resolution_clock::now();
            
            double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
            double ops_per_sec = (count / time_ms) * 1000;
            double speedup = seq_time_ms / time_ms;
            
            std::cout << std::setw(7) << threads 
                      << std::setw(12) << std::fixed << std::setprecision(2) << time_ms
                      << std::setw(12) << std::fixed << std::setprecision(2) 
                      << (ops_per_sec / 1e6) << " M/s"
                      << std::setw(10) << std::fixed << std::setprecision(2) << speedup << "x\n";
        }
    }
    
    void bench_simd_hash(size_t count = 1000000) {
        std::cout << "\n=== SIMD Hash Performance (n=" << count << ") ===\n";
        setup(count);
        
        std::vector<JsonView> key_views;
        for (const auto& k : keys_) {
            key_views.push_back(k);
        }
        
        // Scalar hashing
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < count; ++i) {
            volatile auto h = Hash::compute(key_views[i], 1000000);
        }
        auto end = high_resolution_clock::now();
        double scalar_ns = duration_cast<nanoseconds>(end - start).count() / double(count);
        
#ifdef __AVX2__
        if (__builtin_cpu_supports("avx2")) {
            // SIMD batch hashing
            std::vector<Hash::Result> results;
            start = high_resolution_clock::now();
            Hash::compute_batch(key_views, 1000000, results);
            end = high_resolution_clock::now();
            double simd_ns = duration_cast<nanoseconds>(end - start).count() / double(count);
            
            std::cout << "Method           Time/Hash    Throughput     Speedup\n";
            std::cout << "------------------------------------------------------\n";
            print_result("Scalar FNV-1a", scalar_ns);
            print_result("SIMD AVX2", simd_ns, scalar_ns);
        } else {
            std::cout << "AVX2 not supported on this CPU\n";
            print_result("Scalar FNV-1a", scalar_ns);
        }
#else
        std::cout << "SIMD not compiled (needs -mavx2)\n";
        print_result("Scalar FNV-1a", scalar_ns);
#endif
    }
    
    void bench_memory_bandwidth(size_t count = 1000000) {
        std::cout << "\n=== Memory Bandwidth Test (n=" << count << ") ===\n";
        setup(count, 400);  // Near max slot size
        
        auto m = Maph::create("bench_mem.maph", count);
        
        // Write all data
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < count; ++i) {
            m->set(keys_[i], values_[i]);
        }
        auto end = high_resolution_clock::now();
        
        double write_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double write_mb = (count * 400) / (1024.0 * 1024.0);
        double write_bandwidth = write_mb / (write_ms / 1000.0);
        
        // Read all data
        start = high_resolution_clock::now();
        for (size_t i = 0; i < count; ++i) {
            volatile auto v = m->get(keys_[i]);
        }
        end = high_resolution_clock::now();
        
        double read_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double read_mb = (count * 400) / (1024.0 * 1024.0);
        double read_bandwidth = read_mb / (read_ms / 1000.0);
        
        std::cout << "Operation      Data(MB)    Time(ms)    Bandwidth(GB/s)\n";
        std::cout << "-------------------------------------------------------\n";
        std::cout << std::setw(14) << std::left << "Sequential Write"
                  << std::setw(12) << std::fixed << std::setprecision(1) << write_mb
                  << std::setw(12) << std::fixed << std::setprecision(2) << write_ms
                  << std::setw(12) << std::fixed << std::setprecision(2) << (write_bandwidth / 1024.0) << "\n";
        std::cout << std::setw(14) << std::left << "Sequential Read"
                  << std::setw(12) << std::fixed << std::setprecision(1) << read_mb
                  << std::setw(12) << std::fixed << std::setprecision(2) << read_ms
                  << std::setw(12) << std::fixed << std::setprecision(2) << (read_bandwidth / 1024.0) << "\n";
        
        // Random access pattern
        std::vector<size_t> indices(count);
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng_);
        
        start = high_resolution_clock::now();
        for (size_t idx : indices) {
            volatile auto v = m->get(keys_[idx]);
        }
        end = high_resolution_clock::now();
        
        double random_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double random_bandwidth = read_mb / (random_ms / 1000.0);
        
        std::cout << std::setw(14) << std::left << "Random Read"
                  << std::setw(12) << std::fixed << std::setprecision(1) << read_mb
                  << std::setw(12) << std::fixed << std::setprecision(2) << random_ms
                  << std::setw(12) << std::fixed << std::setprecision(2) << (random_bandwidth / 1024.0) << "\n";
    }
    
    void bench_scalability() {
        std::cout << "\n=== Scalability Test ===\n";
        std::cout << "Size        Build(ms)   Read(ns)   Write(ns)   Memory(MB)\n";
        std::cout << "----------------------------------------------------------\n";
        
        for (size_t size : {1000, 10000, 100000, 1000000}) {
            setup(size);
            
            auto start = high_resolution_clock::now();
            auto m = Maph::create("bench_scale.maph", size * 2);
            for (size_t i = 0; i < size; ++i) {
                m->set(keys_[i], values_[i]);
            }
            auto end = high_resolution_clock::now();
            double build_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
            
            double read_ns = measure_ns([&]() {
                size_t idx = rng_() % size;
                volatile auto v = m->get(keys_[idx]);
            }, 10000);
            
            double write_ns = measure_ns([&]() {
                size_t idx = rng_() % size;
                m->set(keys_[idx], values_[idx]);
            }, 10000);
            
            auto stats = m->stats();
            double memory_mb = stats.memory_bytes / (1024.0 * 1024.0);
            
            std::cout << std::setw(10) << size
                      << std::setw(12) << std::fixed << std::setprecision(1) << build_ms
                      << std::setw(11) << std::fixed << std::setprecision(1) << read_ns
                      << std::setw(12) << std::fixed << std::setprecision(1) << write_ns
                      << std::setw(12) << std::fixed << std::setprecision(1) << memory_mb << "\n";
        }
    }
};

int main() {
    std::cout << "MAPH Performance Benchmarks\n";
    std::cout << "CPU: " << std::thread::hardware_concurrency() << " cores\n";
    
#ifdef __AVX2__
    std::cout << "SIMD: AVX2 enabled\n";
#else
    std::cout << "SIMD: disabled (compile with -mavx2)\n";
#endif
    
    Benchmark bench;
    
    bench.bench_single_ops();
    bench.bench_batch_ops();
    bench.bench_simd_hash();
    bench.bench_memory_bandwidth();
    bench.bench_scalability();
    
    std::cout << "\nBenchmarks complete.\n";
    
    // Cleanup
    std::system("rm -f bench*.maph");
    
    return 0;
}