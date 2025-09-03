#pragma once

#include <vector>
#include <unordered_map>
#include <functional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

/**
 * Python-compatible perfect hash function wrapper
 */
class PyPerfectHash {
public:
    struct PyHasher {
        using hash_type = std::size_t;
        
        std::size_t operator()(py::object x) const {
            return py::hash(x);
        }
    };
    
    using iterator = std::vector<py::object>::const_iterator;
    
private:
    std::unordered_map<std::size_t, std::size_t> perfect_map_;
    std::size_t max_hash_;
    double error_rate_;
    PyHasher hasher_;
    
public:
    PyPerfectHash() : max_hash_(0), error_rate_(0.0) {}
    
    PyPerfectHash(const std::vector<py::object>& elements, double error_rate = 0.0)
        : error_rate_(error_rate) {
        std::size_t index = 0;
        for (const auto& elem : elements) {
            std::size_t hash_val = py::hash(elem);
            
            // Simulate imperfect hashing with error_rate
            if (hash_val % 100 < error_rate * 100) {
                // Collision - map to existing index
                if (index > 0) {
                    perfect_map_[hash_val] = index - 1;
                } else {
                    perfect_map_[hash_val] = 0;
                }
            } else {
                perfect_map_[hash_val] = index++;
            }
        }
        max_hash_ = (index > 0) ? index - 1 : 0;
    }
    
    std::size_t operator()(py::object x) const {
        std::size_t hash_val = py::hash(x);
        auto it = perfect_map_.find(hash_val);
        if (it != perfect_map_.end()) {
            return it->second;
        }
        // Return a hash for unknown elements
        return hash_val % (max_hash_ + 1);
    }
    
    std::size_t max_hash() const {
        return max_hash_;
    }
    
    double error_rate() const {
        return error_rate_;
    }
    
    PyHasher hash_fn() const {
        return hasher_;
    }
    
    bool operator==(const PyPerfectHash& other) const {
        return perfect_map_ == other.perfect_map_ && 
               max_hash_ == other.max_hash_ &&
               error_rate_ == other.error_rate_;
    }
};

/**
 * Builder for PyPerfectHash
 */
class PyPerfectHashBuilder {
private:
    double error_rate_;
    
public:
    explicit PyPerfectHashBuilder(double error_rate = 0.0)
        : error_rate_(error_rate) {}
    
    PyPerfectHash operator()(std::vector<py::object>::const_iterator begin,
                            std::vector<py::object>::const_iterator end) const {
        std::vector<py::object> elements(begin, end);
        return PyPerfectHash(elements, error_rate_);
    }
    
    void set_error_rate(double rate) {
        error_rate_ = rate;
    }
    
    double get_error_rate() const {
        return error_rate_;
    }
};