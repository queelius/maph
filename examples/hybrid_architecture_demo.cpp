/**
 * @file hybrid_architecture_demo.cpp
 * @brief Demonstrates hybrid architecture: REST API + direct mmap access
 *
 * This example shows how a C++ application can directly read from maph
 * stores managed by the REST API server, achieving zero IPC overhead.
 *
 * Setup:
 *   1. Start REST API server: ./maph_server_v3 8080
 *   2. Populate data via REST API (see below)
 *   3. Run this program: ./hybrid_architecture_demo
 *
 * Architecture:
 *   - REST API server: Writes to /data/cache.maph
 *   - This C++ app: Reads directly from /data/cache.maph (zero IPC)
 *
 * Performance:
 *   - Direct mmap read: ~300ns
 *   - REST API read: ~1-2ms
 *   - 5,000× faster!
 */

#include <maph/maph.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>

using namespace maph;

// Measure operation latency
template<typename F>
auto measure_latency(F&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    auto result = func();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return std::make_pair(result, duration.count());
}

int main() {
    std::cout << "=== maph v3 Hybrid Architecture Demo ===\n\n";

    // Path to store managed by REST API server
    std::filesystem::path store_path = "data/cache.maph";

    // Check if store exists
    if (!std::filesystem::exists(store_path)) {
        std::cerr << "Error: Store not found at " << store_path << "\n";
        std::cerr << "\nSetup Instructions:\n";
        std::cerr << "1. Start REST API server:\n";
        std::cerr << "   cd integrations/rest_api && ./maph_server_v3 8080\n\n";
        std::cerr << "2. Populate data via REST API:\n";
        std::cerr << "   curl -X POST http://localhost:8080/stores/cache\n";
        std::cerr << "   curl -X PUT -d 'value1' http://localhost:8080/stores/cache/keys/key1\n";
        std::cerr << "   curl -X PUT -d 'value2' http://localhost:8080/stores/cache/keys/key2\n";
        std::cerr << "   curl -X PUT -d 'value3' http://localhost:8080/stores/cache/keys/key3\n\n";
        std::cerr << "3. Run this demo:\n";
        std::cerr << "   ./hybrid_architecture_demo\n";
        return 1;
    }

    // Open store in READ-ONLY mode
    std::cout << "Opening store: " << store_path << " (read-only)\n";
    auto db_result = maph::open(store_path, true);  // readonly=true

    if (!db_result) {
        std::cerr << "Failed to open store\n";
        return 1;
    }

    auto db = std::move(*db_result);
    std::cout << "✓ Store opened successfully\n\n";

    // Get store statistics
    std::cout << "Store Statistics:\n";
    std::cout << "  Size: " << db.size() << " keys\n";
    std::cout << "  Load factor: " << std::fixed << std::setprecision(3)
              << db.load_factor() << "\n\n";

    if (db.size() == 0) {
        std::cout << "⚠ Store is empty. Add keys via REST API first.\n";
        std::cout << "\nExample:\n";
        std::cout << "  curl -X PUT -d 'hello world' http://localhost:8080/stores/cache/keys/greeting\n\n";
        return 0;
    }

    // Demonstrate direct mmap reads
    std::cout << "=== Direct mmap Read Performance ===\n\n";

    // Test keys (assumes they exist from REST API setup)
    std::vector<std::string> test_keys = {"key1", "key2", "key3", "greeting"};

    for (const auto& key : test_keys) {
        auto [result, latency_ns] = measure_latency([&]() {
            return db.get(key);
        });

        if (result) {
            std::cout << "✓ Key: " << std::setw(12) << std::left << key
                      << " Value: " << std::setw(20) << std::left << *result
                      << " Latency: " << std::setw(6) << std::right << latency_ns << " ns\n";
        } else {
            std::cout << "✗ Key: " << key << " (not found)\n";
        }
    }

    // Benchmark: Sustained read throughput
    std::cout << "\n=== Throughput Benchmark ===\n\n";

    const size_t num_ops = 100000;
    std::vector<std::string> keys;
    for (size_t i = 0; i < db.size() && i < 100; ++i) {
        keys.push_back("key" + std::to_string(i));
    }

    if (!keys.empty()) {
        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_ops; ++i) {
            auto result = db.get(keys[i % keys.size()]);
            // Force compiler to not optimize away
            volatile auto ptr = result.has_value();
            (void)ptr;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        double ops_per_sec = (num_ops * 1000.0) / duration_ms;
        double latency_ns = (duration_ms * 1000000.0) / num_ops;

        std::cout << "Operations: " << num_ops << "\n";
        std::cout << "Duration: " << duration_ms << " ms\n";
        std::cout << "Throughput: " << std::fixed << std::setprecision(2)
                  << ops_per_sec / 1000000.0 << " M ops/sec\n";
        std::cout << "Avg latency: " << std::fixed << std::setprecision(0)
                  << latency_ns << " ns\n\n";
    }

    // Demonstrate live updates
    std::cout << "=== Live Update Detection ===\n\n";
    std::cout << "Monitoring for changes...\n";
    std::cout << "(Update keys via REST API and watch them appear here)\n\n";
    std::cout << "Example: curl -X PUT -d 'updated!' http://localhost:8080/stores/cache/keys/key1\n\n";

    // Monitor for 10 seconds
    auto monitor_start = std::chrono::steady_clock::now();
    std::unordered_map<std::string, std::string> last_seen;

    for (const auto& key : test_keys) {
        auto val = db.get(key);
        if (val) {
            last_seen[key] = std::string(*val);
        }
    }

    while (std::chrono::steady_clock::now() - monitor_start < std::chrono::seconds(10)) {
        for (const auto& key : test_keys) {
            auto val = db.get(key);
            if (val) {
                std::string current(*val);
                if (last_seen[key] != current) {
                    std::cout << "🔄 Change detected: " << key << " = " << current << "\n";
                    last_seen[key] = current;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n=== Performance Summary ===\n\n";
    std::cout << "Direct mmap access advantages:\n";
    std::cout << "  ✓ Sub-microsecond latency (~300ns)\n";
    std::cout << "  ✓ Zero IPC overhead (no network, no serialization)\n";
    std::cout << "  ✓ Zero-copy (direct memory access)\n";
    std::cout << "  ✓ Sees REST API updates immediately\n";
    std::cout << "  ✓ Scales with multiple reader processes\n\n";

    std::cout << "Comparison with REST API:\n";
    std::cout << "  REST API read: ~1-2ms (localhost)\n";
    std::cout << "  Direct mmap: ~0.3μs\n";
    std::cout << "  Speedup: ~5,000×\n\n";

    std::cout << "✓ Demo complete!\n";
    return 0;
}
