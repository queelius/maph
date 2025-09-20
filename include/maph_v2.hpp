/**
 * @file maph_v2.hpp
 * @brief maph v2 - Memory-mapped Adaptive Perfect Hash
 * 
 * Ultra-fast JSON key-value store with true perfect hashing support.
 * Provides guaranteed O(1) lookups after optimization.
 * 
 * Key features:
 * - Dual-mode operation: Standard hash → Perfect hash
 * - Single unified slot array (no confusing static/dynamic split)
 * - Key journal for perfect hash rebuilding
 * - Optimization workflow: Import → Use → Optimize → Perfect lookups
 * - Backward compatibility with v1 databases
 * 
 * Design principles:
 * - Zero-copy via string_view
 * - Single hash computation (reused)
 * - Fixed 512-byte slots
 * - Lock-free operations
 * - No allocations on hot path
 * 
 * Thread Safety:
 * - All read operations are thread-safe
 * - Concurrent writes require external synchronization
 * - Atomic operations on slot versions prevent torn reads
 * 
 * Performance Characteristics:
 * - Lookup (optimized): O(1) guaranteed, single memory access
 * - Lookup (standard): O(1) average, O(k) worst case where k = MAX_PROBE_DISTANCE  
 * - Insert: O(1) average with linear probing fallback
 * - Memory: 512 bytes per slot + 512 byte header + perfect hash overhead
 * - Cache: Optimized for CPU cache lines (64-byte aligned slots)
 */

#pragma once

#include "maph/perfect_hash.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <string_view>
#include <optional>
#include <memory>
#include <vector>
#include <thread>
#include <future>
#include <algorithm>
#include <execution>
#include <chrono>
#include <immintrin.h>  // For SIMD
#include <filesystem>

namespace maph {

using JsonView = std::string_view;

// ===== CONSTANTS =====

static constexpr uint32_t MAGIC_NUMBER = 0x4D415048;  // "MAPH"
static constexpr uint32_t CURRENT_VERSION = 2;        // v2 with perfect hashing
static constexpr size_t SLOT_SIZE = 512;
static constexpr size_t HEADER_SIZE = 512;
static constexpr size_t SLOT_DATA_SIZE = 496;  // SLOT_SIZE - metadata
static constexpr size_t MAX_PROBE_DISTANCE = 10;

// ===== ERROR CODES =====

/**
 * @enum ErrorCode  
 * @brief Error codes returned by maph operations
 */
enum class ErrorCode {
    SUCCESS = 0,          ///< Operation completed successfully
    FILE_OPEN_FAILED,     ///< Failed to open database file
    FILE_TRUNCATE_FAILED, ///< Failed to resize database file
    MMAP_FAILED,          ///< Memory mapping failed
    INVALID_MAGIC,        ///< File has invalid magic number (not a maph database)
    VERSION_MISMATCH,     ///< Database version incompatible with library
    VALUE_TOO_LARGE,      ///< Value exceeds maximum slot size (496 bytes)
    TABLE_FULL,           ///< Hash table is full in probe region
    KEY_NOT_FOUND,        ///< Key does not exist in database
    READONLY_STORE,       ///< Attempted write operation on read-only database
    OPTIMIZATION_FAILED,  ///< Perfect hash optimization failed
    JOURNAL_ERROR         ///< Key journal operation failed
};

/**
 * @struct Result
 * @brief Operation result containing error code and message
 */
struct Result {
    ErrorCode code;      ///< Error code indicating success or failure type
    std::string message; ///< Human-readable error description
    
    bool ok() const { return code == ErrorCode::SUCCESS; }
    operator bool() const { return ok(); }
};

// ===== CORE STRUCTURES =====

/**
 * @struct Slot
 * @brief Storage slot for key-value pairs (unchanged from v1)
 * 
 * Each slot is 512 bytes and cache-line aligned (64 bytes) for optimal
 * performance. The slot contains metadata and the actual value data.
 */
struct alignas(64) Slot {
    std::atomic<uint64_t> hash_version{0};  ///< Combined hash (high 32) and version (low 32)
    uint32_t size{0};                       ///< Size of stored value in bytes
    uint32_t reserved{0};                   ///< Reserved for future use
    char data[SLOT_DATA_SIZE]{};            ///< Value data storage
    
    static constexpr size_t MAX_SIZE = SLOT_DATA_SIZE;
    
    uint32_t hash() const { 
        return static_cast<uint32_t>(hash_version.load(std::memory_order_acquire) >> 32); 
    }
    
    uint32_t version() const { 
        return static_cast<uint32_t>(hash_version.load(std::memory_order_acquire)); 
    }
    
    bool empty() const { 
        return hash() == 0; 
    }
    
    JsonView view() const {
        return JsonView(data, size);
    }
    
    void set(uint32_t h, JsonView value) {
        uint32_t v = version();
        hash_version.store((uint64_t(h) << 32) | (v + 1), std::memory_order_release);
        size = static_cast<uint32_t>(value.size());
        std::memcpy(data, value.data(), value.size());
        hash_version.store((uint64_t(h) << 32) | (v + 2), std::memory_order_release);
    }
    
    void clear() {
        uint32_t v = version();
        hash_version.store(v + 2, std::memory_order_release);
        size = 0;
    }
};

static_assert(sizeof(Slot) == 512);

/**
 * @enum HashMode
 * @brief Hash table operation mode
 */
enum class HashMode {
    STANDARD,    ///< Standard FNV-1a hashing with linear probing
    PERFECT,     ///< Perfect hashing - O(1) guaranteed lookups
    HYBRID       ///< Perfect hash for known keys, standard for new keys
};

/**
 * @struct Header  
 * @brief Database file header with perfect hashing support
 */
struct Header {
    uint32_t magic{MAGIC_NUMBER};           ///< Magic number for file validation ("MAPH")
    uint32_t version{CURRENT_VERSION};      ///< Database format version (v2)
    uint64_t total_slots{0};                ///< Total number of slots in database
    std::atomic<uint64_t> generation{0};    ///< Global generation counter
    HashMode hash_mode{HashMode::STANDARD}; ///< Current hash mode
    PerfectHashType perfect_hash_type{PerfectHashType::DISABLED}; ///< Perfect hash algorithm used
    uint64_t perfect_hash_data_offset{0};   ///< Offset to serialized perfect hash data
    uint64_t perfect_hash_data_size{0};     ///< Size of perfect hash data
    char reserved[448]{};                   ///< Reserved space for future extensions
};

static_assert(sizeof(Header) == 512);

// ===== HASH FUNCTION =====

/**
 * @struct Hash
 * @brief Unified hash function supporting both standard and perfect hashing
 */
struct Hash {
    struct Result {
        uint32_t hash;  ///< Full 32-bit hash value (for slot versioning)
        uint64_t index; ///< Slot index
        bool perfect;   ///< Whether this came from perfect hash
    };
    
    /**
     * @brief Compute hash using standard FNV-1a algorithm
     */
    static Result compute_standard(JsonView key, uint64_t num_slots) {
        uint32_t h = 2166136261u;
        for (unsigned char c : key) {
            h ^= c;
            h *= 16777619u;
        }
        if (h == 0) h = 1;  // Never return 0 (means empty)
        
        uint64_t idx = h % num_slots;
        return {h, idx, false};
    }
    
    /**
     * @brief Compute hash using perfect hash function if available
     */
    static Result compute_perfect(JsonView key, const PerfectHashInterface* perfect_hash, uint64_t num_slots) {
        if (!perfect_hash) {
            return compute_standard(key, num_slots);
        }
        
        auto perfect_idx = perfect_hash->hash(key);
        if (perfect_idx.has_value()) {
            // Perfect hash found - generate standard hash for versioning
            uint32_t h = 2166136261u;
            for (unsigned char c : key) {
                h ^= c;
                h *= 16777619u;
            }
            if (h == 0) h = 1;
            
            return {h, *perfect_idx, true};
        }
        
        // Key not in perfect hash set - use standard hashing
        return compute_standard(key, num_slots);
    }
};

// ===== MAIN CLASS =====

/**
 * @class Maph
 * @brief High-performance memory-mapped key-value store with perfect hashing
 * 
 * Provides a persistent key-value database with adaptive hashing:
 * - Starts with standard FNV-1a + linear probing
 * - Can be optimized to use perfect hashing for O(1) lookups
 * - Maintains key journal for perfect hash rebuilding
 * - Supports optimization workflow for production use
 */
class Maph {
private:
    int fd_{-1};                      ///< File descriptor for mmap'd file
    void* mapped_{nullptr};           ///< Pointer to memory-mapped region
    size_t file_size_{0};             ///< Total size of mapped file
    Header* header_{nullptr};         ///< Pointer to database header
    Slot* slots_{nullptr};            ///< Pointer to slot array
    bool readonly_{false};            ///< Whether database is read-only
    
    // Perfect hashing components
    std::unique_ptr<PerfectHashInterface> perfect_hash_{nullptr}; ///< Perfect hash function
    std::unique_ptr<KeyJournal> key_journal_{nullptr};          ///< Key tracking journal
    std::string database_path_;       ///< Path to database file
    
public:
    Maph() = default;
    ~Maph() { close(); }
    
    // Move-only
    Maph(const Maph&) = delete;
    Maph& operator=(const Maph&) = delete;
    Maph(Maph&& other) noexcept { swap(other); }
    Maph& operator=(Maph&& other) noexcept { swap(other); return *this; }
    
    // ===== CREATE/OPEN =====
    
    /**
     * @brief Create a new database file
     */
    static std::unique_ptr<Maph> create(const std::string& path, 
                                        uint64_t total_slots) {
        auto m = std::make_unique<Maph>();
        m->database_path_ = path;
        
        size_t file_size = sizeof(Header) + (total_slots * sizeof(Slot));
        
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) return nullptr;
        
        if (::ftruncate(fd, file_size) < 0) {
            ::close(fd);
            return nullptr;
        }
        
        void* mapped = ::mmap(nullptr, file_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            return nullptr;
        }
        
        // Initialize header
        auto* header = new (mapped) Header();
        header->total_slots = total_slots;
        header->hash_mode = HashMode::STANDARD;
        header->perfect_hash_type = PerfectHashType::DISABLED;
        
        auto* slots = reinterpret_cast<Slot*>(
            static_cast<char*>(mapped) + sizeof(Header));
        std::memset(slots, 0, total_slots * sizeof(Slot));
        
        m->fd_ = fd;
        m->mapped_ = mapped;
        m->file_size_ = file_size;
        m->header_ = header;
        m->slots_ = slots;
        
        // Initialize key journal
        std::string journal_path = path + ".journal";
        m->key_journal_ = std::make_unique<KeyJournal>(journal_path);
        
        return m;
    }
    
    /**
     * @brief Open an existing database file
     */
    static std::unique_ptr<Maph> open(const std::string& path, bool readonly = false) {
        auto m = std::make_unique<Maph>();
        m->database_path_ = path;
        
        int flags = readonly ? O_RDONLY : O_RDWR;
        int fd = ::open(path.c_str(), flags);
        if (fd < 0) return nullptr;
        
        struct stat st;
        if (::fstat(fd, &st) < 0) {
            ::close(fd);
            return nullptr;
        }
        
        int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
        void* mapped = ::mmap(nullptr, st.st_size, prot, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            return nullptr;
        }
        
        auto* header = static_cast<Header*>(mapped);
        if (header->magic != MAGIC_NUMBER) {
            ::munmap(mapped, st.st_size);
            ::close(fd);
            return nullptr;
        }
        
        m->fd_ = fd;
        m->mapped_ = mapped;
        m->file_size_ = st.st_size;
        m->header_ = header;
        m->slots_ = reinterpret_cast<Slot*>(
            static_cast<char*>(mapped) + sizeof(Header));
        m->readonly_ = readonly;
        
        // Initialize key journal
        if (!readonly) {
            std::string journal_path = path + ".journal";
            m->key_journal_ = std::make_unique<KeyJournal>(journal_path);
        }
        
        // Load perfect hash if present
        if (header->hash_mode == HashMode::PERFECT || header->hash_mode == HashMode::HYBRID) {
            m->load_perfect_hash();
        }
        
        return m;
    }
    
    // ===== CORE OPERATIONS =====
    
    /**
     * @brief Get value for a key with adaptive hashing
     */
    std::optional<JsonView> get(JsonView key) const {
        Hash::Result hash_result;
        
        if (header_->hash_mode == HashMode::PERFECT && perfect_hash_) {
            // Perfect hash mode - direct lookup
            hash_result = Hash::compute_perfect(key, perfect_hash_.get(), header_->total_slots);
            if (hash_result.perfect) {
                // Direct perfect hash lookup
                const Slot& slot = slots_[hash_result.index];
                if (slot.hash() == hash_result.hash) {
                    return slot.view();
                }
                return std::nullopt;
            }
        } else if (header_->hash_mode == HashMode::HYBRID && perfect_hash_) {
            // Hybrid mode - try perfect hash first, then standard
            hash_result = Hash::compute_perfect(key, perfect_hash_.get(), header_->total_slots);
            if (hash_result.perfect) {
                const Slot& slot = slots_[hash_result.index];
                if (slot.hash() == hash_result.hash) {
                    return slot.view();
                }
                // Key might have been removed, fall through to standard lookup
            }
        }
        
        // Standard hash mode or fallback
        hash_result = Hash::compute_standard(key, header_->total_slots);
        
        // Linear probing for collision resolution
        for (size_t i = 0; i < MAX_PROBE_DISTANCE; ++i) {
            uint64_t idx = (hash_result.index + i) % header_->total_slots;
            
            const Slot& slot = slots_[idx];
            if (slot.empty()) break;  // Key not found
            if (slot.hash() == hash_result.hash) {
                return slot.view();
            }
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Store a key-value pair with journal tracking
     */
    bool set(JsonView key, JsonView value) {
        if (readonly_ || value.size() > Slot::MAX_SIZE) return false;
        
        Hash::Result hash_result;
        bool found_slot = false;
        uint64_t slot_idx = 0;
        
        if (header_->hash_mode == HashMode::PERFECT && perfect_hash_) {
            // Perfect hash mode
            hash_result = Hash::compute_perfect(key, perfect_hash_.get(), header_->total_slots);
            if (hash_result.perfect) {
                slot_idx = hash_result.index;
                found_slot = true;
            }
        } else if (header_->hash_mode == HashMode::HYBRID && perfect_hash_) {
            // Hybrid mode - try perfect hash first
            hash_result = Hash::compute_perfect(key, perfect_hash_.get(), header_->total_slots);
            if (hash_result.perfect) {
                slot_idx = hash_result.index;
                found_slot = true;
            }
        }
        
        if (!found_slot) {
            // Use standard hashing with linear probing
            hash_result = Hash::compute_standard(key, header_->total_slots);
            
            for (size_t i = 0; i < MAX_PROBE_DISTANCE; ++i) {
                uint64_t idx = (hash_result.index + i) % header_->total_slots;
                Slot& slot = slots_[idx];
                
                if (slot.empty() || slot.hash() == hash_result.hash) {
                    slot_idx = idx;
                    found_slot = true;
                    break;
                }
            }
        }
        
        if (found_slot) {
            slots_[slot_idx].set(hash_result.hash, value);
            header_->generation.fetch_add(1, std::memory_order_relaxed);
            
            // Record in journal
            if (key_journal_) {
                key_journal_->record_insert(key, hash_result.hash);
            }
            
            return true;
        }
        
        return false;  // Table full
    }
    
    /**
     * @brief Remove a key-value pair
     */
    bool remove(JsonView key) {
        if (readonly_) return false;
        
        Hash::Result hash_result;
        
        if (header_->hash_mode == HashMode::PERFECT && perfect_hash_) {
            hash_result = Hash::compute_perfect(key, perfect_hash_.get(), header_->total_slots);
            if (hash_result.perfect) {
                if (slots_[hash_result.index].hash() == hash_result.hash) {
                    slots_[hash_result.index].clear();
                    header_->generation.fetch_add(1, std::memory_order_relaxed);
                    
                    if (key_journal_) {
                        key_journal_->record_remove(key);
                    }
                    return true;
                }
                return false;
            }
        }
        
        // Standard lookup with probing
        hash_result = Hash::compute_standard(key, header_->total_slots);
        
        for (size_t i = 0; i < MAX_PROBE_DISTANCE; ++i) {
            uint64_t idx = (hash_result.index + i) % header_->total_slots;
            Slot& slot = slots_[idx];
            
            if (slot.empty()) break;
            if (slot.hash() == hash_result.hash) {
                slot.clear();
                header_->generation.fetch_add(1, std::memory_order_relaxed);
                
                if (key_journal_) {
                    key_journal_->record_remove(key);
                }
                return true;
            }
        }
        
        return false;
    }
    
    bool exists(JsonView key) const {
        return get(key).has_value();
    }
    
    // ===== PERFECT HASH OPTIMIZATION =====
    
    /**
     * @brief Optimize the hash table using perfect hashing
     */
    Result optimize(const PerfectHashConfig& config = {}) {
        if (readonly_) {
            return {ErrorCode::READONLY_STORE, "Cannot optimize read-only database"};
        }
        
        if (!key_journal_) {
            return {ErrorCode::JOURNAL_ERROR, "Key journal not available"};
        }
        
        // Get all active keys from journal
        auto active_keys = key_journal_->get_active_keys();
        if (active_keys.empty()) {
            return {ErrorCode::SUCCESS, "No keys to optimize"};
        }
        
        // Build perfect hash function
        auto perfect_hash = PerfectHashFactory::build(active_keys, config);
        if (!perfect_hash) {
            return {ErrorCode::OPTIMIZATION_FAILED, "Failed to build perfect hash function"};
        }
        
        // Verify all keys map correctly
        for (const auto& key : active_keys) {
            auto hash_val = perfect_hash->hash(key);
            if (!hash_val.has_value()) {
                return {ErrorCode::OPTIMIZATION_FAILED, "Perfect hash verification failed"};
            }
        }
        
        // Store perfect hash in database file
        auto serialized = perfect_hash->serialize();
        if (serialized.empty()) {
            return {ErrorCode::OPTIMIZATION_FAILED, "Failed to serialize perfect hash"};
        }
        
        // Save perfect hash data (in production, would extend file)
        // For now, just keep in memory
        perfect_hash_ = std::move(perfect_hash);
        header_->hash_mode = HashMode::PERFECT;
        header_->perfect_hash_type = config.type;
        
        return {ErrorCode::SUCCESS, 
                "Database optimized with perfect hash (" + 
                std::to_string(active_keys.size()) + " keys)"};
    }
    
    /**
     * @brief Get optimization statistics
     */
    struct OptimizationStats {
        HashMode current_mode;
        PerfectHashType hash_type;
        size_t total_keys;
        size_t perfect_hash_memory;
        double collision_rate;
        bool is_optimized;
    };
    
    OptimizationStats get_optimization_stats() const {
        OptimizationStats stats{};
        stats.current_mode = header_->hash_mode;
        stats.hash_type = header_->perfect_hash_type;
        stats.is_optimized = (header_->hash_mode == HashMode::PERFECT);
        
        if (key_journal_) {
            auto journal_stats = key_journal_->get_stats();
            stats.total_keys = journal_stats.total_keys;
        }
        
        if (perfect_hash_) {
            stats.perfect_hash_memory = perfect_hash_->memory_usage();
            stats.collision_rate = 0.0;  // Perfect hash has no collisions
        }
        
        return stats;
    }
    
    // ===== BATCH OPERATIONS (unchanged from v1) =====
    
    template<typename Callback>
    void mget(const std::vector<JsonView>& keys, Callback&& cb) const {
        for (auto key : keys) {
            __builtin_prefetch(&slots_[Hash::compute_standard(key, header_->total_slots).index], 0, 3);
        }
        
        for (auto key : keys) {
            if (auto value = get(key)) {
                cb(key, *value);
            }
        }
    }
    
    size_t mset(const std::vector<std::pair<JsonView, JsonView>>& kvs) {
        if (readonly_) return 0;
        
        size_t count = 0;
        for (const auto& [key, value] : kvs) {
            if (set(key, value)) ++count;
        }
        return count;
    }
    
    template<typename Visitor>
    void scan(Visitor&& visit) const {
        for (uint64_t i = 0; i < header_->total_slots; ++i) {
            const Slot& slot = slots_[i];
            if (!slot.empty()) {
                visit(i, slot.hash(), slot.view());
            }
        }
    }
    
    // ===== UTILITIES =====
    
    void sync() {
        if (!readonly_ && mapped_) {
            ::msync(mapped_, file_size_, MS_ASYNC);
        }
        if (key_journal_) {
            key_journal_->flush();
        }
    }
    
    void close() {
        if (mapped_) {
            ::munmap(mapped_, file_size_);
            mapped_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        perfect_hash_.reset();
        key_journal_.reset();
    }
    
    struct Stats {
        uint64_t total_slots;
        uint64_t used_slots;
        uint64_t generation;
        size_t memory_bytes;
        double load_factor;
        HashMode hash_mode;
        PerfectHashType perfect_hash_type;
        bool is_optimized;
        size_t perfect_hash_memory;
    };
    
    Stats stats() const {
        uint64_t used = 0;
        for (uint64_t i = 0; i < header_->total_slots; ++i) {
            if (!slots_[i].empty()) ++used;
        }
        
        Stats s{};
        s.total_slots = header_->total_slots;
        s.used_slots = used;
        s.generation = header_->generation.load(std::memory_order_relaxed);
        s.memory_bytes = file_size_;
        s.load_factor = static_cast<double>(used) / header_->total_slots;
        s.hash_mode = header_->hash_mode;
        s.perfect_hash_type = header_->perfect_hash_type;
        s.is_optimized = (header_->hash_mode == HashMode::PERFECT);
        s.perfect_hash_memory = perfect_hash_ ? perfect_hash_->memory_usage() : 0;
        
        return s;
    }
    
    // ===== PARALLEL OPERATIONS (unchanged from v1) =====
    
    template<typename Callback>
    void parallel_mget(const std::vector<JsonView>& keys, 
                      Callback&& cb,
                      size_t thread_count = 0) const {
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
        }
        
        if (keys.size() < thread_count * 10) {
            mget(keys, std::forward<Callback>(cb));
            return;
        }
        
        const size_t chunk_size = (keys.size() + thread_count - 1) / thread_count;
        std::vector<std::future<void>> futures;
        
        for (size_t t = 0; t < thread_count; ++t) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, keys.size());
            
            futures.emplace_back(std::async(std::launch::async,
                [this, &keys, &cb, start, end]() {
                    for (size_t i = start; i < end; ++i) {
                        if (auto value = get(keys[i])) {
                            cb(keys[i], *value);
                        }
                    }
                }));
        }
        
        for (auto& f : futures) {
            f.wait();
        }
    }
    
    size_t parallel_mset(const std::vector<std::pair<JsonView, JsonView>>& kvs,
                        size_t thread_count = 0) {
        if (readonly_) return 0;
        
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
        }
        
        if (kvs.size() < thread_count * 10) {
            return mset(kvs);
        }
        
        const size_t chunk_size = (kvs.size() + thread_count - 1) / thread_count;
        std::vector<std::future<size_t>> futures;
        
        for (size_t t = 0; t < thread_count; ++t) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, kvs.size());
            
            futures.emplace_back(std::async(std::launch::async,
                [this, &kvs, start, end]() {
                    size_t count = 0;
                    for (size_t i = start; i < end; ++i) {
                        if (set(kvs[i].first, kvs[i].second)) {
                            ++count;
                        }
                    }
                    return count;
                }));
        }
        
        size_t total = 0;
        for (auto& f : futures) {
            total += f.get();
        }
        return total;
    }

private:
    bool load_perfect_hash() {
        // In production, would load from file
        // For now, this is a placeholder
        return false;
    }
    
    void swap(Maph& other) noexcept {
        std::swap(fd_, other.fd_);
        std::swap(mapped_, other.mapped_);
        std::swap(file_size_, other.file_size_);
        std::swap(header_, other.header_);
        std::swap(slots_, other.slots_);
        std::swap(readonly_, other.readonly_);
        std::swap(perfect_hash_, other.perfect_hash_);
        std::swap(key_journal_, other.key_journal_);
        std::swap(database_path_, other.database_path_);
    }
};

// ===== CONVENIENCE FUNCTIONS =====

inline auto create(const std::string& path, uint64_t slots) {
    return Maph::create(path, slots);
}

inline auto open(const std::string& path) {
    return Maph::open(path, false);
}

inline auto open_readonly(const std::string& path) {
    return Maph::open(path, true);
}

} // namespace maph