/**
 * @file optimization.hpp
 * @brief Perfect hash optimization as a separate concern
 */

#pragma once

#include "core.hpp"
#include "hashers.hpp"
#include "table.hpp"
#include <vector>
#include <algorithm>
#include <execution>
#include <unordered_set>
#include <unordered_map>

namespace maph::v3 {

/**
 * @class optimizer
 * @brief Transforms a standard hash table into a perfect hash table
 *
 * This class exemplifies separation of concerns - optimization is
 * completely orthogonal to the table implementation.
 */
template<typename Table>
class optimizer {
    Table& table_;

public:
    explicit optimizer(Table& t) noexcept : table_(t) {}

    /**
     * @brief Extract all keys from the table
     * Clean functional approach
     */
    [[nodiscard]] std::vector<std::string> extract_keys() const {
        std::vector<std::string> keys;

        auto items = table_.items();
        while (auto item = items.next()) {
            // We need to reconstruct keys from values
            // In a real implementation, we'd store keys or use a journal
            keys.emplace_back(item->value);
        }

        // Remove duplicates
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

        return keys;
    }

    /**
     * @brief Build a perfect hash function from current keys
     */
    [[nodiscard]] result<minimal_perfect_hasher> build_perfect_hash() const {
        auto keys = extract_keys();
        if (keys.empty()) {
            return std::unexpected(error::optimization_failed);
        }

        minimal_perfect_hasher::builder builder;
        for (const auto& key : keys) {
            builder.add(key);
        }

        return builder.build();
    }

    /**
     * @brief Optimize the table in-place
     * Returns a new table with perfect hashing
     */
    template<typename Storage>
    [[nodiscard]] auto optimize_to_perfect(Storage&& storage) const
        -> result<hash_table<minimal_perfect_hasher, std::decay_t<Storage>>> {

        auto perfect_hash = build_perfect_hash();
        if (!perfect_hash) {
            return std::unexpected(perfect_hash.error());
        }

        // Create new table with perfect hash
        auto perfect_table = make_table(
            std::move(*perfect_hash),
            std::forward<Storage>(storage)
        );

        // Copy all data to new table
        auto items = table_.items();
        while (auto item = items.next()) {
            // In real implementation, we'd have the actual keys
            // For now, this is a conceptual example
            if (!perfect_table.set(item->value, item->value)) {
                return std::unexpected(error::optimization_failed);
            }
        }

        return perfect_table;
    }
};

/**
 * @class key_journal
 * @brief Tracks keys for perfect hash rebuilding
 *
 * Separate concern from the main table - does one thing well
 */
class key_journal {
    std::vector<std::string> keys_;
    std::unordered_set<std::string> key_set_;

public:
    void record_insert(std::string_view key) {
        if (key_set_.insert(std::string(key)).second) {
            keys_.emplace_back(key);
        }
    }

    void record_remove(std::string_view key) {
        key_set_.erase(std::string(key));
        keys_.erase(
            std::remove(keys_.begin(), keys_.end(), key),
            keys_.end()
        );
    }

    [[nodiscard]] const std::vector<std::string>& keys() const noexcept {
        return keys_;
    }

    [[nodiscard]] size_t size() const noexcept {
        return keys_.size();
    }

    void clear() noexcept {
        keys_.clear();
        key_set_.clear();
    }
};

/**
 * @class journaled_table
 * @brief Decorator that adds journaling to any table
 *
 * Composable design - wraps any table to add key tracking
 */
template<typename Table>
class journaled_table {
    Table table_;
    key_journal journal_;

public:
    explicit journaled_table(Table t) noexcept
        : table_(std::move(t)) {}

    [[nodiscard]] result<std::string_view> get(std::string_view key) const noexcept {
        return table_.get(key);
    }

    [[nodiscard]] status set(std::string_view key, std::string_view value) noexcept {
        auto status = table_.set(key, value);
        if (status) {
            journal_.record_insert(key);
        }
        return status;
    }

    [[nodiscard]] status remove(std::string_view key) noexcept {
        auto status = table_.remove(key);
        if (status) {
            journal_.record_remove(key);
        }
        return status;
    }

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return table_.contains(key);
    }

    [[nodiscard]] const key_journal& journal() const noexcept {
        return journal_;
    }

    [[nodiscard]] Table& base_table() noexcept {
        return table_;
    }

    [[nodiscard]] const Table& base_table() const noexcept {
        return table_;
    }

    [[nodiscard]] auto statistics() const noexcept {
        return table_.statistics();
    }

    /**
     * @brief Optimize to perfect hash using journaled keys
     */
    template<typename Storage>
    [[nodiscard]] auto optimize(Storage&& storage) const
        -> result<hash_table<minimal_perfect_hasher, std::decay_t<Storage>>> {

        minimal_perfect_hasher::builder builder;
        for (const auto& key : journal_.keys()) {
            builder.add(key);
        }

        auto perfect_hash = builder.build();
        if (!perfect_hash) {
            return std::unexpected(perfect_hash.error());
        }

        auto perfect_table = make_table(
            std::move(*perfect_hash),
            std::forward<Storage>(storage)
        );

        // Copy all data
        for (const auto& key : journal_.keys()) {
            if (auto val = table_.get(key)) {
                if (!perfect_table.set(key, *val)) {
                    return std::unexpected(error::optimization_failed);
                }
            }
        }

        return perfect_table;
    }
};

// Factory function for journaled tables
template<typename Table>
auto with_journal(Table&& t) {
    return journaled_table<std::decay_t<Table>>{
        std::forward<Table>(t)
    };
}

} // namespace maph::v3