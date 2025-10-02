/**
 * @file maph_server_v3.cpp
 * @brief REST API server for maph v3
 *
 * Provides a REST API interface to maph v3 databases with:
 * - Multi-store management with thread-safe access
 * - Standard CRUD operations (GET, PUT, DELETE)
 * - Statistics and monitoring endpoints
 * - CORS support for web clients
 *
 * API Endpoints:
 *   GET    /stores                       - List all stores
 *   POST   /stores/{name}                - Create a new store
 *   GET    /stores/{name}/stats          - Get store statistics
 *   GET    /stores/{name}/keys/{key}     - Get key value
 *   PUT    /stores/{name}/keys/{key}     - Set key value (body = value)
 *   DELETE /stores/{name}/keys/{key}     - Delete key
 *
 * Usage:
 *   ./maph_server_v3 [port]             - Start server (default port 8080)
 *
 * Example:
 *   ./maph_server_v3 9090
 *   curl http://localhost:9090/stores
 *   curl -X PUT -d 'hello world' http://localhost:9090/stores/test/keys/greeting
 *   curl http://localhost:9090/stores/test/keys/greeting
 */

#include "maph/v3/maph.hpp"
#include <microhttpd.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <iostream>

using maph_db = maph::v3::maph;

// ===== JSON HELPERS =====

std::string json_escape(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 32) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string json_error(const std::string& message) {
    return "{\"error\":\"" + json_escape(message) + "\"}";
}

std::string json_success(const std::string& message = "OK") {
    return "{\"success\":true,\"message\":\"" + json_escape(message) + "\"}";
}

std::string json_value(const std::string& value) {
    return "{\"value\":\"" + json_escape(value) + "\"}";
}

// ===== STORE REGISTRY =====

/**
 * @class StoreRegistry
 * @brief Thread-safe registry of maph stores
 *
 * Manages multiple maph database instances with concurrent access.
 * Uses shared_mutex for reader-writer locking (multiple readers, single writer).
 */
class StoreRegistry {
private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, maph_db> stores_;
    std::filesystem::path data_dir_;

public:
    explicit StoreRegistry(const std::filesystem::path& data_dir = "data")
        : data_dir_(data_dir) {
        std::filesystem::create_directories(data_dir_);
    }

    /**
     * Create a new store
     * @param name Store name
     * @param slots Number of slots (default: 10,000)
     * @return Error message on failure, empty string on success
     */
    std::string create_store(const std::string& name, uint64_t slots = 10000) {
        std::unique_lock lock(mutex_);

        if (stores_.find(name) != stores_.end()) {
            return "Store already exists";
        }

        auto path = data_dir_ / (name + ".maph");
        maph_db::config cfg{maph::v3::slot_count{slots}};
        cfg.enable_journal = true;
        cfg.max_probes = 20;

        auto result = maph_db::create(path, cfg);
        if (!result) {
            return "Failed to create store";
        }

        stores_.emplace(name, std::move(*result));
        return "";
    }

    /**
     * Open an existing store
     */
    std::string open_store(const std::string& name, bool readonly = false) {
        std::unique_lock lock(mutex_);

        if (stores_.find(name) != stores_.end()) {
            return "Store already open";
        }

        auto path = data_dir_ / (name + ".maph");
        auto result = maph_db::open(path, readonly);
        if (!result) {
            return "Failed to open store";
        }

        stores_.emplace(name, std::move(*result));
        return "";
    }

    /**
     * Execute a function with read access to a store
     */
    template<typename F>
    std::string with_store_read(const std::string& name, F&& func) {
        std::shared_lock lock(mutex_);
        auto it = stores_.find(name);
        if (it == stores_.end()) {
            return json_error("Store not found");
        }
        return func(it->second);
    }

    /**
     * Execute a function with write access to a store
     */
    template<typename F>
    std::string with_store_write(const std::string& name, F&& func) {
        std::shared_lock lock(mutex_);  // Still shared - maph handles internal locking
        auto it = stores_.find(name);
        if (it == stores_.end()) {
            return json_error("Store not found");
        }
        return func(it->second);
    }

    /**
     * List all stores with their statistics
     */
    std::string list_stores() {
        std::shared_lock lock(mutex_);
        std::ostringstream json;
        json << "[";
        bool first = true;
        for (const auto& [name, store] : stores_) {
            if (!first) json << ",";
            first = false;
            json << "{"
                 << "\"name\":\"" << json_escape(name) << "\","
                 << "\"size\":" << store.size() << ","
                 << "\"load_factor\":" << std::fixed << std::setprecision(3)
                 << store.load_factor()
                 << "}";
        }
        json << "]";
        return json.str();
    }

    /**
     * Optimize store to perfect hash
     * @param name Store name
     * @return Error message on failure, empty string on success
     */
    std::string optimize_store(const std::string& name) {
        std::shared_lock lock(mutex_);  // Read lock - maph handles internal locking
        auto it = stores_.find(name);
        if (it == stores_.end()) {
            return "Store not found";
        }

        auto result = it->second.optimize();
        if (!result) {
            return "Failed to optimize store";
        }

        return "";
    }
};

// ===== GLOBAL STATE =====

StoreRegistry registry;

// ===== HTTP REQUEST HANDLER =====

/**
 * Handle incoming HTTP requests
 *
 * This is a stateless request handler called by libmicrohttpd for each request.
 * POST/PUT request data is handled through upload_data callback.
 */
enum MHD_Result request_handler(void *cls,
                               struct MHD_Connection *connection,
                               const char *url,
                               const char *method,
                               const char *version,
                               const char *upload_data,
                               size_t *upload_data_size,
                               void **con_cls) {

    // Handle POST/PUT data upload
    static int dummy;
    if (*con_cls == nullptr) {
        *con_cls = &dummy;
        return MHD_YES;
    }

    std::string response;
    int status = MHD_HTTP_OK;

    std::string path(url);
    std::string http_method(method);

    // Route requests
    if (path == "/stores" && http_method == "GET") {
        // List all stores
        response = registry.list_stores();
    }
    else if (path == "/stores" && http_method == "POST") {
        // Create new store (expects query params or form data)
        response = json_error("Store creation requires name parameter");
        status = MHD_HTTP_BAD_REQUEST;
    }
    else if (path.find("/stores/") == 0) {
        // Extract store name and sub-path
        size_t slash_pos = path.find('/', 8);  // After "/stores/"
        std::string store_name;
        std::string sub_path;

        if (slash_pos == std::string::npos) {
            store_name = path.substr(8);
        } else {
            store_name = path.substr(8, slash_pos - 8);
            sub_path = path.substr(slash_pos);
        }

        // Handle store-specific endpoints
        if (sub_path.empty() && http_method == "POST") {
            // Create this store
            auto err = registry.create_store(store_name);
            if (err.empty()) {
                response = json_success("Store created");
                status = MHD_HTTP_CREATED;
            } else {
                response = json_error(err);
                status = MHD_HTTP_BAD_REQUEST;
            }
        }
        else if (sub_path == "/stats" && http_method == "GET") {
            // Get store statistics
            response = registry.with_store_read(store_name, [](const maph_db& store) {
                std::ostringstream json;
                json << "{"
                     << "\"size\":" << store.size() << ","
                     << "\"load_factor\":" << std::fixed << std::setprecision(3)
                     << store.load_factor()
                     << "}";
                return json.str();
            });
        }
        else if (sub_path == "/optimize" && http_method == "POST") {
            // Optimize store to perfect hash
            auto err = registry.optimize_store(store_name);
            if (err.empty()) {
                response = json_success("Store optimized to perfect hash");
                status = MHD_HTTP_OK;
            } else {
                response = json_error(err);
                status = (err == "Store not found") ? MHD_HTTP_NOT_FOUND : MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
        else if (sub_path.find("/keys/") == 0) {
            // Key operations
            std::string key = sub_path.substr(6);  // Remove "/keys/"

            if (http_method == "GET") {
                // Get key value
                response = registry.with_store_read(store_name, [&key](const maph_db& store) {
                    auto result = store.get(key);
                    if (result) {
                        return json_value(std::string(*result));
                    } else {
                        return json_error("Key not found");
                    }
                });
                if (response.find("error") != std::string::npos) {
                    status = MHD_HTTP_NOT_FOUND;
                }
            }
            else if (http_method == "PUT") {
                // Set key value (value in request body)
                if (*upload_data_size > 0) {
                    std::string value(upload_data, *upload_data_size);
                    *upload_data_size = 0;  // Mark as consumed

                    response = registry.with_store_write(store_name, [&key, &value](maph_db& store) {
                        auto result = store.set(key, value);
                        if (result) {
                            return json_success("Key set");
                        } else {
                            return json_error("Failed to set key");
                        }
                    });
                } else {
                    response = json_error("No value provided");
                    status = MHD_HTTP_BAD_REQUEST;
                }
            }
            else if (http_method == "DELETE") {
                // Delete key
                response = registry.with_store_write(store_name, [&key](maph_db& store) {
                    auto result = store.remove(key);
                    if (result) {
                        return json_success("Key deleted");
                    } else {
                        return json_error("Key not found");
                    }
                });
                if (response.find("error") != std::string::npos) {
                    status = MHD_HTTP_NOT_FOUND;
                }
            }
            else {
                response = json_error("Method not allowed");
                status = MHD_HTTP_METHOD_NOT_ALLOWED;
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

    // Create HTTP response
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

// ===== MAIN =====

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;

    // Create a default test store
    std::cout << "Initializing maph v3 REST API server...\n";
    auto err = registry.create_store("test", 10000);
    if (!err.empty()) {
        std::cerr << "Warning: Failed to create test store: " << err << "\n";
    }

    // Start HTTP daemon
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

    std::cout << "\n✓ maph v3 REST API server running on port " << port << "\n\n";
    std::cout << "API Endpoints:\n";
    std::cout << "  GET    /stores                       - List all stores\n";
    std::cout << "  POST   /stores/{name}                - Create a new store\n";
    std::cout << "  GET    /stores/{name}/stats          - Get store statistics\n";
    std::cout << "  POST   /stores/{name}/optimize       - Optimize to perfect hash\n";
    std::cout << "  GET    /stores/{name}/keys/{key}     - Get key value\n";
    std::cout << "  PUT    /stores/{name}/keys/{key}     - Set key value (body = value)\n";
    std::cout << "  DELETE /stores/{name}/keys/{key}     - Delete key\n";
    std::cout << "\nExample Usage:\n";
    std::cout << "  curl http://localhost:" << port << "/stores\n";
    std::cout << "  curl -X PUT -d 'hello world' http://localhost:" << port << "/stores/test/keys/greeting\n";
    std::cout << "  curl http://localhost:" << port << "/stores/test/keys/greeting\n";
    std::cout << "\nPerfect Hash Optimization:\n";
    std::cout << "  # 1. Create store and insert keys\n";
    std::cout << "  curl -X POST http://localhost:" << port << "/stores/mydb\n";
    std::cout << "  curl -X PUT -d 'value1' http://localhost:" << port << "/stores/mydb/keys/key1\n";
    std::cout << "  curl -X PUT -d 'value2' http://localhost:" << port << "/stores/mydb/keys/key2\n";
    std::cout << "  # 2. Optimize to perfect hash for O(1) guaranteed lookups\n";
    std::cout << "  curl -X POST http://localhost:" << port << "/stores/mydb/optimize\n";
    std::cout << "\nPress Enter to stop server...\n";

    getchar();  // Wait for user input

    MHD_stop_daemon(daemon);
    std::cout << "Server stopped.\n";
    return 0;
}
