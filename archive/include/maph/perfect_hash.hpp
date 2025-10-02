/**
 * @file perfect_hash.hpp
 * @brief Perfect hash function integration for maph
 * 
 * Provides a unified interface for different perfect hash implementations
 * including RecSplit, CHD, and BBHash. Supports dynamic switching between
 * standard hashing and perfect hashing modes.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <cstdint>
#include <fstream>
#include <mutex>

namespace maph {

using JsonView = std::string_view;

/**
 * @enum PerfectHashType
 * @brief Types of perfect hash functions supported
 */
enum class PerfectHashType {
    RECSPLIT,    ///< RecSplit minimal perfect hash (fastest)
    CHD,         ///< Compress, Hash, and Displace
    BBHASH,      ///< BBHash minimal perfect hash
    DISABLED     ///< Perfect hashing disabled (use standard FNV-1a)
};

/**
 * @struct PerfectHashConfig
 * @brief Configuration for perfect hash functions
 */
struct PerfectHashConfig {
    PerfectHashType type{PerfectHashType::RECSPLIT};
    uint32_t leaf_size{8};      ///< RecSplit leaf size (4-16)
    double gamma{1.0};          ///< BBHash load factor
    uint32_t threads{1};        ///< Construction threads
    bool minimal{true};         ///< Create minimal perfect hash (no unused slots)
};

/**
 * @class PerfectHashInterface
 * @brief Abstract interface for perfect hash functions
 */
class PerfectHashInterface {
public:
    virtual ~PerfectHashInterface() = default;
    
    /**
     * @brief Get the hash value for a key
     * @param key Key to hash
     * @return Hash value, or std::nullopt if key not in original set
     */
    virtual std::optional<uint64_t> hash(JsonView key) const = 0;
    
    /**
     * @brief Get the maximum hash value (for bounds checking)
     * @return Maximum possible hash value
     */
    virtual uint64_t max_hash() const = 0;
    
    /**
     * @brief Check if this is a minimal perfect hash
     * @return true if minimal (no gaps), false if perfect but may have gaps
     */
    virtual bool is_minimal() const = 0;
    
    /**
     * @brief Get the number of keys this hash was built for
     * @return Number of keys in the original set
     */
    virtual size_t key_count() const = 0;
    
    /**
     * @brief Serialize the hash function to binary data
     * @return Binary representation of the hash function
     */
    virtual std::vector<uint8_t> serialize() const = 0;
    
    /**
     * @brief Deserialize a hash function from binary data
     * @param data Binary data to deserialize
     * @return true if successful
     */
    virtual bool deserialize(const std::vector<uint8_t>& data) = 0;
    
    /**
     * @brief Get memory usage in bytes
     * @return Memory consumption of the hash function
     */
    virtual size_t memory_usage() const = 0;
    
    /**
     * @brief Get the type of this hash function
     * @return Hash function type
     */
    virtual PerfectHashType type() const = 0;
};

/**
 * @class RecSplitHash
 * @brief RecSplit minimal perfect hash implementation
 * 
 * RecSplit is currently one of the fastest minimal perfect hash functions.
 * It provides excellent performance and compact memory usage.
 */
class RecSplitHash : public PerfectHashInterface {
private:
    struct RecSplitImpl;
    std::unique_ptr<RecSplitImpl> impl_;
    PerfectHashConfig config_;
    size_t key_count_{0};

public:
    RecSplitHash(const PerfectHashConfig& config = {});
    ~RecSplitHash();
    
    /**
     * @brief Build perfect hash from a set of keys
     * @param keys Vector of keys to build hash for
     * @return true if successful
     */
    bool build(const std::vector<std::string>& keys);
    
    // PerfectHashInterface implementation
    std::optional<uint64_t> hash(JsonView key) const override;
    uint64_t max_hash() const override;
    bool is_minimal() const override { return true; }
    size_t key_count() const override { return key_count_; }
    std::vector<uint8_t> serialize() const override;
    bool deserialize(const std::vector<uint8_t>& data) override;
    size_t memory_usage() const override;
    PerfectHashType type() const override { return PerfectHashType::RECSPLIT; }
};

/**
 * @class StandardHash
 * @brief Fallback to standard FNV-1a hashing when perfect hash not available
 */
class StandardHash : public PerfectHashInterface {
private:
    uint64_t num_slots_;
    size_t key_count_;

public:
    explicit StandardHash(uint64_t num_slots) : num_slots_(num_slots), key_count_(0) {}
    
    void set_key_count(size_t count) { key_count_ = count; }
    
    // PerfectHashInterface implementation
    std::optional<uint64_t> hash(JsonView key) const override;
    uint64_t max_hash() const override { return num_slots_ - 1; }
    bool is_minimal() const override { return false; }
    size_t key_count() const override { return key_count_; }
    std::vector<uint8_t> serialize() const override;
    bool deserialize(const std::vector<uint8_t>& data) override;
    size_t memory_usage() const override { return sizeof(*this); }
    PerfectHashType type() const override { return PerfectHashType::DISABLED; }
};

/**
 * @class PerfectHashFactory
 * @brief Factory for creating perfect hash instances
 */
class PerfectHashFactory {
public:
    /**
     * @brief Create a perfect hash instance
     * @param config Configuration for the hash function
     * @return Unique pointer to hash instance
     */
    static std::unique_ptr<PerfectHashInterface> create(const PerfectHashConfig& config);
    
    /**
     * @brief Build a perfect hash from keys
     * @param keys Keys to build hash for
     * @param config Hash configuration
     * @return Perfect hash instance, or nullptr on failure
     */
    static std::unique_ptr<PerfectHashInterface> build(
        const std::vector<std::string>& keys,
        const PerfectHashConfig& config = {});
    
    /**
     * @brief Load a perfect hash from serialized data
     * @param data Serialized hash data
     * @param type Type of hash function
     * @return Perfect hash instance, or nullptr on failure
     */
    static std::unique_ptr<PerfectHashInterface> load(
        const std::vector<uint8_t>& data,
        PerfectHashType type);
};

/**
 * @class KeyJournal
 * @brief Maintains a journal of all keys for perfect hash rebuilding
 * 
 * The key journal tracks all keys that have been inserted into the hash table.
 * This is essential for rebuilding perfect hash functions when new keys are added
 * or when optimization is requested.
 */
class KeyJournal {
private:
    std::string journal_path_;
    std::ofstream journal_file_;
    std::vector<std::string> cached_keys_;
    bool caching_enabled_{true};
    mutable std::mutex mutex_;

public:
    explicit KeyJournal(const std::string& journal_path);
    ~KeyJournal();
    
    /**
     * @brief Record a key insertion
     * @param key Key that was inserted
     * @param value_hash Hash of the associated value (for verification)
     */
    void record_insert(JsonView key, uint32_t value_hash);
    
    /**
     * @brief Record a key removal
     * @param key Key that was removed
     */
    void record_remove(JsonView key);
    
    /**
     * @brief Get all currently active keys
     * @return Vector of all keys that are currently in the table
     */
    std::vector<std::string> get_active_keys() const;
    
    /**
     * @brief Load keys from the journal file
     * @param force_reload Force reloading even if cached
     * @return Number of keys loaded
     */
    size_t load_keys(bool force_reload = false);
    
    /**
     * @brief Clear the journal (use with caution)
     */
    void clear();
    
    /**
     * @brief Get statistics about the journal
     */
    struct Stats {
        size_t total_keys;
        size_t journal_size_bytes;
        size_t memory_usage_bytes;
        bool is_cached;
    };
    
    Stats get_stats() const;
    
    /**
     * @brief Enable/disable key caching in memory
     * @param enabled Whether to cache keys
     */
    void set_caching(bool enabled) { caching_enabled_ = enabled; }
    
    /**
     * @brief Flush any pending writes to disk
     */
    void flush();
    
    /**
     * @brief Compact the journal by removing obsolete entries
     * @return Number of entries removed
     */
    size_t compact();
};

} // namespace maph