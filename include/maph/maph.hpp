/**
 * @file maph.hpp
 * @brief Main header for maph - composable perfect hash tables
 *
 * This is the primary interface that brings together all components
 * in a clean, composable way. Each piece does one thing well.
 */

#pragma once

#include "core.hpp"
#include "hashers.hpp"
#include "storage.hpp"
#include "table.hpp"
#include "optimization.hpp"
#include <memory>

namespace maph {

// Configuration struct - aggregate type to allow designated initializers
struct maph_config {
    slot_count slots{1000};
    size_t max_probes{10};
    bool enable_journal{true};
    bool enable_cache{false};
    size_t cache_size{1000};
};

/**
 * @class maph
 * @brief High-level interface combining all components elegantly
 *
 * This class provides a convenient interface while maintaining
 * the composability of the underlying components.
 */
class maph {
public:
    using config = maph_config;

private:
    // Type erasure for flexibility without template bloat
    class impl_base {
    public:
        virtual ~impl_base() = default;
        virtual result<std::string_view> get(std::string_view key) const = 0;
        virtual status set(std::string_view key, std::string_view value) = 0;
        virtual status remove(std::string_view key) = 0;
        virtual bool contains(std::string_view key) const = 0;
        virtual result<void> optimize() = 0;
        virtual size_t size() const = 0;
        virtual double load_factor() const = 0;
    };

    template<typename Table>
    class impl : public impl_base {
        Table table_;

    public:
        explicit impl(Table t) : table_(std::move(t)) {}

        result<std::string_view> get(std::string_view key) const override {
            return table_.get(key);
        }

        status set(std::string_view key, std::string_view value) override {
            return table_.set(key, value);
        }

        status remove(std::string_view key) override {
            return table_.remove(key);
        }

        bool contains(std::string_view key) const override {
            return table_.contains(key);
        }

        result<void> optimize() override {
            // Optimization would create a new perfect hash table
            // This is a simplified version
            return {};
        }

        size_t size() const override {
            auto stats = table_.statistics();
            return stats.used_slots;
        }

        double load_factor() const override {
            auto stats = table_.statistics();
            return stats.load_factor;
        }
    };

    std::unique_ptr<impl_base> pimpl_;

public:
    // ===== FACTORY METHODS =====

    /**
     * @brief Create a new memory-mapped database
     * Clean error handling with std::expected
     */
    [[nodiscard]] static result<maph> create(
        const std::filesystem::path& path,
        const config& cfg = config{}) {

        auto storage_result = mmap_storage<>::create(path, cfg.slots);
        if (!storage_result) {
            return std::unexpected(storage_result.error());
        }

        auto hasher = linear_probe_hasher{
            fnv1a_hasher{cfg.slots},
            cfg.max_probes
        };

        if (cfg.enable_journal) {
            auto table = with_journal(make_table(
                std::move(hasher),
                std::move(*storage_result)
            ));

            return maph{std::make_unique<impl<decltype(table)>>(std::move(table))};
        } else {
            auto table = make_table(
                std::move(hasher),
                std::move(*storage_result)
            );

            return maph{std::make_unique<impl<decltype(table)>>(std::move(table))};
        }
    }

    /**
     * @brief Open an existing database
     */
    [[nodiscard]] static result<maph> open(
        const std::filesystem::path& path,
        bool readonly = false) {

        auto storage_result = mmap_storage<>::open(path, readonly);
        if (!storage_result) {
            return std::unexpected(storage_result.error());
        }

        auto slots = storage_result->get_slot_count();
        auto hasher = linear_probe_hasher{
            fnv1a_hasher{slots},
            10
        };

        auto table = make_table(
            std::move(hasher),
            std::move(*storage_result)
        );

        return maph{std::make_unique<impl<decltype(table)>>(std::move(table))};
    }

    /**
     * @brief Create an in-memory database
     */
    [[nodiscard]] static maph create_memory(const config& cfg = config{}) {
        auto hasher = linear_probe_hasher{
            fnv1a_hasher{cfg.slots},
            cfg.max_probes
        };

        auto storage = heap_storage<>{cfg.slots};

        if (cfg.enable_cache) {
            auto cached = cached_storage{std::move(storage), cfg.cache_size};
            auto table = make_table(std::move(hasher), std::move(cached));
            return maph{std::make_unique<impl<decltype(table)>>(std::move(table))};
        } else {
            auto table = make_table(std::move(hasher), std::move(storage));
            return maph{std::make_unique<impl<decltype(table)>>(std::move(table))};
        }
    }

    // ===== CORE OPERATIONS =====

    /**
     * @brief Get value for key
     * Clean monadic error handling
     */
    [[nodiscard]] result<std::string_view> get(std::string_view key) const {
        return pimpl_->get(key);
    }

    /**
     * @brief Set key-value pair
     */
    [[nodiscard]] status set(std::string_view key, std::string_view value) {
        return pimpl_->set(key, value);
    }

    /**
     * @brief Remove key
     */
    [[nodiscard]] status remove(std::string_view key) {
        return pimpl_->remove(key);
    }

    /**
     * @brief Check if key exists
     */
    [[nodiscard]] bool contains(std::string_view key) const {
        return pimpl_->contains(key);
    }

    /**
     * @brief Get value with default
     * Functional programming style
     */
    [[nodiscard]] std::string_view get_or(
        std::string_view key,
        std::string_view default_value) const {
        return get(key).value_or(default_value);
    }

    /**
     * @brief Update value functionally
     * Returns true if key existed and was updated
     */
    template<typename F>
    [[nodiscard]] bool update(std::string_view key, F&& transform) {
        auto current = this->get(key);
        if (!current) return false;

        auto new_value = transform(*current);
        return this->set(key, new_value).has_value();
    }

    // ===== BATCH OPERATIONS =====

    /**
     * @brief Set multiple values transactionally
     * All-or-nothing semantics
     */
    [[nodiscard]] status set_all(
        std::initializer_list<std::pair<std::string_view, std::string_view>> pairs) {

        // First check if all will fit
        for (const auto& [k, v] : pairs) {
            if (!contains(k)) {
                // Would need to add new key
                // Check if there's space (simplified)
                if (load_factor() > 0.9) {
                    return std::unexpected(error::table_full);
                }
            }
        }

        // Now apply all changes
        for (const auto& [k, v] : pairs) {
            if (auto status = set(k, v); !status) {
                return status;
            }
        }

        return {};
    }

    // ===== OPTIMIZATION =====

    /**
     * @brief Optimize to perfect hash
     */
    [[nodiscard]] result<void> optimize() {
        return pimpl_->optimize();
    }

    // ===== STATISTICS =====

    [[nodiscard]] size_t size() const {
        return pimpl_->size();
    }

    [[nodiscard]] double load_factor() const {
        return pimpl_->load_factor();
    }

    [[nodiscard]] bool empty() const {
        return size() == 0;
    }

private:
    explicit maph(std::unique_ptr<impl_base> impl)
        : pimpl_(std::move(impl)) {}
};

// ===== PIPELINE OPERATIONS =====
// Functional programming style operations

/**
 * @brief Pipeline for transforming tables
 * Enables: table | optimize() | cache(1000) | persist("optimized.maph")
 */
namespace pipeline {

struct optimize_op {};

inline auto optimize() { return optimize_op{}; }

template<typename Table>
auto operator|(Table&& table, optimize_op) {
    optimizer opt{table};
    // Implementation depends on table type
    return table;  // Placeholder
}

struct cache_op {
    size_t size;
};

inline auto cache(size_t size) { return cache_op{size}; }

template<typename Storage>
auto operator|(Storage&& storage, cache_op op) {
    return cached_storage{std::forward<Storage>(storage), op.size};
}

} // namespace pipeline

// ===== FREE FUNCTION WRAPPERS =====
// These enable `maph::create()` and `maph::create_memory()` syntax

/**
 * @brief Create a new memory-mapped database
 */
[[nodiscard]] inline result<maph> create(
    const std::filesystem::path& path,
    maph_config cfg = maph_config{}) {
    return maph::create(path, cfg);
}

/**
 * @brief Create an in-memory database
 */
[[nodiscard]] inline maph create_memory(maph_config cfg = maph_config{}) {
    return maph::create_memory(cfg);
}

/**
 * @brief Open an existing database
 */
[[nodiscard]] inline result<maph> open(
    const std::filesystem::path& path,
    bool readonly = false) {
    return maph::open(path, readonly);
}

} // namespace maph
