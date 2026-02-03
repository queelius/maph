/**
 * @file storage.hpp
 * @brief Storage backends - orthogonal to hashing algorithms
 */

#pragma once

#include "core.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <memory>
#include <filesystem>
#include <utility>
#include <deque>
#include <unordered_map>

namespace maph {

// ===== STORAGE POLICIES =====

/**
 * @class heap_storage
 * @brief Simple heap-based storage for testing and small datasets
 *
 * Clean, simple, does one thing well
 */
template<size_t SlotSize = 512>
class heap_storage {
public:
    using slot_type = slot<SlotSize>;

private:
    std::vector<slot_type> slots_;

public:
    explicit heap_storage(slot_count count)
        : slots_(count.value) {}

    [[nodiscard]] result<value> read(slot_index idx) const noexcept {
        if (idx.value >= slots_.size()) {
            return std::unexpected(error::key_not_found);
        }
        return slots_[idx.value].get();
    }

    [[nodiscard]] status write(slot_index idx, hash_value hash, std::span<const std::byte> data) noexcept {
        if (idx.value >= slots_.size()) {
            return std::unexpected(error::key_not_found);
        }
        return slots_[idx.value].set(hash, data);
    }

    [[nodiscard]] status clear(slot_index idx) noexcept {
        if (idx.value >= slots_.size()) {
            return std::unexpected(error::key_not_found);
        }
        slots_[idx.value].clear();
        return {};
    }

    [[nodiscard]] constexpr maph::slot_count get_slot_count() const noexcept {
        return maph::slot_count{slots_.size()};
    }

    [[nodiscard]] bool empty(slot_index idx) const noexcept {
        return idx.value < slots_.size() && slots_[idx.value].empty();
    }

    [[nodiscard]] hash_value hash_at(slot_index idx) const noexcept {
        if (idx.value >= slots_.size()) {
            return hash_value{0};
        }
        return slots_[idx.value].hash();
    }
};

/**
 * @class mmap_storage
 * @brief Memory-mapped storage with RAII and strong typing
 *
 * Handles one responsibility: memory-mapped file management
 */
template<size_t SlotSize = 512>
class mmap_storage {
    using slot_type = slot<SlotSize>;

    // RAII wrapper for file descriptor
    class file_descriptor {
        int fd_{-1};
    public:
        explicit file_descriptor(int fd) noexcept : fd_(fd) {}
        ~file_descriptor() { if (fd_ >= 0) ::close(fd_); }

        file_descriptor(file_descriptor&& other) noexcept
            : fd_(std::exchange(other.fd_, -1)) {}

        file_descriptor& operator=(file_descriptor&& other) noexcept {
            if (this != &other) {
                if (fd_ >= 0) ::close(fd_);
                fd_ = std::exchange(other.fd_, -1);
            }
            return *this;
        }

        int get() const noexcept { return fd_; }
        int release() noexcept { return std::exchange(fd_, -1); }
    };

    // RAII wrapper for memory mapping
    class memory_map {
        void* addr_{nullptr};
        size_t size_{0};

    public:
        memory_map(void* addr, size_t size) noexcept
            : addr_(addr), size_(size) {}

        ~memory_map() {
            if (addr_) ::munmap(addr_, size_);
        }

        memory_map(memory_map&& other) noexcept
            : addr_(std::exchange(other.addr_, nullptr)),
              size_(std::exchange(other.size_, 0)) {}

        memory_map& operator=(memory_map&& other) noexcept {
            if (this != &other) {
                if (addr_) ::munmap(addr_, size_);
                addr_ = std::exchange(other.addr_, nullptr);
                size_ = std::exchange(other.size_, 0);
            }
            return *this;
        }

        void* get() const noexcept { return addr_; }
        size_t size() const noexcept { return size_; }
    };

    struct header {
        uint32_t magic;
        uint32_t version;
        uint64_t slot_count;
        char reserved[496];
    };

    file_descriptor fd_;
    memory_map map_;
    header* header_{nullptr};
    slot_type* slots_{nullptr};
    bool readonly_{false};

public:
    // Factory methods for clean construction
    [[nodiscard]] static result<mmap_storage> create(
        const std::filesystem::path& path,
        slot_count count) {

        size_t file_size = sizeof(header) + count.value * sizeof(slot_type);

        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            return std::unexpected(error::io_error);
        }

        file_descriptor fd_guard{fd};

        if (::ftruncate(fd, file_size) < 0) {
            return std::unexpected(error::io_error);
        }

        void* addr = ::mmap(nullptr, file_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            return std::unexpected(error::io_error);
        }

        memory_map map{addr, file_size};

        // Initialize header
        auto* h = new (addr) header{
            .magic = 0x4D415048,
            .version = 3,
            .slot_count = count.value
        };

        // Initialize slots
        auto* slots = reinterpret_cast<slot_type*>(
            static_cast<char*>(addr) + sizeof(header));
        std::uninitialized_default_construct_n(slots, count.value);

        return mmap_storage{
            std::move(fd_guard),
            std::move(map),
            h,
            slots,
            false
        };
    }

    [[nodiscard]] static result<mmap_storage> open(
        const std::filesystem::path& path,
        bool readonly = false) {

        int flags = readonly ? O_RDONLY : O_RDWR;
        int fd = ::open(path.c_str(), flags);
        if (fd < 0) {
            return std::unexpected(error::io_error);
        }

        file_descriptor fd_guard{fd};

        struct stat st;
        if (::fstat(fd, &st) < 0) {
            return std::unexpected(error::io_error);
        }

        int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
        void* addr = ::mmap(nullptr, st.st_size, prot, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            return std::unexpected(error::io_error);
        }

        memory_map map{addr, static_cast<size_t>(st.st_size)};

        auto* h = static_cast<header*>(addr);
        if (h->magic != 0x4D415048) {
            return std::unexpected(error::invalid_format);
        }

        auto* slots = reinterpret_cast<slot_type*>(
            static_cast<char*>(addr) + sizeof(header));

        return mmap_storage{
            std::move(fd_guard),
            std::move(map),
            h,
            slots,
            readonly
        };
    }

    // Storage interface implementation
    [[nodiscard]] result<value> read(slot_index idx) const noexcept {
        if (idx.value >= header_->slot_count) {
            return std::unexpected(error::key_not_found);
        }
        return slots_[idx.value].get();
    }

    [[nodiscard]] status write(slot_index idx, hash_value hash, std::span<const std::byte> data) noexcept {
        if (readonly_) {
            return std::unexpected(error::permission_denied);
        }
        if (idx.value >= header_->slot_count) {
            return std::unexpected(error::key_not_found);
        }
        return slots_[idx.value].set(hash, data);
    }

    [[nodiscard]] status clear(slot_index idx) noexcept {
        if (readonly_) {
            return std::unexpected(error::permission_denied);
        }
        if (idx.value >= header_->slot_count) {
            return std::unexpected(error::key_not_found);
        }
        slots_[idx.value].clear();
        return {};
    }

    [[nodiscard]] maph::slot_count get_slot_count() const noexcept {
        return maph::slot_count{header_->slot_count};
    }

    [[nodiscard]] bool empty(slot_index idx) const noexcept {
        return idx.value < header_->slot_count && slots_[idx.value].empty();
    }

    [[nodiscard]] hash_value hash_at(slot_index idx) const noexcept {
        if (idx.value >= header_->slot_count) {
            return hash_value{0};
        }
        return slots_[idx.value].hash();
    }

    // Additional utilities
    void sync() noexcept {
        if (!readonly_ && map_.get()) {
            ::msync(map_.get(), map_.size(), MS_ASYNC);
        }
    }

private:
    mmap_storage(file_descriptor&& fd, memory_map&& map,
                header* h, slot_type* slots, bool readonly)
        : fd_(std::move(fd)), map_(std::move(map)),
          header_(h), slots_(slots), readonly_(readonly) {}
};

/**
 * @class cached_storage
 * @brief Decorator that adds caching to any storage backend
 *
 * Composable caching layer - works with any storage.
 *
 * @warning NOT thread-safe. The internal cache (mutable unordered_map) is not
 * protected by any synchronization. If concurrent access is needed, wrap this
 * class with external locking or use one instance per thread.
 */
template<typename S>
class cached_storage {
    S backend_;
    mutable std::unordered_map<uint64_t, value> cache_;
    size_t max_cache_size_;

public:
    explicit cached_storage(S backend, size_t max_size = 1000)
        : backend_(std::move(backend)), max_cache_size_(max_size) {}

    [[nodiscard]] result<value> read(slot_index idx) const noexcept {
        // Check cache first
        if (auto it = cache_.find(idx.value); it != cache_.end()) {
            return it->second;
        }

        // Read from backend
        auto result = backend_.read(idx);
        if (result) {
            // Cache the value if there's room
            if (cache_.size() < max_cache_size_) {
                cache_[idx.value] = *result;
            }
        }
        return result;
    }

    [[nodiscard]] status write(slot_index idx, hash_value hash, std::span<const std::byte> data) noexcept {
        // Write through to backend
        auto status = backend_.write(idx, hash, data);
        if (status) {
            // Update cache
            cache_[idx.value] = value{data};
        }
        return status;
    }

    [[nodiscard]] status clear(slot_index idx) noexcept {
        cache_.erase(idx.value);
        return backend_.clear(idx);
    }

    [[nodiscard]] maph::slot_count get_slot_count() const noexcept {
        return backend_.get_slot_count();
    }

    [[nodiscard]] bool empty(slot_index idx) const noexcept {
        return backend_.empty(idx);
    }

    [[nodiscard]] hash_value hash_at(slot_index idx) const noexcept {
        return backend_.hash_at(idx);
    }

    void clear_cache() noexcept {
        cache_.clear();
    }
};

} // namespace maph