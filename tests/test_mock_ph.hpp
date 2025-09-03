#pragma once

#include <unordered_map>
#include <functional>
#include <vector>
#include <algorithm>

/**
 * Mock perfect hash function for testing
 */
template <typename T>
class MockPerfectHash {
public:
    struct H {
        using hash_type = std::size_t;
        
        std::size_t operator()(const T& x) const {
            return std::hash<T>{}(x);
        }
    };
    
    using iterator = typename std::vector<T>::const_iterator;
    
private:
    std::unordered_map<T, std::size_t> perfect_map_;
    std::size_t max_hash_;
    double error_rate_;
    H hasher_;
    
public:
    MockPerfectHash() : max_hash_(0), error_rate_(0.0) {}
    
    template <typename I>
    MockPerfectHash(I begin, I end, double error_rate = 0.0)
        : error_rate_(error_rate) {
        std::size_t index = 0;
        for (auto it = begin; it != end; ++it) {
            // Simulate imperfect hashing with error_rate
            if (std::hash<T>{}(*it) % 100 < error_rate * 100) {
                // Collision - map to existing index
                if (index > 0) {
                    perfect_map_[*it] = index - 1;
                } else {
                    perfect_map_[*it] = 0;
                }
            } else {
                perfect_map_[*it] = index++;
            }
        }
        max_hash_ = (index > 0) ? index - 1 : 0;
    }
    
    std::size_t operator()(const T& x) const {
        auto it = perfect_map_.find(x);
        if (it != perfect_map_.end()) {
            return it->second;
        }
        // Return a hash for unknown elements
        return std::hash<T>{}(x) % (max_hash_ + 1);
    }
    
    std::size_t max_hash() const {
        return max_hash_;
    }
    
    double error_rate() const {
        return error_rate_;
    }
    
    H hash_fn() const {
        return hasher_;
    }
    
    bool operator==(const MockPerfectHash& other) const {
        return perfect_map_ == other.perfect_map_ && 
               max_hash_ == other.max_hash_ &&
               error_rate_ == other.error_rate_;
    }
};

/**
 * Builder for MockPerfectHash
 */
template <typename T>
class MockPerfectHashBuilder {
private:
    double error_rate_;
    
public:
    explicit MockPerfectHashBuilder(double error_rate = 0.0)
        : error_rate_(error_rate) {}
    
    template <typename I>
    MockPerfectHash<T> operator()(I begin, I end) const {
        return MockPerfectHash<T>(begin, end, error_rate_);
    }
};