/**
 * @file test_benchmark_performance.cpp
 * @brief Performance benchmarks comparing FNV+probe vs Perfect Hash
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "maph.hpp"
#include <filesystem>
#include <random>
#include <chrono>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

using namespace maph;
namespace fs = std::filesystem;

class BenchmarkFixture {
protected:
    std::string test_file;
    std::unique_ptr<Maph> db;
    std::vector<std::string> keys;
    std::vector<std::string> values;
    
    BenchmarkFixture() 
        : test_file("/tmp/bench_maph_" + std::to_string(std::random_device{}()) + ".maph") {}
    
    ~BenchmarkFixture() {
        cleanup();
    }
    
    void cleanup() {
        db.reset();
        if (fs::exists(test_file)) fs::remove(test_file);
        if (fs::exists(test_file + ".journal")) fs::remove(test_file + ".journal");
    }
    
    void prepare_dataset(size_t count) {
        keys.clear();
        values.clear();
        keys.reserve(count);
        values.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            keys.push_back("{\"id\":" + std::to_string(i) + ",\"type\":\"record\"}");
            values.push_back("{\"data\":\"value_" + std::to_string(i) + 
                           "\",\"timestamp\":" + std::to_string(1700000000 + i) + "}");
        }
    }
    
    void populate_database() {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!db->set(keys[i], values[i])) {
                throw std::runtime_error("Failed to insert key at index " + std::to_string(i));
            }
        }
    }
};

TEST_CASE_METHOD(BenchmarkFixture, "Benchmark: Lookup Performance Comparison", "[benchmark][performance]") {
    const size_t DATASET_SIZE = 10000;
    const size_t LOOKUP_COUNT = 1000;
    
    db = Maph::create(test_file, DATASET_SIZE * 2);
    REQUIRE(db != nullptr);
    
    prepare_dataset(DATASET_SIZE);
    populate_database();
    
    // Prepare random lookup indices
    std::vector<size_t> lookup_indices;
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, DATASET_SIZE - 1);
    for (size_t i = 0; i < LOOKUP_COUNT; ++i) {
        lookup_indices.push_back(dist(rng));
    }
    
    SECTION("Standard Hash (FNV + Linear Probing)") {
        BENCHMARK("Lookup - Standard Hash") {
            for (size_t idx : lookup_indices) {
                auto val = db->get(keys[idx]);
                if (!val.has_value()) return -1;
            }
            return 0;
        };
    }
    
    // Now optimize the database
    auto opt_result = db->optimize();
    REQUIRE(opt_result.ok());
    
    SECTION("Perfect Hash (After Optimization)") {
        BENCHMARK("Lookup - Perfect Hash") {
            for (size_t idx : lookup_indices) {
                auto val = db->get(keys[idx]);
                if (!val.has_value()) return -1;
            }
            return 0;
        };
    }
}

TEST_CASE_METHOD(BenchmarkFixture, "Benchmark: Insert Performance", "[benchmark][performance]") {
    const size_t INSERT_COUNT = 5000;
    
    SECTION("Sequential Inserts") {
        db = Maph::create(test_file, INSERT_COUNT * 2);
        REQUIRE(db != nullptr);
        prepare_dataset(INSERT_COUNT);
        
        BENCHMARK("Sequential Insert") {
            for (size_t i = 0; i < 100; ++i) {
                db->set(keys[i % INSERT_COUNT], values[i % INSERT_COUNT]);
            }
        };
    }
    
    SECTION("Random Inserts") {
        db = Maph::create(test_file, INSERT_COUNT * 2);
        REQUIRE(db != nullptr);
        prepare_dataset(INSERT_COUNT);
        
        std::mt19937 rng(42);
        std::vector<size_t> random_indices;
        for (size_t i = 0; i < INSERT_COUNT; ++i) {
            random_indices.push_back(i);
        }
        std::shuffle(random_indices.begin(), random_indices.end(), rng);
        
        BENCHMARK("Random Insert") {
            for (size_t i = 0; i < 100; ++i) {
                size_t idx = random_indices[i % INSERT_COUNT];
                db->set(keys[idx], values[idx]);
            }
        };
    }
}

TEST_CASE_METHOD(BenchmarkFixture, "Benchmark: Batch Operations", "[benchmark][performance]") {
    const size_t DATASET_SIZE = 10000;
    const size_t BATCH_SIZE = 1000;
    
    db = Maph::create(test_file, DATASET_SIZE * 2);
    REQUIRE(db != nullptr);
    
    prepare_dataset(DATASET_SIZE);
    populate_database();
    
    // Prepare batch
    std::vector<std::pair<JsonView, JsonView>> batch_kvs;
    for (size_t i = 0; i < BATCH_SIZE; ++i) {
        batch_kvs.emplace_back(keys[i], values[i]);
    }
    
    SECTION("Batch Set - Standard Hash") {
        BENCHMARK("Batch Set (1000 items)") {
            return db->mset(batch_kvs);
        };
    }
    
    db->optimize();
    
    SECTION("Batch Set - After Optimization") {
        BENCHMARK("Batch Set After Opt (1000 items)") {
            return db->mset(batch_kvs);
        };
    }
    
    SECTION("Batch Get") {
        std::vector<JsonView> batch_keys;
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            batch_keys.push_back(keys[i]);
        }
        
        size_t found_count = 0;
        BENCHMARK("Batch Get (1000 items)") {
            db->mget(batch_keys, [&found_count](JsonView key, JsonView value) {
                found_count++;
            });
            return found_count;
        };
    }
}

TEST_CASE_METHOD(BenchmarkFixture, "Benchmark: Optimization Process", "[benchmark][performance]") {
    std::vector<size_t> dataset_sizes = {100, 1000, 5000, 10000, 50000};
    
    std::cout << "\n=== Optimization Time Analysis ===\n";
    std::cout << std::setw(15) << "Dataset Size" 
              << std::setw(20) << "Insert Time (ms)"
              << std::setw(20) << "Optimize Time (ms)"
              << std::setw(15) << "Keys/sec\n";
    std::cout << std::string(70, '-') << "\n";
    
    for (size_t size : dataset_sizes) {
        cleanup();
        db = Maph::create(test_file, size * 2);
        REQUIRE(db != nullptr);
        
        prepare_dataset(size);
        
        // Measure insert time
        auto insert_start = std::chrono::high_resolution_clock::now();
        populate_database();
        auto insert_end = std::chrono::high_resolution_clock::now();
        auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            insert_end - insert_start).count();
        
        // Measure optimization time
        auto opt_start = std::chrono::high_resolution_clock::now();
        auto result = db->optimize();
        auto opt_end = std::chrono::high_resolution_clock::now();
        auto opt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            opt_end - opt_start).count();
        
        REQUIRE(result.ok());
        
        double keys_per_sec = opt_ms > 0 ? (size * 1000.0 / opt_ms) : 0;
        
        std::cout << std::setw(15) << size
                  << std::setw(20) << insert_ms
                  << std::setw(20) << opt_ms
                  << std::setw(15) << std::fixed << std::setprecision(0) 
                  << keys_per_sec << "\n";
    }
    std::cout << "\n";
}

TEST_CASE("Detailed Performance Analysis", "[.][analysis]") {
    const size_t DATASET_SIZE = 10000;
    const size_t SAMPLE_SIZE = 1000;
    
    std::string test_file = "/tmp/perf_analysis.maph";
    auto db = Maph::create(test_file, DATASET_SIZE * 2);
    REQUIRE(db != nullptr);
    
    // Generate dataset
    std::vector<std::string> keys, values;
    for (size_t i = 0; i < DATASET_SIZE; ++i) {
        keys.push_back("key_" + std::to_string(i));
        values.push_back("{\"value\":" + std::to_string(i) + "}");
    }
    
    // Insert all data
    for (size_t i = 0; i < DATASET_SIZE; ++i) {
        db->set(keys[i], values[i]);
    }
    
    // Measure standard hash performance
    std::vector<double> standard_times;
    for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
        size_t idx = i % DATASET_SIZE;
        auto start = std::chrono::high_resolution_clock::now();
        auto val = db->get(keys[idx]);
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        standard_times.push_back(ns);
    }
    
    // Optimize
    db->optimize();
    
    // Measure perfect hash performance  
    std::vector<double> perfect_times;
    for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
        size_t idx = i % DATASET_SIZE;
        auto start = std::chrono::high_resolution_clock::now();
        auto val = db->get(keys[idx]);
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        perfect_times.push_back(ns);
    }
    
    // Calculate statistics
    auto calculate_stats = [](const std::vector<double>& times) {
        double sum = 0, min_t = times[0], max_t = times[0];
        for (double t : times) {
            sum += t;
            min_t = std::min(min_t, t);
            max_t = std::max(max_t, t);
        }
        double avg = sum / times.size();
        
        // Calculate percentiles
        std::vector<double> sorted = times;
        std::sort(sorted.begin(), sorted.end());
        double p50 = sorted[sorted.size() / 2];
        double p95 = sorted[sorted.size() * 95 / 100];
        double p99 = sorted[sorted.size() * 99 / 100];
        
        return std::make_tuple(avg, min_t, max_t, p50, p95, p99);
    };
    
    auto [std_avg, std_min, std_max, std_p50, std_p95, std_p99] = calculate_stats(standard_times);
    auto [pf_avg, pf_min, pf_max, pf_p50, pf_p95, pf_p99] = calculate_stats(perfect_times);
    
    std::cout << "\n=== Detailed Lookup Performance Analysis ===\n";
    std::cout << "Dataset: " << DATASET_SIZE << " keys, Sample: " << SAMPLE_SIZE << " lookups\n\n";
    
    std::cout << std::setw(20) << "Metric" 
              << std::setw(20) << "Standard Hash (ns)"
              << std::setw(20) << "Perfect Hash (ns)"
              << std::setw(15) << "Improvement\n";
    std::cout << std::string(75, '-') << "\n";
    
    auto print_row = [](const std::string& metric, double std_val, double pf_val) {
        double improvement = ((std_val - pf_val) / std_val) * 100;
        std::cout << std::setw(20) << metric
                  << std::setw(20) << std::fixed << std::setprecision(0) << std_val
                  << std::setw(20) << std::fixed << std::setprecision(0) << pf_val
                  << std::setw(14) << std::fixed << std::setprecision(1) << improvement << "%\n";
    };
    
    print_row("Average", std_avg, pf_avg);
    print_row("Minimum", std_min, pf_min);
    print_row("Maximum", std_max, pf_max);
    print_row("Median (P50)", std_p50, pf_p50);
    print_row("P95", std_p95, pf_p95);
    print_row("P99", std_p99, pf_p99);
    
    std::cout << "\n";
    
    // Cleanup
    db.reset();
    fs::remove(test_file);
    fs::remove(test_file + ".journal");
}