/**
 * @file simple_maph_server.cpp
 * @brief Simple REST API server for maph with clean perfect hashing
 * 
 * Provides a straightforward REST API for the new maph system including:
 * - Standard CRUD operations
 * - Perfect hash optimization endpoint
 * - Simple metrics and statistics
 */

#include <maph/maph.hpp>
#include <microhttpd.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <sstream>
#include <json/json.h>

using namespace maph;

// Simple JSON response builder
std::string json_response(const std::string& key, const std::string& value) {
    return "{\"" + key + "\":\"" + value + "\"}";
}

std::string json_error(const std::string& message) {
    return "{\"error\":\"" + message + "\"}";
}

std::string json_success(const std::string& message = "OK") {
    return "{\"success\":true,\"message\":\"" + message + "\"}";
}

// Simple store registry
class SimpleStoreRegistry {
private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Maph>> stores_;
    
public:
    bool create_store(const std::string& name, uint64_t slots) {
        std::unique_lock lock(mutex_);
        if (stores_.find(name) != stores_.end()) {
            return false;
        }
        
        auto store = Maph::create("data/" + name + ".maph", slots);
        if (!store) return false;
        
        stores_[name] = std::move(store);
        return true;
    }
    
    template<typename F>
    std::string with_store(const std::string& name, F&& func) {
        std::shared_lock lock(mutex_);
        auto it = stores_.find(name);
        if (it == stores_.end()) {
            return json_error("Store not found");
        }
        return func(*it->second);
    }
    
    std::string list_stores() {
        std::shared_lock lock(mutex_);
        std::ostringstream json;
        json << "[";
        bool first = true;
        for (const auto& [name, store] : stores_) {
            if (!first) json << ",";
            first = false;
            auto stats = store->stats();
            json << "{"
                 << "\"name\":\"" << name << "\","
                 << "\"slots\":" << stats.total_slots << ","
                 << "\"used\":" << stats.used_slots << ","
                 << "\"optimized\":" << (stats.is_optimized ? "true" : "false")
                 << "}";
        }
        json << "]";
        return json.str();
    }
};

SimpleStoreRegistry registry;

// HTTP request handler
enum MHD_Result request_handler(void *cls,
                               struct MHD_Connection *connection,
                               const char *url,
                               const char *method,
                               const char *version,
                               const char *upload_data,
                               size_t *upload_data_size,
                               void **con_cls) {
    
    std::string response;
    int status = MHD_HTTP_OK;
    
    std::string path(url);
    std::string http_method(method);
    
    // Parse URL path
    if (path == "/stores" && http_method == "GET") {
        response = registry.list_stores();
    }
    else if (path == "/stores" && http_method == "POST") {
        // Simple store creation - expect name and slots in URL params
        response = json_success("Store creation endpoint");
    }
    else if (path.find("/stores/") == 0) {
        // Extract store name
        size_t slash_pos = path.find('/', 8);  // After "/stores/"
        std::string store_name;
        std::string sub_path;
        
        if (slash_pos == std::string::npos) {
            store_name = path.substr(8);
        } else {
            store_name = path.substr(8, slash_pos - 8);
            sub_path = path.substr(slash_pos);
        }
        
        if (sub_path == "/stats") {
            response = registry.with_store(store_name, [](Maph& store) {
                auto stats = store.stats();
                std::ostringstream json;
                json << "{"
                     << "\"total_slots\":" << stats.total_slots << ","
                     << "\"used_slots\":" << stats.used_slots << ","
                     << "\"load_factor\":" << stats.load_factor << ","
                     << "\"optimized\":" << (stats.is_optimized ? "true" : "false") << ","
                     << "\"perfect_hash_keys\":" << stats.perfect_hash_keys << ","
                     << "\"journal_entries\":" << stats.journal_entries << ","
                     << "\"collision_rate\":" << stats.collision_rate
                     << "}";
                return json.str();
            });
        }
        else if (sub_path == "/optimize" && http_method == "POST") {
            response = registry.with_store(store_name, [](Maph& store) {
                auto result = store.optimize();
                if (result.ok()) {
                    return json_success(result.message);
                } else {
                    return json_error(result.message);
                }
            });
        }
        else if (sub_path.find("/keys/") == 0) {
            std::string key = sub_path.substr(6);  // Remove "/keys/"
            
            if (http_method == "GET") {
                response = registry.with_store(store_name, [&key](Maph& store) {
                    auto value = store.get(key);
                    if (value) {
                        return std::string(*value);
                    } else {
                        return json_error("Key not found");
                    }
                });
                if (response.find("error") != std::string::npos) {
                    status = MHD_HTTP_NOT_FOUND;
                }
            }
            else if (http_method == "PUT") {
                std::string value(upload_data, *upload_data_size);
                response = registry.with_store(store_name, [&key, &value](Maph& store) {
                    if (store.set(key, value)) {
                        return json_success("Key set");
                    } else {
                        return json_error("Failed to set key");
                    }
                });
            }
            else if (http_method == "DELETE") {
                response = registry.with_store(store_name, [&key](Maph& store) {
                    if (store.remove(key)) {
                        return json_success("Key removed");
                    } else {
                        return json_error("Key not found");
                    }
                });
            }
        }
        else {
            response = json_error("Unknown endpoint");
            status = MHD_HTTP_NOT_FOUND;
        }
    }
    else {
        response = json_error("Not found");
        status = MHD_HTTP_NOT_FOUND;
    }
    
    // Create response
    struct MHD_Response *mhd_response = MHD_create_response_from_buffer(
        response.length(),
        (void*)response.c_str(),
        MHD_RESPMEM_MUST_COPY
    );
    
    MHD_add_response_header(mhd_response, "Content-Type", "application/json");
    MHD_add_response_header(mhd_response, "Access-Control-Allow-Origin", "*");
    
    enum MHD_Result ret = MHD_queue_response(connection, status, mhd_response);
    MHD_destroy_response(mhd_response);
    
    return ret;
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    
    // Create data directory
    system("mkdir -p data");
    
    // Create a sample store for testing
    registry.create_store("test", 10000);
    
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        port,
        nullptr,
        nullptr,
        &request_handler,
        nullptr,
        MHD_OPTION_END
    );
    
    if (!daemon) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }
    
    std::cout << "Simple maph REST API server listening on port " << port << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /stores - List all stores" << std::endl;
    std::cout << "  GET /stores/{name}/stats - Get store statistics" << std::endl;
    std::cout << "  POST /stores/{name}/optimize - Optimize store with perfect hashing" << std::endl;
    std::cout << "  GET /stores/{name}/keys/{key} - Get key value" << std::endl;
    std::cout << "  PUT /stores/{name}/keys/{key} - Set key value" << std::endl;
    std::cout << "  DELETE /stores/{name}/keys/{key} - Delete key" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Enter to stop server..." << std::endl;
    
    getchar();  // Wait for user input
    
    MHD_stop_daemon(daemon);
    return 0;
}