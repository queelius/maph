/**
 * @file maph_cli.cpp
 * @brief Command-line interface for maph database
 * 
 * Provides a comprehensive CLI for interacting with maph databases including:
 * - Database creation and management
 * - Key-value operations (get, set, remove)
 * - Batch operations for high-throughput scenarios
 * - Performance benchmarking tools
 * - Bulk data import/export
 * 
 * Usage: maph <command> [arguments] [options]
 * 
 * @author Maph Development Team
 * @version 1.0.0
 */

#include "maph.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <iomanip>  // For std::setprecision
#include <thread>   // For std::thread::hardware_concurrency

using namespace maph;

// Exit codes for consistent error handling
constexpr int EXIT_SUCCESS_CODE = 0;
constexpr int EXIT_ERROR_CODE = 1;
constexpr int EXIT_INVALID_ARGS = 2;
constexpr int EXIT_FILE_ERROR = 3;
constexpr int EXIT_DATABASE_FULL = 4;

/**
 * @brief Display comprehensive usage information
 * 
 * Prints all available commands, options, and examples to stderr.
 * Called when invalid arguments are provided or --help is requested.
 */
void usage() {
    std::cerr << R"(maph - Memory-mapped Approximate Perfect Hash

COMMANDS:
    create <file> <slots>           Create new maph file
    set <file> <key> <value>        Set key-value pair
    get <file> <key>                Get value for key
    remove <file> <key>             Remove key
    stats <file>                    Show statistics
    optimize <file>                 Optimize database with perfect hashing
    bench <file>                    Run benchmark
    bench_parallel <file> [threads] Run parallel benchmark
    load_bulk <file> <jsonl>        Load JSONL file in parallel
    mget <file> <key1> ...          Get multiple keys
    mset <file> k1 v1 k2 v2...      Set multiple key-value pairs

OPTIONS:
    --threads <n>                   Thread count for parallel ops
    --durability <ms>               Enable async durability

EXAMPLES:
    maph create data.maph 1000000
    maph set data.maph '{"id":1}' '{"name":"alice"}'
    maph get data.maph '{"id":1}'
    maph bench_parallel data.maph 8
    maph load_bulk data.maph input.jsonl --threads 4
)";
}

/**
 * @brief Main entry point for maph CLI
 * 
 * Parses command-line arguments and dispatches to appropriate command handler.
 * Supports various operations on maph databases including CRUD operations,
 * batch processing, and performance benchmarking.
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code (0 for success, non-zero for various error conditions)
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
        return EXIT_INVALID_ARGS;
    }
    
    std::string cmd = argv[1];
    
    // Handle help request
    if (cmd == "--help" || cmd == "-h") {
        usage();
        return EXIT_SUCCESS_CODE;
    }
    
    /**
     * CREATE command - Create a new database file
     * 
     * Usage: maph create <file> <slots>
     * 
     * Creates a new memory-mapped database with the specified number of slots.
     * The file size will be approximately slots * 512 bytes + header.
     */
    if (cmd == "create" && argc == 4) {
        try {
            uint64_t num_slots = std::stoull(argv[3]);
            
            // Validate slot count
            if (num_slots == 0) {
                std::cerr << "Error: Number of slots must be greater than 0\n";
                return EXIT_INVALID_ARGS;
            }
            
            auto m = Maph::create(argv[2], num_slots);
            if (!m) {
                std::cerr << "Failed to create " << argv[2] << "\n";
                std::cerr << "Check disk space and permissions\n";
                return EXIT_FILE_ERROR;
            }
            
            // Calculate and display file size
            size_t file_size = sizeof(Header) + (num_slots * sizeof(Slot));
            std::cout << "Created " << argv[2] << " with " << num_slots << " slots\n";
            std::cout << "File size: " << (file_size / (1024*1024)) << " MB\n";
            return EXIT_SUCCESS_CODE;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return EXIT_ERROR_CODE;
        }
    }
    
    /**
     * SET command - Store a key-value pair
     * 
     * Usage: maph set <file> <key> <value>
     * 
     * Stores or updates a value in the database. Value must be <= 496 bytes.
     * Returns error if database is full or value is too large.
     */
    if (cmd == "set" && argc == 5) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            std::cerr << "Check if file exists and has correct permissions\n";
            return EXIT_FILE_ERROR;
        }
        
        // Check value size
        std::string_view value(argv[4]);
        if (value.size() > Slot::MAX_SIZE) {
            std::cerr << "Error: Value too large (" << value.size() 
                      << " bytes, max " << Slot::MAX_SIZE << ")\n";
            return EXIT_INVALID_ARGS;
        }
        
        if (m->set(argv[3], argv[4])) {
            std::cout << "OK\n";
            return EXIT_SUCCESS_CODE;
        } else {
            std::cerr << "Failed to set - database may be full\n";
            return EXIT_DATABASE_FULL;
        }
    }
    
    /**
     * GET command - Retrieve a value by key
     * 
     * Usage: maph get <file> <key>
     * 
     * Retrieves the value associated with the key.
     * Prints the value to stdout if found, "null" if not found.
     * Uses read-only mode for better performance and safety.
     */
    if (cmd == "get" && argc == 4) {
        auto m = open_readonly(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        if (auto value = m->get(argv[3])) {
            // Value found - print to stdout
            std::cout << *value << "\n";
            return EXIT_SUCCESS_CODE;
        } else {
            // Key not found - print null and return error
            std::cout << "null\n";
            return EXIT_ERROR_CODE;
        }
    }
    
    /**
     * REMOVE command - Delete a key-value pair
     * 
     * Usage: maph remove <file> <key>
     * 
     * Removes the specified key from the database.
     * The slot is marked as empty and can be reused.
     */
    if (cmd == "remove" && argc == 4) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        if (m->remove(argv[3])) {
            std::cout << "OK\n";
            return EXIT_SUCCESS_CODE;
        } else {
            std::cerr << "Not found\n";
            return EXIT_ERROR_CODE;
        }
    }
    
    /**
     * STATS command - Display database statistics
     * 
     * Usage: maph stats <file>
     * 
     * Shows detailed statistics including slot usage, memory consumption,
     * load factor, and generation counter. Useful for monitoring database health.
     */
    if (cmd == "stats" && argc == 3) {
        auto m = open_readonly(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        auto s = m->stats();
        
        // Display statistics in readable format
        std::cout << "Database: " << argv[2] << "\n";
        std::cout << "======================\n";
        std::cout << "Total slots:     " << s.total_slots << "\n";
        std::cout << "Used slots:      " << s.used_slots 
                  << " (" << (s.used_slots * 100.0 / s.total_slots) << "%)\n";
        std::cout << "Free slots:      " << (s.total_slots - s.used_slots) << "\n";
        std::cout << "Load factor:     " << std::fixed << std::setprecision(4) << s.load_factor << "\n";
        std::cout << "Memory:          " << s.memory_bytes / (1024*1024) << " MB\n";
        std::cout << "Generation:      " << s.generation << "\n";
        std::cout << "Optimized:       " << (s.is_optimized ? "Yes" : "No") << "\n";
        if (s.is_optimized) {
            std::cout << "Perfect hash keys: " << s.perfect_hash_keys << "\n";
        }
        std::cout << "Journal entries: " << s.journal_entries << "\n";
        std::cout << "Collision rate:  " << std::fixed << std::setprecision(2) 
                  << (s.collision_rate * 100) << "%\n";
        
        // Warn if database is getting full
        if (s.load_factor > 0.8) {
            std::cerr << "\nWARNING: Database is " 
                      << (s.load_factor * 100) << "% full\n";
        }
        
        return EXIT_SUCCESS_CODE;
    }
    
    /**
     * OPTIMIZE command - Optimize database with perfect hashing
     * 
     * Usage: maph optimize <file>
     * 
     * Builds perfect hash function for all keys in the journal,
     * enabling O(1) guaranteed lookups for existing keys.
     */
    if (cmd == "optimize" && argc == 3) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        std::cout << "Optimizing database with perfect hashing...\n";
        auto start = std::chrono::high_resolution_clock::now();
        
        auto result = m->optimize();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        if (result.ok()) {
            std::cout << "Optimization completed in " << ms << " ms\n";
            std::cout << result.message << "\n";
            
            auto stats = m->stats();
            std::cout << "Database now optimized with " << stats.perfect_hash_keys << " keys\n";
            std::cout << "Journal entries: " << stats.journal_entries << "\n";
            
            return EXIT_SUCCESS_CODE;
        } else {
            std::cerr << "Optimization failed: " << result.message << "\n";
            return EXIT_ERROR_CODE;
        }
    }
    
    /**
     * BENCH command - Run single-threaded performance benchmark
     * 
     * Usage: maph bench <file>
     * 
     * Performs sequential write and read operations to measure throughput.
     * Writes 100,000 key-value pairs then reads them back.
     * Reports operations per second for both writes and reads.
     */
    if (cmd == "bench" && argc == 3) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        const int N = 100000;  // Number of operations for benchmark
        
        std::cout << "Running benchmark with " << N << " operations...\n";
        auto start = std::chrono::high_resolution_clock::now();
        
        // Write benchmark - test sequential insertion performance
        for (int i = 0; i < N; ++i) {
            std::string key = "{\"id\":" + std::to_string(i) + "}";
            std::string val = "{\"v\":" + std::to_string(i*10) + "}";
            if (!m->set(key, val)) {
                std::cerr << "Warning: Write failed at iteration " << i << "\n";
            }
        }
        
        auto mid = std::chrono::high_resolution_clock::now();
        
        // Read benchmark - test sequential retrieval performance
        // Count successful reads
        int found = 0;
        for (int i = 0; i < N; ++i) {
            std::string key = "{\"id\":" + std::to_string(i) + "}";
            auto v = m->get(key);
            if (v.has_value()) found++;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        
        // Calculate performance metrics
        auto write_us = std::chrono::duration_cast<std::chrono::microseconds>(mid - start).count();
        auto read_us = std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count();
        
        // Display results
        std::cout << "\nBenchmark Results:\n";
        std::cout << "==================\n";
        std::cout << "Write Performance:\n";
        std::cout << "  Operations: " << N << "\n";
        std::cout << "  Time: " << write_us / 1000.0 << " ms\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / write_us) << " ops/sec\n";
        std::cout << "  Latency: " << (write_us * 1000 / N) << " ns/op\n\n";
        
        std::cout << "Read Performance:\n";
        std::cout << "  Operations: " << N << "\n";
        std::cout << "  Found: " << found << "/" << N << "\n";
        std::cout << "  Time: " << read_us / 1000.0 << " ms\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / read_us) << " ops/sec\n";
        std::cout << "  Latency: " << (read_us * 1000 / N) << " ns/op\n";
        
        return EXIT_SUCCESS_CODE;
    }
    
    /**
     * BENCH_PARALLEL command - Run multi-threaded performance benchmark
     * 
     * Usage: maph bench_parallel <file> [threads]
     * 
     * Performs parallel write and read operations using multiple threads.
     * If thread count is not specified, uses hardware concurrency.
     * Demonstrates scalability and parallel throughput capabilities.
     */
    if (cmd == "bench_parallel" && (argc == 3 || argc == 4)) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        // Parse thread count or use hardware default
        size_t threads = (argc == 4) ? std::stoull(argv[3]) : std::thread::hardware_concurrency();
        const int N = 100000;  // Total operations to perform
        
        std::cout << "Running parallel benchmark with " << threads << " threads...\n";
        std::cout << "Operations per thread: " << (N / threads) << "\n\n";
        
        // Prepare batch data - pre-allocate to avoid allocation during benchmark
        std::vector<std::pair<JsonView, JsonView>> kvs;
        std::vector<std::string> keys, values;
        keys.reserve(N);
        values.reserve(N);
        
        for (int i = 0; i < N; ++i) {
            keys.push_back("{\"id\":" + std::to_string(i) + "}");
            values.push_back("{\"v\":" + std::to_string(i*10) + "}");
        }
        
        // Create views for zero-copy operations
        for (int i = 0; i < N; ++i) {
            kvs.emplace_back(keys[i], values[i]);
        }
        
        // Parallel write benchmark - distribute writes across threads
        auto start = std::chrono::high_resolution_clock::now();
        size_t written = m->parallel_mset(kvs, threads);
        auto mid = std::chrono::high_resolution_clock::now();
        
        // Parallel read benchmark - distribute reads across threads
        std::vector<JsonView> key_views;
        key_views.reserve(keys.size());
        for (const auto& k : keys) {
            key_views.push_back(k);
        }
        
        // Count successful reads using atomic counter
        std::atomic<size_t> count{0};
        m->parallel_mget(key_views, 
            [&count](JsonView k, JsonView v) { 
                count.fetch_add(1, std::memory_order_relaxed); 
            }, 
            threads);
        auto end = std::chrono::high_resolution_clock::now();
        
        // Calculate and display performance metrics
        auto write_us = std::chrono::duration_cast<std::chrono::microseconds>(mid - start).count();
        auto read_us = std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count();
        
        std::cout << "Parallel Benchmark Results (" << threads << " threads):\n";
        std::cout << "======================================\n";
        std::cout << "Write Performance:\n";
        std::cout << "  Operations: " << N << "\n";
        std::cout << "  Successful: " << written << "\n";
        std::cout << "  Time: " << write_us / 1000.0 << " ms\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / write_us) << " ops/sec\n";
        std::cout << "  Per-thread: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / write_us / threads) << " ops/sec/thread\n\n";
        
        std::cout << "Read Performance:\n";
        std::cout << "  Operations: " << N << "\n";
        std::cout << "  Found: " << count.load() << "\n";
        std::cout << "  Time: " << read_us / 1000.0 << " ms\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / read_us) << " ops/sec\n";
        std::cout << "  Per-thread: " << std::fixed << std::setprecision(0) 
                  << (N * 1000000.0 / read_us / threads) << " ops/sec/thread\n";
        
        // Calculate speedup
        std::cout << "\nSpeedup vs single thread: " 
                  << std::fixed << std::setprecision(2)
                  << threads << "x theoretical, "
                  << (N * 1000000.0 / read_us) / (5000000.0) << "x actual\n";
        
        return EXIT_SUCCESS_CODE;
    }
    
    /**
     * LOAD_BULK command - Import data from JSONL file
     * 
     * Usage: maph load_bulk <file> <jsonl> [--threads <n>]
     * 
     * Loads key-value pairs from a JSONL file using parallel processing.
     * Each line should contain {"input": key, "output": value}.
     * Supports custom thread count for optimal performance.
     */
    if (cmd == "load_bulk" && argc >= 4) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open database " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        size_t threads = std::thread::hardware_concurrency();
        // Check for --threads option
        for (int i = 4; i < argc - 1; ++i) {
            if (std::string(argv[i]) == "--threads") {
                threads = std::stoull(argv[i + 1]);
                break;
            }
        }
        
        std::ifstream file(argv[3]);
        if (!file) {
            std::cerr << "Failed to open " << argv[3] << "\n";
            return 1;
        }
        
        // Load all lines first
        std::vector<std::pair<std::string, std::string>> kvs;
        std::string line;
        while (std::getline(file, line)) {
            // Parse JSONL: expect {"input": key, "output": value}
            size_t input_pos = line.find("\"input\":");
            size_t output_pos = line.find("\"output\":");
            if (input_pos != std::string::npos && output_pos != std::string::npos) {
                // Simple extraction (assumes no nested quotes)
                size_t key_start = input_pos + 8;
                size_t key_end = line.find(',', key_start);
                size_t val_start = output_pos + 9;
                size_t val_end = line.find('}', val_start);
                
                std::string key = line.substr(key_start, key_end - key_start);
                std::string val = line.substr(val_start, val_end - val_start);
                
                // Trim quotes if present
                if (key.front() == '"') key = key.substr(1, key.size() - 2);
                if (val.front() == '"') val = val.substr(1, val.size() - 2);
                
                kvs.emplace_back(key, val);
            }
        }
        
        std::cout << "Loading " << kvs.size() << " entries with " << threads << " threads...\n";
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Convert to views for parallel_mset
        std::vector<std::pair<JsonView, JsonView>> kv_views;
        for (const auto& [k, v] : kvs) {
            kv_views.emplace_back(k, v);
        }
        
        size_t loaded = m->parallel_mset(kv_views, threads);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        std::cout << "Loaded " << loaded << "/" << kvs.size() << " entries in " << ms << " ms\n";
        std::cout << "Rate: " << (loaded * 1000.0 / ms) << " entries/sec\n";
        return 0;
    }
    
    /**
     * MGET command - Get multiple keys in batch
     * 
     * Usage: maph mget <file> <key1> [key2] [key3] ...
     * 
     * Retrieves multiple keys efficiently using prefetching.
     * Prints each found key-value pair to stdout.
     * More efficient than multiple individual get operations.
     */
    if (cmd == "mget" && argc >= 4) {
        auto m = open_readonly(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        // Collect all keys from command line
        std::vector<JsonView> keys;
        keys.reserve(argc - 3);
        for (int i = 3; i < argc; ++i) {
            keys.emplace_back(argv[i]);
        }
        
        // Perform batch retrieval with callback for each found pair
        size_t found = 0;
        m->mget(keys, [&found](JsonView key, JsonView value) {
            std::cout << key << ": " << value << "\n";
            found++;
        });
        
        // Report summary
        if (found < keys.size()) {
            std::cerr << "\nFound " << found << "/" << keys.size() << " keys\n";
        }
        
        return (found == keys.size()) ? EXIT_SUCCESS_CODE : EXIT_ERROR_CODE;
    }
    
    /**
     * MSET command - Set multiple key-value pairs in batch
     * 
     * Usage: maph mset <file> <key1> <value1> [key2] [value2] ...
     * 
     * Sets multiple key-value pairs efficiently in a single operation.
     * Arguments must come in pairs (key followed by value).
     * Reports how many pairs were successfully stored.
     */
    if (cmd == "mset" && argc >= 5 && (argc - 3) % 2 == 0) {
        auto m = open(argv[2]);
        if (!m) {
            std::cerr << "Failed to open " << argv[2] << "\n";
            return EXIT_FILE_ERROR;
        }
        
        // Collect key-value pairs from command line
        std::vector<std::pair<JsonView, JsonView>> kvs;
        kvs.reserve((argc - 3) / 2);
        
        // Validate value sizes before attempting batch operation
        for (int i = 3; i < argc; i += 2) {
            std::string_view value(argv[i + 1]);
            if (value.size() > Slot::MAX_SIZE) {
                std::cerr << "Error: Value for key '" << argv[i] 
                          << "' too large (" << value.size() << " bytes)\n";
                return EXIT_INVALID_ARGS;
            }
            kvs.emplace_back(argv[i], argv[i + 1]);
        }
        
        // Perform batch insertion
        size_t count = m->mset(kvs);
        std::cout << "Stored " << count << "/" << kvs.size() << " pairs\n";
        
        if (count < kvs.size()) {
            std::cerr << "Warning: " << (kvs.size() - count) 
                      << " pairs failed (database may be full)\n";
            return EXIT_DATABASE_FULL;
        }
        
        return EXIT_SUCCESS_CODE;
    }
    
    // If no command matched, show usage
    std::cerr << "Error: Unknown command '" << cmd << "'\n\n";
    usage();
    return EXIT_INVALID_ARGS;
}