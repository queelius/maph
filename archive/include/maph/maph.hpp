/**
 * @file maph.hpp
 * @brief Lean, mean, mmap-based perfect hash JSON mapping
 * 
 * Philosophy: Speed at every decision point
 * - mmap for zero-copy access
 * - Fixed slots for O(1) everything
 * - Perfect hash for direct indexing
 * - JSON for flexibility (only overhead we accept)
 */

#pragma once

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <memory>
#include <atomic>
#include <functional>

namespace maph {

// Fixed-size slot: 512 bytes aligned for cache
struct alignas(512) Slot {
    std::atomic<uint32_t> version{0};  // Lock-free updates
    uint32_t size{0};                  // Actual JSON size
    uint32_t hash{0};                  // Key hash
    uint32_t reserved{0};              // Padding
    char data[496]{};                  // JSON data
    
    static constexpr size_t MAX_JSON_SIZE = 496;
};

static_assert(sizeof(Slot) == 512, "Slot must be exactly 512 bytes");

// File header
struct Header {
    uint32_t magic{0x4D415048};        // "MAPH"
    uint32_t version{1};
    uint64_t num_slots{0};
    uint64_t slot_size{sizeof(Slot)};
    std::atomic<uint64_t> generation{0};
    char padding[488]{};
};

static_assert(sizeof(Header) == 512, "Header must be exactly 512 bytes");

/**
 * Main maph class - lean and fast
 */
class Maph {
private:
    int fd_{-1};
    void* mapped_{nullptr};
    size_t file_size_{0};
    Header* header_{nullptr};
    Slot* slots_{nullptr};
    bool readonly_{false};
    
    // Perfect hash function (pluggable)
    std::function<uint32_t(const std::string&)> hash_fn_;
    
public:
    Maph() = default;
    ~Maph() { close(); }
    
    // No copy, move only
    Maph(const Maph&) = delete;
    Maph& operator=(const Maph&) = delete;
    Maph(Maph&& other) noexcept { *this = std::move(other); }
    Maph& operator=(Maph&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            mapped_ = other.mapped_;
            file_size_ = other.file_size_;
            header_ = other.header_;
            slots_ = other.slots_;
            readonly_ = other.readonly_;
            hash_fn_ = std::move(other.hash_fn_);
            other.fd_ = -1;
            other.mapped_ = nullptr;
        }
        return *this;
    }
    
    // ===== CREATION & OPENING =====
    
    static std::unique_ptr<Maph> create(const std::string& path, 
                                        size_t num_slots) {
        auto m = std::make_unique<Maph>();
        
        size_t file_size = sizeof(Header) + (num_slots * sizeof(Slot));
        
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) return nullptr;
        
        if (ftruncate(fd, file_size) < 0) {
            ::close(fd);
            return nullptr;
        }
        
        void* mapped = mmap(nullptr, file_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            return nullptr;
        }
        
        // Initialize header
        Header* header = new (mapped) Header();
        header->num_slots = num_slots;
        
        // Zero slots
        Slot* slots = reinterpret_cast<Slot*>(
            static_cast<char*>(mapped) + sizeof(Header));
        memset(slots, 0, num_slots * sizeof(Slot));
        
        m->fd_ = fd;
        m->mapped_ = mapped;
        m->file_size_ = file_size;
        m->header_ = header;
        m->slots_ = slots;
        m->readonly_ = false;
        
        // Default hash function (can be overridden)
        m->hash_fn_ = [num_slots](const std::string& key) {
            std::hash<std::string> h;
            return h(key) % num_slots;
        };
        
        return m;
    }
    
    static std::unique_ptr<Maph> open(const std::string& path,
                                      bool readonly = false) {
        auto m = std::make_unique<Maph>();
        
        int flags = readonly ? O_RDONLY : O_RDWR;
        int fd = ::open(path.c_str(), flags);
        if (fd < 0) return nullptr;
        
        struct stat st;
        if (fstat(fd, &st) < 0) {
            ::close(fd);
            return nullptr;
        }
        
        int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
        void* mapped = mmap(nullptr, st.st_size, prot, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            return nullptr;
        }
        
        Header* header = static_cast<Header*>(mapped);
        if (header->magic != 0x4D415048) {
            munmap(mapped, st.st_size);
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
        
        // Default hash
        size_t num_slots = header->num_slots;
        m->hash_fn_ = [num_slots](const std::string& key) {
            std::hash<std::string> h;
            return h(key) % num_slots;
        };
        
        return m;
    }
    
    // ===== CORE OPERATIONS (FAST!) =====
    
    // Get value for JSON key - direct memory access
    std::string get(const std::string& json_key) const {
        uint32_t index = hash_fn_(json_key);
        if (index >= header_->num_slots) return "";
        
        const Slot& slot = slots_[index];
        if (slot.size == 0) return "";
        
        // Optional: verify hash matches
        // if (slot.hash != simple_hash(json_key)) return "";
        
        return std::string(slot.data, slot.size);
    }
    
    // Set value - just memcpy
    bool set(const std::string& json_key, const std::string& json_value) {
        if (readonly_) return false;
        if (json_value.size() > Slot::MAX_JSON_SIZE) return false;
        
        uint32_t index = hash_fn_(json_key);
        if (index >= header_->num_slots) return false;
        
        Slot& slot = slots_[index];
        
        // Lock-free update
        uint32_t v = slot.version.fetch_add(1);
        
        slot.size = json_value.size();
        slot.hash = simple_hash(json_key);
        memcpy(slot.data, json_value.data(), json_value.size());
        
        slot.version.fetch_add(1);
        header_->generation.fetch_add(1);
        
        return true;
    }
    
    // Check existence - even faster
    bool exists(const std::string& json_key) const {
        uint32_t index = hash_fn_(json_key);
        if (index >= header_->num_slots) return false;
        return slots_[index].size > 0;
    }
    
    // Delete entry
    bool remove(const std::string& json_key) {
        if (readonly_) return false;
        
        uint32_t index = hash_fn_(json_key);
        if (index >= header_->num_slots) return false;
        
        Slot& slot = slots_[index];
        slot.version.fetch_add(1);
        slot.size = 0;
        slot.version.fetch_add(1);
        header_->generation.fetch_add(1);
        
        return true;
    }
    
    // ===== BATCH OPERATIONS =====
    
    // Prefetch and batch get
    std::vector<std::string> mget(const std::vector<std::string>& keys) const {
        std::vector<std::string> results;
        results.reserve(keys.size());
        
        // Prefetch all slots into cache
        for (const auto& key : keys) {
            uint32_t index = hash_fn_(key);
            if (index < header_->num_slots) {
                __builtin_prefetch(&slots_[index], 0, 3);
            }
        }
        
        // Now read (from cache)
        for (const auto& key : keys) {
            results.push_back(get(key));
        }
        
        return results;
    }
    
    // ===== UTILITIES =====
    
    void set_hash_function(std::function<uint32_t(const std::string&)> fn) {
        hash_fn_ = std::move(fn);
    }
    
    size_t size() const { return header_->num_slots; }
    
    size_t used() const {
        size_t count = 0;
        for (size_t i = 0; i < header_->num_slots; ++i) {
            if (slots_[i].size > 0) count++;
        }
        return count;
    }
    
    uint64_t generation() const { return header_->generation.load(); }
    
    void sync() {
        if (!readonly_ && mapped_) {
            msync(mapped_, file_size_, MS_ASYNC);
        }
    }
    
    void close() {
        if (mapped_) {
            munmap(mapped_, file_size_);
            mapped_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
private:
    static uint32_t simple_hash(const std::string& s) {
        uint32_t h = 0;
        for (char c : s) {
            h = h * 31 + c;
        }
        return h;
    }
};

// ===== FLUENT API =====

class MaphBuilder {
private:
    std::string path_;
    size_t num_slots_{1000000};
    std::function<uint32_t(const std::string&)> hash_fn_;
    
public:
    MaphBuilder& path(const std::string& p) {
        path_ = p;
        return *this;
    }
    
    MaphBuilder& slots(size_t n) {
        num_slots_ = n;
        return *this;
    }
    
    MaphBuilder& hash(std::function<uint32_t(const std::string&)> fn) {
        hash_fn_ = std::move(fn);
        return *this;
    }
    
    std::unique_ptr<Maph> build() {
        auto m = Maph::create(path_, num_slots_);
        if (m && hash_fn_) {
            m->set_hash_function(std::move(hash_fn_));
        }
        return m;
    }
};

// Convenience functions
inline std::unique_ptr<Maph> open(const std::string& path) {
    return Maph::open(path);
}

inline std::unique_ptr<Maph> create(const std::string& path, size_t slots) {
    return Maph::create(path, slots);
}

} // namespace maph