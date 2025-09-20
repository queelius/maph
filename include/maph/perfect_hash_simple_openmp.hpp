/**
 * @file perfect_hash_simple_openmp.hpp
 * @brief Simplified OpenMP-optimized perfect hash implementation
 *
 * A clean, simplified version focusing on core OpenMP parallelization
 * without complex NUMA and template features.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <iomanip>

#ifdef MAPH_HAS_OPENMP
#include <omp.h>
#endif

#include <immintrin.h>

namespace maph {
namespace simple_openmp {

// Configuration
struct Config {
    size_t max_threads{0};  // 0 = auto
    bool enable_avx2{true};
    bool enable_parallel{true};
    size_t min_parallel_size{1000};
    uint64_t seed{42};
};

// Simple FNV-1a hash
inline uint64_t hash_fnv1a(std::string_view key, uint64_t seed = 0) {
    uint64_t hash = 0xcbf29ce484222325ULL ^ seed;
    for (unsigned char c : key) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Batch hash with OpenMP
inline void hash_batch_parallel(
    const std::vector<std::string_view>& keys,
    std::vector<uint64_t>& hashes,
    uint64_t seed,
    const Config& config
) {
    hashes.resize(keys.size());

#ifdef MAPH_HAS_OPENMP
    if (config.enable_parallel && keys.size() >= config.min_parallel_size) {
        size_t num_threads = config.max_threads;
        if (num_threads == 0) {
            num_threads = omp_get_max_threads();
        }

        #pragma omp parallel for num_threads(num_threads) schedule(static)
        for (size_t i = 0; i < keys.size(); ++i) {
            hashes[i] = hash_fnv1a(keys[i], seed);
        }
    } else {
        for (size_t i = 0; i < keys.size(); ++i) {
            hashes[i] = hash_fnv1a(keys[i], seed);
        }
    }
#else
    for (size_t i = 0; i < keys.size(); ++i) {
        hashes[i] = hash_fnv1a(keys[i], seed);
    }
#endif
}

// AVX2 batch hash for aligned data
#ifdef __AVX2__
inline void hash_batch_avx2(
    const std::vector<std::string_view>& keys,
    std::vector<uint64_t>& hashes,
    uint64_t seed
) {
    hashes.resize(keys.size());
    const __m256i prime = _mm256_set1_epi64x(0x100000001b3ULL);
    const __m256i offset = _mm256_set1_epi64x(0xcbf29ce484222325ULL ^ seed);

    size_t i = 0;
    // Process 4 keys at a time
    for (; i + 3 < keys.size(); i += 4) {
        __m256i hash_vec = offset;

        // Find minimum length for vectorization
        size_t min_len = keys[i].length();
        for (size_t j = 1; j < 4; ++j) {
            min_len = std::min(min_len, keys[i+j].length());
        }

        // Process common bytes with SIMD
        for (size_t pos = 0; pos < min_len; ++pos) {
            __m256i bytes = _mm256_set_epi64x(
                keys[i+3][pos], keys[i+2][pos],
                keys[i+1][pos], keys[i][pos]
            );
            hash_vec = _mm256_xor_si256(hash_vec, bytes);

            // Multiply using 32-bit operations
            __m256i lo = _mm256_mul_epu32(hash_vec, prime);
            __m256i hi = _mm256_mul_epu32(_mm256_srli_epi64(hash_vec, 32), prime);
            hash_vec = _mm256_add_epi64(lo, _mm256_slli_epi64(hi, 32));
        }

        // Store partial results
        alignas(32) uint64_t partial[4];
        _mm256_store_si256((__m256i*)partial, hash_vec);

        // Finish remaining bytes scalar
        for (size_t k = 0; k < 4; ++k) {
            for (size_t pos = min_len; pos < keys[i+k].length(); ++pos) {
                partial[k] ^= static_cast<unsigned char>(keys[i+k][pos]);
                partial[k] *= 0x100000001b3ULL;
            }
            hashes[i+k] = partial[k];
        }
    }

    // Handle remaining keys
    for (; i < keys.size(); ++i) {
        hashes[i] = hash_fnv1a(keys[i], seed);
    }
}
#endif

// Simple perfect hash implementation
class SimplePerfectHash {
private:
    std::vector<uint32_t> table_;
    std::vector<uint64_t> key_hashes_;
    size_t num_keys_{0};
    size_t table_size_{0};
    Config config_;

    struct Stats {
        double construction_ms{0};
        size_t collisions{0};
    } stats_;

public:
    explicit SimplePerfectHash(const Config& config = {}) : config_(config) {
#ifdef MAPH_HAS_OPENMP
        if (config_.max_threads == 0) {
            config_.max_threads = omp_get_max_threads();
        }
#endif
        // Check AVX2 support
        config_.enable_avx2 = config_.enable_avx2 && __builtin_cpu_supports("avx2");
    }

    bool build(const std::vector<std::string_view>& keys) {
        auto start = std::chrono::high_resolution_clock::now();

        num_keys_ = keys.size();
        if (num_keys_ == 0) return true;

        // Size table with some overhead
        table_size_ = num_keys_ * 2;
        // Round up to power of 2
        table_size_ = 1ULL << (64 - __builtin_clzll(table_size_ - 1));

        table_.clear();
        table_.resize(table_size_, UINT32_MAX);

        // Hash all keys
        key_hashes_.clear();
#ifdef __AVX2__
        if (config_.enable_avx2) {
            hash_batch_avx2(keys, key_hashes_, config_.seed);
        } else {
            hash_batch_parallel(keys, key_hashes_, config_.seed, config_);
        }
#else
        hash_batch_parallel(keys, key_hashes_, config_.seed, config_);
#endif

        // Insert with linear probing
        stats_.collisions = 0;
        for (size_t i = 0; i < num_keys_; ++i) {
            uint32_t slot = key_hashes_[i] & (table_size_ - 1);

            for (size_t probe = 0; probe < table_size_; ++probe) {
                uint32_t idx = (slot + probe) & (table_size_ - 1);

                if (table_[idx] == UINT32_MAX) {
                    table_[idx] = i;
                    break;
                }

                stats_.collisions++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        stats_.construction_ms = std::chrono::duration<double, std::milli>(end - start).count();

        return true;
    }

    std::optional<uint32_t> lookup(std::string_view key) const {
        if (table_.empty()) return std::nullopt;

        uint64_t hash = hash_fnv1a(key, config_.seed);
        uint32_t slot = hash & (table_size_ - 1);

        // Linear probe
        for (size_t probe = 0; probe < 10; ++probe) {
            uint32_t idx = (slot + probe) & (table_size_ - 1);
            uint32_t entry = table_[idx];

            if (entry == UINT32_MAX) {
                return std::nullopt;
            }

            if (entry < num_keys_ && key_hashes_[entry] == hash) {
                return entry;
            }
        }

        return std::nullopt;
    }

    void lookup_batch(
        const std::vector<std::string_view>& keys,
        std::vector<std::optional<uint32_t>>& results
    ) const {
        results.resize(keys.size());

        if (table_.empty()) {
            std::fill(results.begin(), results.end(), std::nullopt);
            return;
        }

        // Hash all keys first
        std::vector<uint64_t> hashes;
#ifdef __AVX2__
        if (config_.enable_avx2) {
            hash_batch_avx2(keys, hashes, config_.seed);
        } else {
            hash_batch_parallel(keys, hashes, config_.seed, config_);
        }
#else
        hash_batch_parallel(keys, hashes, config_.seed, config_);
#endif

        // Perform lookups
#ifdef MAPH_HAS_OPENMP
        if (config_.enable_parallel && keys.size() >= config_.min_parallel_size) {
            #pragma omp parallel for schedule(static)
            for (size_t i = 0; i < keys.size(); ++i) {
                uint32_t slot = hashes[i] & (table_size_ - 1);
                results[i] = std::nullopt;

                for (size_t probe = 0; probe < 10; ++probe) {
                    uint32_t idx = (slot + probe) & (table_size_ - 1);
                    uint32_t entry = table_[idx];

                    if (entry == UINT32_MAX) {
                        break;
                    }

                    if (entry < num_keys_ && key_hashes_[entry] == hashes[i]) {
                        results[i] = entry;
                        break;
                    }
                }
            }
        } else {
            for (size_t i = 0; i < keys.size(); ++i) {
                uint32_t slot = hashes[i] & (table_size_ - 1);
                results[i] = std::nullopt;

                for (size_t probe = 0; probe < 10; ++probe) {
                    uint32_t idx = (slot + probe) & (table_size_ - 1);
                    uint32_t entry = table_[idx];

                    if (entry == UINT32_MAX) {
                        break;
                    }

                    if (entry < num_keys_ && key_hashes_[entry] == hashes[i]) {
                        results[i] = entry;
                        break;
                    }
                }
            }
        }
#else
        for (size_t i = 0; i < keys.size(); ++i) {
            results[i] = lookup(keys[i]);
        }
#endif
    }

    size_t memory_usage() const {
        return sizeof(*this) +
               table_.capacity() * sizeof(uint32_t) +
               key_hashes_.capacity() * sizeof(uint64_t);
    }

    double load_factor() const {
        return static_cast<double>(num_keys_) / table_size_;
    }

    void print_stats() const {
        std::cout << "SimplePerfectHash Statistics:\n"
                  << "  Keys: " << num_keys_ << "\n"
                  << "  Table size: " << table_size_ << "\n"
                  << "  Load factor: " << std::fixed << std::setprecision(2)
                  << (load_factor() * 100) << "%\n"
                  << "  Memory: " << (memory_usage() / 1024.0 / 1024.0) << " MB\n"
                  << "  Construction: " << stats_.construction_ms << " ms\n"
                  << "  Collisions: " << stats_.collisions << "\n"
#ifdef MAPH_HAS_OPENMP
                  << "  Threads: " << config_.max_threads << "\n"
#endif
                  << "  AVX2: " << (config_.enable_avx2 ? "Yes" : "No") << "\n";
    }
};

} // namespace simple_openmp
} // namespace maph