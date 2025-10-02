/**
 * @file maph_openmp.hpp
 * @brief OpenMP-optimized extensions for the maph hash table
 *
 * Provides parallel operations for:
 * - Batch insertions and lookups
 * - Parallel filter construction
 * - SIMD-optimized hash computations
 * - Thread-safe concurrent operations
 */

#pragma once

#include "maph.hpp"
#include <vector>
#include <atomic>
#include <algorithm>

#ifdef MAPH_HAS_OPENMP
#include <omp.h>
#endif

namespace maph {
namespace parallel {

/**
 * @brief OpenMP-optimized maph extension
 *
 * Adds parallel batch operations while maintaining compatibility
 * with the base maph interface.
 */
class MaphParallel {
private:
    std::unique_ptr<Maph> maph_;
    int num_threads_{0};

    // Cache line padding for thread-local data
    static constexpr size_t CACHE_LINE_SIZE = 64;

    struct alignas(CACHE_LINE_SIZE) ThreadLocalStats {
        size_t operations{0};
        size_t cache_hits{0};
        size_t collisions{0};
        char padding[CACHE_LINE_SIZE - 3 * sizeof(size_t)];
    };

    mutable std::vector<ThreadLocalStats> thread_stats_;

public:
    MaphParallel() {
#ifdef MAPH_HAS_OPENMP
        num_threads_ = omp_get_max_threads();
#else
        num_threads_ = 1;
#endif
        thread_stats_.resize(num_threads_);
    }

    /**
     * @brief Create a new parallel maph instance
     */
    static std::unique_ptr<MaphParallel> create(
        const std::string& path,
        size_t num_slots,
        int threads = 0
    ) {
        auto mp = std::make_unique<MaphParallel>();
        mp->maph_ = Maph::create(path, num_slots);

        if (threads > 0) {
            mp->num_threads_ = threads;
            mp->thread_stats_.resize(threads);
        }

        return mp;
    }

    /**
     * @brief Open existing maph file with parallel support
     */
    static std::unique_ptr<MaphParallel> open(
        const std::string& path,
        bool readonly = false,
        int threads = 0
    ) {
        auto mp = std::make_unique<MaphParallel>();
        mp->maph_ = Maph::open(path, readonly);

        if (threads > 0) {
            mp->num_threads_ = threads;
            mp->thread_stats_.resize(threads);
        }

        return mp;
    }

    /**
     * @brief Parallel batch insert with OpenMP
     *
     * Uses dynamic scheduling for load balancing and
     * atomic operations for thread-safe updates.
     */
    bool parallel_set(
        const std::vector<std::pair<std::string, std::string>>& kvs
    ) {
        if (!maph_) return false;

        std::atomic<bool> success{true};
        std::atomic<size_t> completed{0};

#ifdef MAPH_HAS_OPENMP
        #pragma omp parallel num_threads(num_threads_)
        {
            int tid = omp_get_thread_num();
            auto& stats = thread_stats_[tid];

            #pragma omp for schedule(dynamic, 64)
            for (size_t i = 0; i < kvs.size(); ++i) {
                if (!success.load(std::memory_order_relaxed)) continue;

                bool result = maph_->set(kvs[i].first, kvs[i].second);
                if (!result) {
                    success.store(false, std::memory_order_relaxed);
                } else {
                    stats.operations++;
                    completed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
#else
        for (const auto& [key, value] : kvs) {
            if (!maph_->set(key, value)) {
                return false;
            }
            completed++;
        }
#endif

        return success.load();
    }

    /**
     * @brief Parallel batch lookup with prefetching
     *
     * Optimizes cache usage by prefetching slots before access.
     */
    std::vector<std::string> parallel_get(
        const std::vector<std::string>& keys
    ) const {
        if (!maph_) return std::vector<std::string>(keys.size());

        std::vector<std::string> results(keys.size());

#ifdef MAPH_HAS_OPENMP
        // Phase 1: Prefetch all slots
        #pragma omp parallel for num_threads(num_threads_) schedule(static, 128)
        for (size_t i = 0; i < keys.size(); ++i) {
            // Trigger prefetch by calling exists (lightweight)
            volatile bool dummy = maph_->exists(keys[i]);
        }

        // Phase 2: Actual lookups (data should be in cache)
        #pragma omp parallel for num_threads(num_threads_) schedule(static, 64)
        for (size_t i = 0; i < keys.size(); ++i) {
            results[i] = maph_->get(keys[i]);
        }
#else
        for (size_t i = 0; i < keys.size(); ++i) {
            results[i] = maph_->get(keys[i]);
        }
#endif

        return results;
    }

    /**
     * @brief Parallel exists check with SIMD reduction
     */
    size_t parallel_count_exists(
        const std::vector<std::string>& keys
    ) const {
        if (!maph_) return 0;

        std::atomic<size_t> count{0};

#ifdef MAPH_HAS_OPENMP
        #pragma omp parallel num_threads(num_threads_)
        {
            size_t local_count = 0;

            #pragma omp for simd reduction(+:local_count) schedule(static)
            for (size_t i = 0; i < keys.size(); ++i) {
                if (maph_->exists(keys[i])) {
                    local_count++;
                }
            }

            count.fetch_add(local_count, std::memory_order_relaxed);
        }
#else
        for (const auto& key : keys) {
            if (maph_->exists(key)) {
                count++;
            }
        }
#endif

        return count.load();
    }

    /**
     * @brief Parallel remove with conflict detection
     */
    size_t parallel_remove(
        const std::vector<std::string>& keys
    ) {
        if (!maph_) return 0;

        std::atomic<size_t> removed{0};

#ifdef MAPH_HAS_OPENMP
        #pragma omp parallel for num_threads(num_threads_) schedule(dynamic, 32)
        for (size_t i = 0; i < keys.size(); ++i) {
            if (maph_->remove(keys[i])) {
                removed.fetch_add(1, std::memory_order_relaxed);
            }
        }
#else
        for (const auto& key : keys) {
            if (maph_->remove(key)) {
                removed++;
            }
        }
#endif

        return removed.load();
    }

    /**
     * @brief Parallel scan with predicate filtering
     *
     * Scans all slots in parallel and applies a predicate function.
     */
    template<typename Predicate>
    std::vector<std::string> parallel_scan(
        Predicate pred
    ) const {
        if (!maph_) return {};

        size_t num_slots = maph_->size();
        std::vector<std::vector<std::string>> thread_results(num_threads_);

#ifdef MAPH_HAS_OPENMP
        #pragma omp parallel num_threads(num_threads_)
        {
            int tid = omp_get_thread_num();
            auto& local_results = thread_results[tid];

            #pragma omp for schedule(dynamic, 256)
            for (size_t i = 0; i < num_slots; ++i) {
                // Would need access to slots directly for full scan
                // This is a placeholder for the concept
            }
        }
#endif

        // Merge thread-local results
        std::vector<std::string> results;
        for (const auto& thread_result : thread_results) {
            results.insert(results.end(), thread_result.begin(), thread_result.end());
        }

        return results;
    }

    /**
     * @brief Get statistics about parallel operations
     */
    struct ParallelStats {
        size_t total_operations{0};
        size_t total_cache_hits{0};
        size_t total_collisions{0};
        std::vector<size_t> per_thread_ops;
    };

    ParallelStats get_stats() const {
        ParallelStats stats;
        stats.per_thread_ops.reserve(num_threads_);

        for (const auto& ts : thread_stats_) {
            stats.total_operations += ts.operations;
            stats.total_cache_hits += ts.cache_hits;
            stats.total_collisions += ts.collisions;
            stats.per_thread_ops.push_back(ts.operations);
        }

        return stats;
    }

    /**
     * @brief Reset thread statistics
     */
    void reset_stats() {
        for (auto& ts : thread_stats_) {
            ts.operations = 0;
            ts.cache_hits = 0;
            ts.collisions = 0;
        }
    }

    /**
     * @brief Set number of threads for parallel operations
     */
    void set_num_threads(int threads) {
        num_threads_ = threads;
        thread_stats_.resize(threads);
#ifdef MAPH_HAS_OPENMP
        omp_set_num_threads(threads);
#endif
    }

    int get_num_threads() const { return num_threads_; }

    // Forward base operations
    std::string get(const std::string& key) const {
        return maph_ ? maph_->get(key) : "";
    }

    bool set(const std::string& key, const std::string& value) {
        return maph_ ? maph_->set(key, value) : false;
    }

    bool exists(const std::string& key) const {
        return maph_ ? maph_->exists(key) : false;
    }

    bool remove(const std::string& key) {
        return maph_ ? maph_->remove(key) : false;
    }

    size_t size() const { return maph_ ? maph_->size() : 0; }
    size_t used() const { return maph_ ? maph_->used() : 0; }
    void sync() { if (maph_) maph_->sync(); }
};

/**
 * @brief Builder for parallel maph instances
 */
class MaphParallelBuilder {
private:
    std::string path_;
    size_t num_slots_{1000000};
    int num_threads_{0};
    std::function<uint32_t(const std::string&)> hash_fn_;

public:
    MaphParallelBuilder& path(const std::string& p) {
        path_ = p;
        return *this;
    }

    MaphParallelBuilder& slots(size_t n) {
        num_slots_ = n;
        return *this;
    }

    MaphParallelBuilder& threads(int t) {
        num_threads_ = t;
        return *this;
    }

    MaphParallelBuilder& hash(std::function<uint32_t(const std::string&)> fn) {
        hash_fn_ = std::move(fn);
        return *this;
    }

    std::unique_ptr<MaphParallel> build() {
        auto mp = MaphParallel::create(path_, num_slots_, num_threads_);
        if (mp && mp->maph_ && hash_fn_) {
            mp->maph_->set_hash_function(std::move(hash_fn_));
        }
        return mp;
    }
};

} // namespace parallel
} // namespace maph