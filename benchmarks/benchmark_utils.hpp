/**
 * @file benchmark_utils.hpp
 * @brief Utilities for accurate benchmarking with statistical analysis
 */

#pragma once

#include <chrono>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <string>
#include <random>

namespace maph::bench {

// High-resolution timer using rdtsc for nanosecond precision
class timer {
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;
    time_point start_;

public:
    timer() : start_(clock::now()) {}

    void reset() {
        start_ = clock::now();
    }

    // Returns elapsed time in nanoseconds
    uint64_t elapsed_ns() const {
        auto end = clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
    }

    // Returns elapsed time in microseconds
    double elapsed_us() const {
        return elapsed_ns() / 1000.0;
    }

    // Returns elapsed time in milliseconds
    double elapsed_ms() const {
        return elapsed_ns() / 1000000.0;
    }
};

// Statistical summary of timing measurements
struct stats {
    double min;
    double max;
    double mean;
    double median;
    double p90;
    double p95;
    double p99;
    double p999;
    double p9999;
    double stddev;
    size_t count;

    void print(const std::string& label, const std::string& unit = "ns") const {
        std::cout << "\n=== " << label << " ===\n";
        std::cout << "Count:   " << count << "\n";
        std::cout << "Min:     " << std::fixed << std::setprecision(2) << min << " " << unit << "\n";
        std::cout << "Max:     " << max << " " << unit << "\n";
        std::cout << "Mean:    " << mean << " " << unit << "\n";
        std::cout << "Median:  " << median << " " << unit << "\n";
        std::cout << "Stddev:  " << stddev << " " << unit << "\n";
        std::cout << "p90:     " << p90 << " " << unit << "\n";
        std::cout << "p95:     " << p95 << " " << unit << "\n";
        std::cout << "p99:     " << p99 << " " << unit << "\n";
        std::cout << "p99.9:   " << p999 << " " << unit << "\n";
        std::cout << "p99.99:  " << p9999 << " " << unit << "\n";
    }

    // Print in CSV format for easy import into paper tables
    void print_csv(const std::string& label) const {
        std::cout << label << ","
                  << std::fixed << std::setprecision(0)
                  << min << ","
                  << median << ","
                  << p90 << ","
                  << p99 << ","
                  << p999 << ","
                  << p9999 << "\n";
    }
};

// Compute statistics from a vector of measurements
inline stats compute_stats(std::vector<double> measurements) {
    if (measurements.empty()) {
        return {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    }

    std::sort(measurements.begin(), measurements.end());

    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * measurements.size());
        if (idx >= measurements.size()) idx = measurements.size() - 1;
        return measurements[idx];
    };

    double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
    double mean = sum / measurements.size();

    double sq_sum = std::accumulate(measurements.begin(), measurements.end(), 0.0,
        [mean](double acc, double val) {
            return acc + (val - mean) * (val - mean);
        });
    double stddev = std::sqrt(sq_sum / measurements.size());

    return {
        measurements.front(),
        measurements.back(),
        mean,
        percentile(0.5),
        percentile(0.90),
        percentile(0.95),
        percentile(0.99),
        percentile(0.999),
        percentile(0.9999),
        stddev,
        measurements.size()
    };
}

// Random key generator for benchmarks
class key_generator {
    std::mt19937_64 gen_;
    std::uniform_int_distribution<uint64_t> dist_;
    size_t key_count_;
    std::vector<std::string> cached_keys_;

public:
    explicit key_generator(size_t key_count, uint64_t seed = 42)
        : gen_(seed), dist_(0, UINT64_MAX), key_count_(key_count) {
        // Pre-generate keys for consistent benchmarking
        cached_keys_.reserve(key_count);
        for (size_t i = 0; i < key_count; ++i) {
            cached_keys_.push_back("key:" + std::to_string(i));
        }
    }

    // Get a specific key by index
    const std::string& get(size_t index) const {
        return cached_keys_[index % key_count_];
    }

    // Get a random key
    const std::string& random() {
        return cached_keys_[dist_(gen_) % key_count_];
    }

    // Get all keys
    const std::vector<std::string>& all_keys() const {
        return cached_keys_;
    }

    size_t count() const { return key_count_; }
};

// Value generator for benchmarks
class value_generator {
    std::mt19937_64 gen_;
    size_t value_size_;

public:
    explicit value_generator(size_t value_size, uint64_t seed = 42)
        : gen_(seed), value_size_(value_size) {}

    // Generate a value for a given key
    std::string generate(const std::string& key) {
        // Create JSON-like value with consistent size
        std::string value = R"({"id":")" + key + R"(","data":")";

        // Pad to desired size
        size_t padding_needed = value_size_ - value.size() - 2;  // -2 for closing quotes
        if (padding_needed > 0) {
            value.append(padding_needed, 'x');
        }
        value += "\"}";

        // Ensure exact size
        if (value.size() > value_size_) {
            value.resize(value_size_);
        }

        return value;
    }
};

// Zipfian distribution for realistic access patterns
class zipfian_generator {
    std::mt19937_64 gen_;
    std::uniform_real_distribution<double> dist_;
    size_t n_;
    double theta_;
    double alpha_;
    double zeta_;
    double eta_;

    double zeta(size_t n, double theta) {
        double sum = 0.0;
        for (size_t i = 1; i <= n; ++i) {
            sum += 1.0 / std::pow(i, theta);
        }
        return sum;
    }

public:
    explicit zipfian_generator(size_t n, double theta = 0.99, uint64_t seed = 42)
        : gen_(seed), dist_(0.0, 1.0), n_(n), theta_(theta) {
        zeta_ = zeta(n, theta);
        alpha_ = 1.0 / (1.0 - theta);
        eta_ = (1.0 - std::pow(2.0 / n, 1.0 - theta)) / (1.0 - zeta(2, theta) / zeta_);
    }

    size_t next() {
        double u = dist_(gen_);
        double uz = u * zeta_;

        if (uz < 1.0) return 0;
        if (uz < 1.0 + std::pow(0.5, theta_)) return 1;

        return static_cast<size_t>(n_ * std::pow(eta_ * u - eta_ + 1.0, alpha_));
    }
};

// Warmup helper to ensure caches are hot
template<typename Func>
void warmup(Func&& f, size_t iterations = 100000) {
    for (size_t i = 0; i < iterations; ++i) {
        f();
    }
}

// Benchmark runner with automatic warmup and statistical analysis
template<typename Func>
stats benchmark(const std::string& name, Func&& f, size_t iterations, size_t warmup_iterations = 1000) {
    std::cout << "Running benchmark: " << name << " (" << iterations << " iterations)\n";

    // Warmup
    std::cout << "  Warming up (" << warmup_iterations << " iterations)...\n";
    for (size_t i = 0; i < warmup_iterations; ++i) {
        f();
    }

    // Actual benchmark
    std::cout << "  Measuring...\n";
    std::vector<double> measurements;
    measurements.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        timer t;
        f();
        measurements.push_back(static_cast<double>(t.elapsed_ns()));
    }

    auto s = compute_stats(measurements);
    s.print(name);

    return s;
}

} // namespace maph::bench
