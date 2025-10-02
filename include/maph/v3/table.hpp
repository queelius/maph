/**
 * @file table.hpp
 * @brief The hash table - composes storage and hashing orthogonally
 */

#pragma once

#include "core.hpp"
#include "hashers.hpp"
#include "storage.hpp"
#include <ranges>
#include <coroutine>

namespace maph::v3 {

/**
 * @class hash_table
 * @brief Clean, composable hash table implementation
 *
 * This class exemplifies the Unix philosophy: it does one thing well -
 * manages a hash table by composing a hasher and storage backend.
 * All complexity is pushed down to the composed components.
 */
template<hasher Hasher, typename Storage>
class hash_table {
    Hasher hasher_;
    Storage storage_;

    // Helper to convert string_view to bytes
    static std::span<const std::byte> as_bytes(std::string_view sv) noexcept {
        return {reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    }

    static std::string_view as_string(std::span<const std::byte> bytes) noexcept {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    // Truncate hash to 32 bits for comparison (slots only store 32-bit hashes)
    static hash_value truncate_hash(hash_value h) noexcept {
        return hash_value{static_cast<uint32_t>(h.value & 0xFFFFFFFF)};
    }

public:
    constexpr hash_table(Hasher h, Storage s) noexcept
        : hasher_(std::move(h)), storage_(std::move(s)) {}

    // ===== CORE OPERATIONS =====

    /**
     * @brief Get value for a key
     * Simple, clear, no hidden complexity
     */
    [[nodiscard]] result<std::string_view> get(std::string_view key) const noexcept {
        if constexpr (requires { hasher_.probe_sequence(key); }) {
            // Linear probing support
            auto probes = hasher_.probe_sequence(key);
            auto target_hash = truncate_hash(hasher_.hash(key));

            while (!probes.at_end()) {
                auto idx = *probes;
                if (storage_.empty(idx)) {
                    return std::unexpected(error::key_not_found);
                }
                if (storage_.hash_at(idx) == target_hash) {
                    auto val = storage_.read(idx);
                    if (val) {
                        return as_string(val->bytes());
                    }
                }
                ++probes;
            }
            return std::unexpected(error::key_not_found);
        } else {
            // Direct indexing (perfect hash)
            auto hash = truncate_hash(hasher_.hash(key));
            auto idx = slot_index{hash % storage_.get_slot_count().value};

            if (storage_.hash_at(idx) != hash) {
                return std::unexpected(error::key_not_found);
            }

            auto val = storage_.read(idx);
            if (val) {
                return as_string(val->bytes());
            }
            return std::unexpected(error::key_not_found);
        }
    }

    /**
     * @brief Set a key-value pair
     * Clean error propagation with std::expected
     */
    [[nodiscard]] status set(std::string_view key, std::string_view value) noexcept {
        auto value_bytes = as_bytes(value);
        auto hash = truncate_hash(hasher_.hash(key));

        if constexpr (requires { hasher_.probe_sequence(key); }) {
            // Linear probing support
            auto probes = hasher_.probe_sequence(key);

            while (!probes.at_end()) {
                auto idx = *probes;
                if (storage_.empty(idx) || storage_.hash_at(idx) == hash) {
                    return storage_.write(idx, hash, value_bytes);
                }
                ++probes;
            }
            return std::unexpected(error::table_full);
        } else {
            // Direct indexing
            auto idx = slot_index{hash % storage_.get_slot_count().value};
            return storage_.write(idx, hash, value_bytes);
        }
    }

    /**
     * @brief Remove a key
     */
    [[nodiscard]] status remove(std::string_view key) noexcept {
        if constexpr (requires { hasher_.probe_sequence(key); }) {
            auto probes = hasher_.probe_sequence(key);
            auto target_hash = truncate_hash(hasher_.hash(key));

            while (!probes.at_end()) {
                auto idx = *probes;
                if (storage_.empty(idx)) {
                    return std::unexpected(error::key_not_found);
                }
                if (storage_.hash_at(idx) == target_hash) {
                    return storage_.clear(idx);
                }
                ++probes;
            }
            return std::unexpected(error::key_not_found);
        } else {
            auto hash = truncate_hash(hasher_.hash(key));
            auto idx = slot_index{hash % storage_.get_slot_count().value};

            if (storage_.hash_at(idx) != hash) {
                return std::unexpected(error::key_not_found);
            }
            return storage_.clear(idx);
        }
    }

    /**
     * @brief Check if a key exists
     */
    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return get(key).has_value();
    }

    // ===== ITERATION =====

    /**
     * @brief Iterate over all key-value pairs
     * Uses C++20 coroutines for elegant lazy evaluation
     */
    auto items() const {
        struct item {
            slot_index index;
            hash_value hash;
            std::string_view value;
        };

        struct iterator {
            const hash_table* table;
            slot_index current{0};

            std::optional<item> next() {
                auto total = table->storage_.get_slot_count();
                while (current.value < total.value) {
                    auto idx = current;
                    current = slot_index{current.value + 1};

                    if (!table->storage_.empty(idx)) {
                        if (auto val = table->storage_.read(idx)) {
                            return item{
                                idx,
                                table->storage_.hash_at(idx),
                                as_string(val->bytes())
                            };
                        }
                    }
                }
                return std::nullopt;
            }
        };

        return iterator{this};
    }

    // ===== BATCH OPERATIONS =====

    /**
     * @brief Get multiple keys efficiently
     * Clean functional interface with callbacks
     */
    template<typename Callback>
    void get_batch(std::span<const std::string_view> keys, Callback&& cb) const {
        for (auto key : keys) {
            if (auto val = get(key)) {
                cb(key, *val);
            }
        }
    }

    /**
     * @brief Set multiple key-value pairs
     * Returns number successfully set
     */
    size_t set_batch(std::span<const std::pair<std::string_view, std::string_view>> pairs) {
        size_t count = 0;
        for (const auto& [key, value] : pairs) {
            if (set(key, value)) {
                ++count;
            }
        }
        return count;
    }

    // ===== STATISTICS =====

    struct stats {
        slot_count total_slots;
        size_t used_slots;
        double load_factor;
    };

    [[nodiscard]] stats statistics() const noexcept {
        auto total = storage_.get_slot_count();
        size_t used = 0;

        for (uint64_t i = 0; i < total.value; ++i) {
            if (!storage_.empty(slot_index{i})) {
                ++used;
            }
        }

        return {
            total,
            used,
            static_cast<double>(used) / total.value
        };
    }
};

// ===== FACTORY FUNCTIONS =====
// Clean, composable construction

template<typename Hasher, typename Storage>
auto make_table(Hasher&& h, Storage&& s) {
    return hash_table<std::decay_t<Hasher>, std::decay_t<Storage>>{
        std::forward<Hasher>(h),
        std::forward<Storage>(s)
    };
}

// Convenience factory for memory-mapped table
inline auto make_mmap_table(const std::filesystem::path& path, slot_count slots) {
    return mmap_storage<>::create(path, slots).transform([slots](auto&& storage) {
        return make_table(
            linear_probe_hasher{fnv1a_hasher{slots}},
            std::move(storage)
        );
    });
}

// Convenience factory for in-memory table
inline auto make_memory_table(slot_count slots) {
    return make_table(
        linear_probe_hasher{fnv1a_hasher{slots}},
        heap_storage<>{slots}
    );
}

} // namespace maph::v3