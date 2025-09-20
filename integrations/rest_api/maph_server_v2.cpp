/**
 * @file maph_server_v2.cpp
 * @brief Enhanced REST API server for maph v2 with perfect hashing support
 * 
 * Provides comprehensive REST API for maph v2 databases including:
 * - Standard CRUD operations
 * - Perfect hash optimization workflow
 * - Optimization monitoring and statistics
 * - Batch import/export with perfect hash support
 * - Performance benchmarking endpoints
 */

#include "maph_v2.hpp"
#include "httplib.h"
#include <filesystem>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

using namespace maph;
namespace fs = std::filesystem;

// JSON normalization (unchanged from v1)
std::string minimal_normalize_json(const std::string& json) {
    std::string result;
    result.reserve(json.size());
    
    bool in_string = false;
    bool escape_next = false;
    
    for (char c : json) {
        if (escape_next) {
            result += c;
            escape_next = false;
            continue;
        }
        
        if (c == '\\' && in_string) {
            result += c;
            escape_next = true;
            continue;
        }
        
        if (c == '"') {
            in_string = !in_string;
            result += c;
            continue;
        }
        
        if (in_string || !std::isspace(c)) {
            result += c;
        }
    }
    
    return result;
}

// Enhanced JSON builder with perfect hashing support
class SimpleJSON {
    std::ostringstream ss;
    bool first = true;
    
public:
    SimpleJSON& begin_object() { ss << "{"; first = true; return *this; }
    SimpleJSON& end_object() { ss << "}"; return *this; }
    SimpleJSON& begin_array() { ss << "["; first = true; return *this; }
    SimpleJSON& end_array() { ss << "]"; return *this; }
    
    SimpleJSON& key(const std::string& k) {
        if (!first) ss << ",";
        ss << "\"" << k << "\":";
        first = false;
        return *this;
    }
    
    SimpleJSON& value(const std::string& v) {
        ss << "\"" << v << "\"";
        return *this;
    }
    
    SimpleJSON& value(int64_t v) {
        ss << v;
        return *this;
    }
    
    SimpleJSON& value(double v) {
        ss << v;
        return *this;
    }
    
    SimpleJSON& value(bool v) {
        ss << (v ? "true" : "false");
        return *this;
    }
    
    SimpleJSON& raw(const std::string& r) {
        ss << r;
        return *this;
    }
    
    SimpleJSON& comma() {
        if (!first) ss << ",";
        first = false;
        return *this;
    }
    
    std::string str() const { return ss.str(); }
};

std::string hash_mode_to_string(HashMode mode) {
    switch (mode) {
        case HashMode::STANDARD: return "standard";
        case HashMode::PERFECT: return "perfect";
        case HashMode::HYBRID: return "hybrid";
        default: return "unknown";
    }
}

std::string hash_type_to_string(PerfectHashType type) {
    switch (type) {
        case PerfectHashType::RECSPLIT: return "recsplit";
        case PerfectHashType::CHD: return "chd";
        case PerfectHashType::BBHASH: return "bbhash";
        case PerfectHashType::DISABLED: return "disabled";
        default: return "unknown";
    }
}

PerfectHashType parse_hash_type(const std::string& type) {
    if (type == "recsplit") return PerfectHashType::RECSPLIT;
    if (type == "chd") return PerfectHashType::CHD;
    if (type == "bbhash") return PerfectHashType::BBHASH;
    return PerfectHashType::RECSPLIT;  // Default
}

// Enhanced Store Registry with perfect hashing support
class StoreRegistry {
private:
    struct StoreEntry {
        std::string name;
        std::unique_ptr<Maph> store;
        std::atomic<uint64_t> operations{0};
        std::chrono::steady_clock::time_point created;
        std::atomic<bool> optimized{false};
        std::atomic<uint64_t> optimization_time_ms{0};
    };
    
    std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<StoreEntry>> stores_;
    std::string data_dir_ = "./maph_stores/";
    
public:
    StoreRegistry() {
        fs::create_directories(data_dir_);
        // Load existing stores
        for (const auto& entry : fs::directory_iterator(data_dir_)) {
            if (entry.path().extension() == ".maph") {
                auto name = entry.path().stem().string();
                auto store = Maph::open(entry.path().string());
                if (store) {
                    auto store_entry = std::make_unique<StoreEntry>();
                    store_entry->name = name;
                    store_entry->store = std::move(store);
                    store_entry->created = std::chrono::steady_clock::now();
                    
                    // Check if already optimized
                    auto stats = store_entry->store->get_optimization_stats();
                    store_entry->optimized = stats.is_optimized;
                    
                    stores_[name] = std::move(store_entry);
                    std::cout << "Loaded store: " << name 
                             << (store_entry->optimized ? " (optimized)" : " (standard)") << std::endl;
                }
            }
        }
    }
    
    bool create(const std::string& name, uint64_t slots) {
        std::unique_lock lock(mutex_);
        if (stores_.find(name) != stores_.end()) {
            return false;
        }
        
        auto path = data_dir_ + name + ".maph";
        auto store = Maph::create(path, slots);
        if (!store) return false;
        
        auto entry = std::make_unique<StoreEntry>();
        entry->name = name;
        entry->store = std::move(store);
        entry->created = std::chrono::steady_clock::now();
        stores_[name] = std::move(entry);
        return true;
    }
    
    bool remove(const std::string& name) {
        std::unique_lock lock(mutex_);
        auto it = stores_.find(name);
        if (it == stores_.end()) return false;
        
        stores_.erase(it);
        fs::remove(data_dir_ + name + ".maph");
        fs::remove(data_dir_ + name + ".maph.journal");  // Remove journal too
        return true;
    }
    
    template<typename F>
    bool with_store(const std::string& name, F&& func) {
        std::shared_lock lock(mutex_);
        auto it = stores_.find(name);
        if (it == stores_.end()) return false;
        
        it->second->operations.fetch_add(1, std::memory_order_relaxed);
        func(*it->second->store, *it->second);
        return true;
    }
    
    std::string list_json() {
        std::shared_lock lock(mutex_);
        SimpleJSON json;
        json.begin_array();
        
        bool first = true;
        for (const auto& [name, entry] : stores_) {
            if (!first) json.comma();
            first = false;
            
            auto stats = entry->store->stats();
            auto opt_stats = entry->store->get_optimization_stats();
            
            json.begin_object()
                .key("name").value(name)
                .key("slots").value(static_cast<int64_t>(stats.total_slots))
                .key("used").value(static_cast<int64_t>(stats.used_slots))
                .key("load_factor").value(stats.load_factor)
                .key("memory_mb").value(stats.memory_bytes / (1024.0 * 1024.0))
                .key("operations").value(static_cast<int64_t>(entry->operations.load()))
                .key("hash_mode").value(hash_mode_to_string(stats.hash_mode))
                .key("hash_type").value(hash_type_to_string(stats.perfect_hash_type))
                .key("optimized").value(stats.is_optimized)
                .key("perfect_hash_memory").value(static_cast<int64_t>(stats.perfect_hash_memory))
                .key("optimization_time_ms").value(static_cast<int64_t>(entry->optimization_time_ms.load()))
                .end_object();
        }
        json.end_array();
        return json.str();
    }
};

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    
    StoreRegistry registry;
    httplib::Server svr;
    
    // Enable CORS
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
    
    svr.Options(".*", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });
    
    // ===== STANDARD ENDPOINTS =====
    
    // List stores (enhanced with optimization info)
    svr.Get("/stores", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content(registry.list_json(), "application/json");
    });
    
    // Create store
    svr.Post("/stores", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name;
        uint64_t slots = 100000;
        
        size_t name_pos = req.body.find("\"name\"");
        if (name_pos != std::string::npos) {
            size_t start = req.body.find("\"", name_pos + 6) + 1;
            size_t end = req.body.find("\"", start);
            name = req.body.substr(start, end - start);
        }
        
        size_t slots_pos = req.body.find("\"slots\"");
        if (slots_pos != std::string::npos) {
            size_t start = req.body.find(":", slots_pos) + 1;
            size_t end = req.body.find_first_of(",}", start);
            slots = std::stoull(req.body.substr(start, end - start));
        }
        
        if (name.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Name required\"}", "application/json");
            return;
        }
        
        if (registry.create(name, slots)) {
            SimpleJSON json;
            json.begin_object()
                .key("success").value(true)
                .key("name").value(name)
                .key("slots").value(static_cast<int64_t>(slots))
                .key("hash_mode").value("standard")
                .key("optimized").value(false)
                .end_object();
            res.status = 201;
            res.set_content(json.str(), "application/json");
        } else {
            res.status = 409;
            res.set_content("{\"error\":\"Store already exists\"}", "application/json");
        }
    });
    
    // Delete store
    svr.Delete(R"(/stores/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        if (registry.remove(name)) {
            res.status = 204;
        } else {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // Get store stats (enhanced with optimization info)
    svr.Get(R"(/stores/([^/]+)/stats)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        bool found = registry.with_store(name, [&res](Maph& store, auto& entry) {
            auto stats = store.stats();
            auto opt_stats = store.get_optimization_stats();
            
            SimpleJSON json;
            json.begin_object()
                .key("total_slots").value(static_cast<int64_t>(stats.total_slots))
                .key("used_slots").value(static_cast<int64_t>(stats.used_slots))
                .key("load_factor").value(stats.load_factor)
                .key("memory_bytes").value(static_cast<int64_t>(stats.memory_bytes))
                .key("generation").value(static_cast<int64_t>(stats.generation))
                .key("hash_mode").value(hash_mode_to_string(stats.hash_mode))
                .key("hash_type").value(hash_type_to_string(stats.perfect_hash_type))
                .key("optimized").value(stats.is_optimized)
                .key("perfect_hash_memory").value(static_cast<int64_t>(stats.perfect_hash_memory))
                .key("total_keys").value(static_cast<int64_t>(opt_stats.total_keys))
                .key("collision_rate").value(opt_stats.collision_rate)
                .key("optimization_time_ms").value(static_cast<int64_t>(entry.optimization_time_ms.load()))
                .end_object();
            res.set_content(json.str(), "application/json");
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // ===== CRUD OPERATIONS (mostly unchanged) =====
    
    svr.Get(R"(/stores/([^/]+)/keys/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        std::string key = req.matches[2];
        std::string normalized_key = minimal_normalize_json(key);
        
        bool found = registry.with_store(name, [&res, &normalized_key](Maph& store, auto& entry) {
            if (auto value = store.get(normalized_key)) {
                res.set_content(std::string(*value), "application/json");
            } else {
                res.status = 404;
                res.set_content("{\"error\":\"Key not found\"}", "application/json");
            }
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    svr.Put(R"(/stores/([^/]+)/keys/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        std::string key = req.matches[2];
        std::string normalized_key = minimal_normalize_json(key);
        std::string normalized_value = minimal_normalize_json(req.body);
        
        bool found = registry.with_store(name, [&res, &normalized_key, &normalized_value](Maph& store, auto& entry) {
            if (store.set(normalized_key, normalized_value)) {
                res.status = 204;
            } else {
                res.status = 507;
                res.set_content("{\"error\":\"Failed to set value\"}", "application/json");
            }
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    svr.Delete(R"(/stores/([^/]+)/keys/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        std::string key = req.matches[2];
        std::string normalized_key = minimal_normalize_json(key);
        
        bool found = registry.with_store(name, [&res, &normalized_key](Maph& store, auto& entry) {
            if (store.remove(normalized_key)) {
                res.status = 204;
            } else {
                res.status = 404;
                res.set_content("{\"error\":\"Key not found\"}", "application/json");
            }
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // ===== PERFECT HASH OPTIMIZATION ENDPOINTS =====
    
    // Optimize store with perfect hashing
    svr.Post(R"(/stores/([^/]+)/optimize)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        // Parse optimization configuration from request body
        PerfectHashConfig config;
        
        // Simple JSON parsing for configuration
        if (!req.body.empty()) {
            if (req.body.find("\"type\":\"chd\"") != std::string::npos) {
                config.type = PerfectHashType::CHD;
            } else if (req.body.find("\"type\":\"bbhash\"") != std::string::npos) {
                config.type = PerfectHashType::BBHASH;
            } else {
                config.type = PerfectHashType::RECSPLIT;  // Default
            }
            
            size_t leaf_size_pos = req.body.find("\"leaf_size\":");
            if (leaf_size_pos != std::string::npos) {
                size_t start = req.body.find(":", leaf_size_pos) + 1;
                size_t end = req.body.find_first_of(",}", start);
                try {
                    config.leaf_size = std::stoul(req.body.substr(start, end - start));
                } catch (...) {}
            }
            
            size_t threads_pos = req.body.find("\"threads\":");
            if (threads_pos != std::string::npos) {
                size_t start = req.body.find(":", threads_pos) + 1;
                size_t end = req.body.find_first_of(",}", start);
                try {
                    config.threads = std::stoul(req.body.substr(start, end - start));
                } catch (...) {}
            }
        }
        
        bool found = registry.with_store(name, [&res, &config](Maph& store, auto& entry) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            auto result = store.optimize(config);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            if (result.ok()) {
                entry.optimized = true;
                entry.optimization_time_ms = duration_ms;
                
                auto opt_stats = store.get_optimization_stats();
                
                SimpleJSON json;
                json.begin_object()
                    .key("success").value(true)
                    .key("message").value(result.message)
                    .key("optimization_time_ms").value(duration_ms)
                    .key("hash_mode").value(hash_mode_to_string(opt_stats.current_mode))
                    .key("hash_type").value(hash_type_to_string(opt_stats.hash_type))
                    .key("total_keys").value(static_cast<int64_t>(opt_stats.total_keys))
                    .key("perfect_hash_memory").value(static_cast<int64_t>(opt_stats.perfect_hash_memory))
                    .key("collision_rate").value(opt_stats.collision_rate)
                    .key("benefits").begin_array()
                        .value("Zero collisions")
                        .value("Guaranteed O(1) lookups")
                        .value("Optimal memory layout")
                        .value("Single memory access per lookup")
                    .end_array()
                    .end_object();
                    
                res.set_content(json.str(), "application/json");
            } else {
                SimpleJSON json;
                json.begin_object()
                    .key("success").value(false)
                    .key("error").value(result.message)
                    .key("optimization_time_ms").value(duration_ms)
                    .end_object();
                res.status = 500;
                res.set_content(json.str(), "application/json");
            }
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // Get optimization status
    svr.Get(R"(/stores/([^/]+)/optimization)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        bool found = registry.with_store(name, [&res](Maph& store, auto& entry) {
            auto stats = store.stats();
            auto opt_stats = store.get_optimization_stats();
            
            SimpleJSON json;
            json.begin_object()
                .key("optimized").value(stats.is_optimized)
                .key("hash_mode").value(hash_mode_to_string(stats.hash_mode))
                .key("hash_type").value(hash_type_to_string(stats.perfect_hash_type))
                .key("total_keys").value(static_cast<int64_t>(opt_stats.total_keys))
                .key("perfect_hash_memory").value(static_cast<int64_t>(opt_stats.perfect_hash_memory))
                .key("collision_rate").value(opt_stats.collision_rate)
                .key("optimization_time_ms").value(static_cast<int64_t>(entry.optimization_time_ms.load()))
                .key("performance_benefits").begin_object()
                    .key("lookup_complexity").value(stats.is_optimized ? "O(1) guaranteed" : "O(1) average, O(k) worst case")
                    .key("memory_accesses").value(stats.is_optimized ? 1 : "1-10")
                    .key("collision_probability").value(stats.is_optimized ? 0.0 : 0.1)
                .end_object()
                .end_object();
            res.set_content(json.str(), "application/json");
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // ===== ENHANCED IMPORT WITH OPTIMIZATION =====
    
    // Import with automatic optimization
    svr.Post(R"(/stores/([^/]+)/import-and-optimize)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        bool found = registry.with_store(name, [&res, &req](Maph& store, auto& entry) {
            std::istringstream stream(req.body);
            std::string line;
            size_t imported = 0;
            size_t failed = 0;
            std::vector<std::pair<JsonView, JsonView>> batch;
            std::vector<std::string> storage;
            
            // Import phase
            auto import_start = std::chrono::high_resolution_clock::now();
            
            while (std::getline(stream, line)) {
                if (line.empty()) continue;
                
                size_t input_pos = line.find("\"input\"");
                size_t output_pos = line.find("\"output\"");
                
                if (input_pos != std::string::npos && output_pos != std::string::npos) {
                    // Simplified JSON parsing
                    size_t input_start = line.find(':', input_pos) + 1;
                    size_t input_end = line.find(',', input_start);
                    size_t output_start = line.find(':', output_pos) + 1;
                    size_t output_end = line.rfind('}');
                    
                    if (input_end != std::string::npos && output_end != std::string::npos) {
                        std::string input = line.substr(input_start, input_end - input_start);
                        std::string output = line.substr(output_start, output_end - output_start);
                        
                        storage.push_back(minimal_normalize_json(input));
                        storage.push_back(minimal_normalize_json(output));
                        
                        batch.emplace_back(storage[storage.size()-2], storage[storage.size()-1]);
                        
                        if (batch.size() >= 1000) {
                            imported += store.parallel_mset(batch);
                            batch.clear();
                            storage.clear();
                        }
                    } else {
                        failed++;
                    }
                } else {
                    failed++;
                }
            }
            
            if (!batch.empty()) {
                imported += store.parallel_mset(batch);
            }
            
            auto import_end = std::chrono::high_resolution_clock::now();
            auto import_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                import_end - import_start).count();
            
            // Optimization phase
            auto opt_start = std::chrono::high_resolution_clock::now();
            auto result = store.optimize();
            auto opt_end = std::chrono::high_resolution_clock::now();
            auto opt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                opt_end - opt_start).count();
            
            if (result.ok()) {
                entry.optimized = true;
                entry.optimization_time_ms = opt_ms;
                
                auto opt_stats = store.get_optimization_stats();
                
                SimpleJSON json;
                json.begin_object()
                    .key("success").value(true)
                    .key("imported").value(static_cast<int64_t>(imported))
                    .key("failed").value(static_cast<int64_t>(failed))
                    .key("import_time_ms").value(import_ms)
                    .key("optimization_time_ms").value(opt_ms)
                    .key("total_time_ms").value(import_ms + opt_ms)
                    .key("optimized").value(true)
                    .key("hash_mode").value(hash_mode_to_string(opt_stats.current_mode))
                    .key("total_keys").value(static_cast<int64_t>(opt_stats.total_keys))
                    .key("perfect_hash_memory").value(static_cast<int64_t>(opt_stats.perfect_hash_memory))
                    .key("message").value("Data imported and optimized with perfect hashing")
                    .end_object();
                res.set_content(json.str(), "application/json");
            } else {
                SimpleJSON json;
                json.begin_object()
                    .key("success").value(false)
                    .key("imported").value(static_cast<int64_t>(imported))
                    .key("failed").value(static_cast<int64_t>(failed))
                    .key("import_time_ms").value(import_ms)
                    .key("optimization_error").value(result.message)
                    .key("message").value("Data imported but optimization failed")
                    .end_object();
                res.status = 500;
                res.set_content(json.str(), "application/json");
            }
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // ===== PERFORMANCE BENCHMARKING =====
    
    // Benchmark endpoint comparing standard vs optimized performance
    svr.Post(R"(/stores/([^/]+)/benchmark)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        
        // Parse benchmark configuration
        int num_operations = 10000;
        bool include_optimization = false;
        
        size_t ops_pos = req.body.find("\"operations\":");
        if (ops_pos != std::string::npos) {
            size_t start = req.body.find(":", ops_pos) + 1;
            size_t end = req.body.find_first_of(",}", start);
            try {
                num_operations = std::stoi(req.body.substr(start, end - start));
            } catch (...) {}
        }
        
        if (req.body.find("\"include_optimization\":true") != std::string::npos) {
            include_optimization = true;
        }
        
        bool found = registry.with_store(name, [&](Maph& store, auto& entry) {
            SimpleJSON json;
            json.begin_object();
            
            // Generate test data
            std::vector<std::string> test_keys;
            std::vector<std::string> test_values;
            for (int i = 0; i < num_operations; ++i) {
                test_keys.push_back("{\"id\":" + std::to_string(i) + "}");
                test_values.push_back("{\"value\":" + std::to_string(i * 10) + "}");
            }
            
            // Populate database
            for (int i = 0; i < num_operations; ++i) {
                store.set(test_keys[i], test_values[i]);
            }
            
            // Benchmark standard mode
            auto standard_start = std::chrono::high_resolution_clock::now();
            int found_count = 0;
            for (int i = 0; i < num_operations; ++i) {
                if (store.get(test_keys[i])) found_count++;
            }
            auto standard_end = std::chrono::high_resolution_clock::now();
            auto standard_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                standard_end - standard_start).count();
            
            json.key("standard_performance").begin_object()
                .key("operations").value(num_operations)
                .key("found").value(found_count)
                .key("time_microseconds").value(standard_ms)
                .key("throughput_ops_per_sec").value(num_operations * 1000000.0 / standard_ms)
                .key("latency_ns_per_op").value(standard_ms * 1000 / num_operations)
                .end_object();
            
            if (include_optimization) {
                // Optimize
                auto opt_start = std::chrono::high_resolution_clock::now();
                auto result = store.optimize();
                auto opt_end = std::chrono::high_resolution_clock::now();
                auto opt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    opt_end - opt_start).count();
                
                if (result.ok()) {
                    // Benchmark optimized mode
                    auto optimized_start = std::chrono::high_resolution_clock::now();
                    found_count = 0;
                    for (int i = 0; i < num_operations; ++i) {
                        if (store.get(test_keys[i])) found_count++;
                    }
                    auto optimized_end = std::chrono::high_resolution_clock::now();
                    auto optimized_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                        optimized_end - optimized_start).count();
                    
                    entry.optimized = true;
                    entry.optimization_time_ms = opt_ms;
                    
                    json.key("optimization").begin_object()
                        .key("success").value(true)
                        .key("time_ms").value(opt_ms)
                        .end_object();
                    
                    json.key("optimized_performance").begin_object()
                        .key("operations").value(num_operations)
                        .key("found").value(found_count)
                        .key("time_microseconds").value(optimized_ms)
                        .key("throughput_ops_per_sec").value(num_operations * 1000000.0 / optimized_ms)
                        .key("latency_ns_per_op").value(optimized_ms * 1000 / num_operations)
                        .end_object();
                    
                    double speedup = static_cast<double>(standard_ms) / optimized_ms;
                    json.key("improvement").begin_object()
                        .key("speedup_factor").value(speedup)
                        .key("latency_reduction_percent").value((1.0 - 1.0/speedup) * 100)
                        .key("throughput_improvement_percent").value((speedup - 1.0) * 100)
                        .end_object();
                } else {
                    json.key("optimization").begin_object()
                        .key("success").value(false)
                        .key("error").value(result.message)
                        .end_object();
                }
            }
            
            json.end_object();
            res.set_content(json.str(), "application/json");
        });
        
        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Store not found\"}", "application/json");
        }
    });
    
    // Serve index.html with enhanced UI
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        std::ifstream file("index.html");
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
        } else {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
        }
    });
    
    std::cout << "maph v2 REST API server listening on port " << port << std::endl;
    std::cout << "Features: Perfect hashing, optimization workflow, enhanced benchmarking" << std::endl;
    std::cout << "Stores directory: ./maph_stores/" << std::endl;
    std::cout << "Web interface: http://localhost:" << port << "/" << std::endl;
    
    svr.listen("0.0.0.0", port);
    
    return 0;
}