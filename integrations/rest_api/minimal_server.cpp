/**
 * @file minimal_server.cpp
 * @brief Minimal REST API server for maph with clean perfect hashing
 * 
 * A very simple HTTP server demonstration for maph operations.
 * Uses basic socket programming - production use should use a proper HTTP library.
 */

#include <maph/maph.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>

using namespace maph;

// Simple HTTP response builder
std::string http_response(int status, const std::string& body, const std::string& content_type = "application/json") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " OK\\r\\n";
    response << "Content-Type: " << content_type << "\\r\\n";
    response << "Content-Length: " << body.length() << "\\r\\n";
    response << "Access-Control-Allow-Origin: *\\r\\n";
    response << "Connection: close\\r\\n";
    response << "\\r\\n";
    response << body;
    return response.str();
}

std::string json_error(const std::string& message) {
    return "{\"error\":\"" + message + "\"}";
}

std::string json_success(const std::string& message = "OK") {
    return "{\"success\":true,\"message\":\"" + message + "\"}";
}

// Global store for demo
std::unique_ptr<Maph> demo_store;

void handle_client(int client_socket) {
    char buffer[4096];
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_read] = '\\0';
    std::string request(buffer);
    
    // Parse the HTTP request line
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    std::string response_body;
    int status = 200;
    
    try {
        if (path == "/stats") {
            // Get database statistics
            auto stats = demo_store->stats();
            std::ostringstream json;
            json << "{"
                 << "\\"total_slots\\":" << stats.total_slots << ","
                 << "\\"used_slots\\":" << stats.used_slots << ","
                 << "\\"load_factor\\":" << stats.load_factor << ","
                 << "\\"optimized\\":" << (stats.is_optimized ? "true" : "false") << ","
                 << "\\"perfect_hash_keys\\":" << stats.perfect_hash_keys << ","
                 << "\\"journal_entries\\":" << stats.journal_entries << ","
                 << "\\"collision_rate\\":" << stats.collision_rate
                 << "}";
            response_body = json.str();
        }
        else if (path == "/optimize" && method == "POST") {
            // Optimize database with perfect hashing
            auto result = demo_store->optimize();
            if (result.ok()) {
                response_body = json_success(result.message);
            } else {
                response_body = json_error(result.message);
                status = 500;
            }
        }
        else if (path.find("/key/") == 0) {
            std::string key = path.substr(5);  // Remove "/key/"
            
            if (method == "GET") {
                auto value = demo_store->get(key);
                if (value) {
                    response_body = std::string(*value);
                } else {
                    response_body = json_error("Key not found");
                    status = 404;
                }
            }
            else if (method == "POST" || method == "PUT") {
                // For demo, just use the key as value
                std::string value = "{\\"demo_value\\": \\"" + key + "\\"}";
                if (demo_store->set(key, value)) {
                    response_body = json_success("Key set");
                } else {
                    response_body = json_error("Failed to set key");
                    status = 500;
                }
            }
            else if (method == "DELETE") {
                if (demo_store->remove(key)) {
                    response_body = json_success("Key removed");
                } else {
                    response_body = json_error("Key not found");
                    status = 404;
                }
            }
        }
        else if (path == "/") {
            // Simple web interface
            response_body = R"(<!DOCTYPE html>
<html>
<head><title>Maph Demo</title></head>
<body>
    <h1>Maph Perfect Hash Demo</h1>
    <h2>Operations</h2>
    <button onclick="getStats()">Get Stats</button>
    <button onclick="optimize()">Optimize</button>
    <button onclick="setKey()">Set Demo Key</button>
    <button onclick="getKey()">Get Demo Key</button>
    <div id="output" style="margin-top: 20px; padding: 10px; border: 1px solid #ccc; min-height: 100px;"></div>
    
    <script>
    function output(text) {
        document.getElementById('output').innerHTML = '<pre>' + text + '</pre>';
    }
    
    function getStats() {
        fetch('/stats')
            .then(response => response.json())
            .then(data => output(JSON.stringify(data, null, 2)))
            .catch(error => output('Error: ' + error));
    }
    
    function optimize() {
        fetch('/optimize', {method: 'POST'})
            .then(response => response.json())
            .then(data => output(JSON.stringify(data, null, 2)))
            .catch(error => output('Error: ' + error));
    }
    
    function setKey() {
        fetch('/key/demo_key', {method: 'POST'})
            .then(response => response.json())
            .then(data => output(JSON.stringify(data, null, 2)))
            .catch(error => output('Error: ' + error));
    }
    
    function getKey() {
        fetch('/key/demo_key')
            .then(response => response.json())
            .then(data => output(JSON.stringify(data, null, 2)))
            .catch(error => output('Error: ' + error));
    }
    </script>
</body>
</html>)";
        }
        else {
            response_body = json_error("Endpoint not found");
            status = 404;
        }
    } catch (const std::exception& e) {
        response_body = json_error("Server error: " + std::string(e.what()));
        status = 500;
    }
    
    std::string http_resp = http_response(status, response_body, 
                                         path == "/" ? "text/html" : "application/json");
    
    send(client_socket, http_resp.c_str(), http_resp.length(), 0);
    close(client_socket);
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    
    // Create demo database
    demo_store = Maph::create("/tmp/maph_demo.db", 10000);
    if (!demo_store) {
        std::cerr << "Failed to create demo database" << std::endl;
        return 1;
    }
    
    // Add some sample data
    demo_store->set("user1", "{\\"name\\": \\"Alice\\", \\"age\\": 30}");
    demo_store->set("user2", "{\\"name\\": \\"Bob\\", \\"age\\": 25}");
    demo_store->set("user3", "{\\"name\\": \\"Charlie\\", \\"age\\": 35}");
    
    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind socket to port " << port << std::endl;
        close(server_socket);
        return 1;
    }
    
    if (listen(server_socket, 10) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_socket);
        return 1;
    }
    
    std::cout << "Maph demo server listening on port " << port << std::endl;
    std::cout << "Open http://localhost:" << port << " in your browser" << std::endl;
    std::cout << "API endpoints:" << std::endl;
    std::cout << "  GET /stats - Database statistics" << std::endl;
    std::cout << "  POST /optimize - Enable perfect hashing" << std::endl;
    std::cout << "  GET /key/{key} - Get key value" << std::endl;
    std::cout << "  POST /key/{key} - Set key value" << std::endl;
    std::cout << "  DELETE /key/{key} - Delete key" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop server" << std::endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket >= 0) {
            // Handle each client in a separate thread for simplicity
            std::thread client_thread(handle_client, client_socket);
            client_thread.detach();
        }
    }
    
    close(server_socket);
    return 0;
}