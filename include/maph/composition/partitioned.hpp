/**
 * @file partitioned.hpp
 * @brief Sharded perfect-hash composition for trivially-parallel builds.
 *
 * partitioned_phf<Inner> shards keys by a first-level hash into P groups,
 * builds one Inner PHF per shard in parallel, and presents a unified
 * slot_for() that returns shard_offset + inner.slot_for. Each shard is
 * independent so build time scales near-linearly with threads; the ceiling
 * is set by the largest shard's build time.
 *
 * When every inner PHF is minimal (range_size == num_keys), the partitioned
 * PHF is also minimal. If inner builds bump alpha to succeed, the outer
 * total range may exceed num_keys.
 *
 * Serialization embeds each shard's bytes with a length prefix.
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/serialization.hpp"
#include "../detail/hash.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace maph {

/**
 * partitioned_phf: shards keys into P groups, builds one Inner PHF per shard,
 * presents a unified slot_for that returns shard_offset + inner.slot_for.
 *
 * Build time scales near-linearly with threads (each shard is independent);
 * the ceiling is set by the largest shard's build time.
 *
 * When every inner PHF is minimal (range_size == num_keys), the partitioned
 * PHF is also minimal. If inner builds bump alpha, the outer total range may
 * exceed num_keys.
 *
 * Serialization embeds each shard's bytes with a length prefix.
 */
template<typename Inner>
class partitioned_phf {
    static_assert(perfect_hash_function<Inner>,
        "partitioned_phf Inner must satisfy perfect_hash_function");

public:
    class builder;

private:
    std::vector<Inner> shards_;
    std::vector<uint64_t> offsets_;  // size shards_.size() + 1; offsets_[i] = start of shard i's range
    uint64_t seed_{0};
    size_t num_keys_{0};
    size_t range_size_{0};
    size_t num_shards_{0};

    static uint64_t shard_hash(std::string_view key, uint64_t seed) noexcept {
        uint64_t h = phf_hash_with_seed(key, seed ^ 0x3a2e0c73b8b6c7d1ULL);
        return phf_remix(h);
    }

    size_t shard_for(std::string_view key) const noexcept {
        return static_cast<size_t>(shard_hash(key, seed_) % num_shards_);
    }

public:
    partitioned_phf() = default;
    partitioned_phf(partitioned_phf&&) = default;
    partitioned_phf& operator=(partitioned_phf&&) = default;

    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        size_t s = shard_for(key);
        uint64_t local = shards_[s].slot_for(key).value;
        return slot_index{offsets_[s] + local};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return num_keys_; }
    [[nodiscard]] size_t range_size() const noexcept { return range_size_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (num_keys_ == 0) return 0.0;
        return static_cast<double>(memory_bytes() * 8) / static_cast<double>(num_keys_);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        size_t total = sizeof(*this)
                     + offsets_.size() * sizeof(uint64_t)
                     + shards_.size() * sizeof(Inner);  // struct overhead per shard
        for (const auto& sh : shards_) total += sh.memory_bytes();
        return total;
    }

    static constexpr uint32_t ALGORITHM_ID = 7;

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID);
        phf_serial::append(out, seed_);
        phf_serial::append(out, static_cast<uint64_t>(num_keys_));
        phf_serial::append(out, static_cast<uint64_t>(range_size_));
        phf_serial::append(out, static_cast<uint64_t>(num_shards_));

        // Offsets
        phf_serial::append_vector(out, offsets_);

        // Each shard: length-prefixed serialized bytes
        for (const auto& sh : shards_) {
            auto bytes = sh.serialize();
            phf_serial::append(out, static_cast<uint64_t>(bytes.size()));
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
        return out;
    }

    [[nodiscard]] static result<partitioned_phf> deserialize(std::span<const std::byte> data) {
        phf_serial::reader rd(data);
        if (!phf_serial::verify_header(rd, ALGORITHM_ID)) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t seed{}, nkeys{}, rsize{}, nshards{};
        if (!rd.read(seed) || !rd.read(nkeys) ||
            !rd.read(rsize) || !rd.read(nshards)) {
            return std::unexpected(error::invalid_format);
        }

        partitioned_phf r;
        r.seed_ = seed;
        r.num_keys_ = static_cast<size_t>(nkeys);
        r.range_size_ = static_cast<size_t>(rsize);
        r.num_shards_ = static_cast<size_t>(nshards);

        if (!rd.read_vector(r.offsets_)) return std::unexpected(error::invalid_format);
        if (r.offsets_.size() != r.num_shards_ + 1) return std::unexpected(error::invalid_format);

        r.shards_.reserve(r.num_shards_);
        for (size_t i = 0; i < r.num_shards_; ++i) {
            uint64_t len{};
            if (!rd.read(len)) return std::unexpected(error::invalid_format);
            std::span<const std::byte> shard_span;
            if (!rd.read_span(shard_span, static_cast<size_t>(len))) {
                return std::unexpected(error::invalid_format);
            }
            auto shard = Inner::deserialize(shard_span);
            if (!shard.has_value()) return std::unexpected(shard.error());
            r.shards_.push_back(std::move(*shard));
        }
        return r;
    }

    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0xac32e5f5b3a8a3d7ULL};
        size_t num_shards_{0};  // 0 = auto (target ~15000 keys/shard)
        size_t threads_{0};     // 0 = auto (hardware_concurrency)

    public:
        builder() = default;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& add_all(std::span<const std::string> keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        builder& with_shards(size_t P) {
            num_shards_ = P;
            return *this;
        }

        builder& with_threads(size_t n) {
            threads_ = n;
            return *this;
        }

        [[nodiscard]] result<partitioned_phf> build() {
            if (keys_.empty()) return std::unexpected(error::optimization_failed);

            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
            size_t n = keys_.size();

            size_t P = num_shards_;
            if (P == 0) {
                // Target ~15000 keys per shard; clamp to reasonable range.
                P = std::max<size_t>(1, (n + 14999) / 15000);
            }
            if (P > n) P = n;  // can't have empty shards in the scheme we use
            if (P == 0) P = 1;

            size_t nthreads = threads_;
            if (nthreads == 0) {
                nthreads = std::max<size_t>(1u, std::thread::hardware_concurrency());
            }

            // Partition keys into shards.
            std::vector<std::vector<std::string>> shard_keys(P);
            for (auto& k : keys_) {
                size_t s = static_cast<size_t>(shard_hash(k, seed_) % P);
                shard_keys[s].push_back(std::move(k));
            }

            // Build shards in parallel.
            std::vector<Inner> shards(P);
            std::atomic<size_t> next_shard{0};
            std::atomic<bool> failed{false};

            auto worker = [&]() {
                while (!failed.load(std::memory_order_acquire)) {
                    size_t i = next_shard.fetch_add(1, std::memory_order_relaxed);
                    if (i >= P) break;
                    // Derive per-shard seed so each shard's builds are independent.
                    uint64_t shard_seed = seed_ + i * 0x9e3779b97f4a7c15ULL;
                    auto built = typename Inner::builder{}
                        .add_all(shard_keys[i])
                        .with_seed(shard_seed)
                        .build();
                    if (!built.has_value()) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                    shards[i] = std::move(*built);
                }
            };

            std::vector<std::thread> pool;
            pool.reserve(nthreads);
            for (size_t t = 0; t < nthreads; ++t) pool.emplace_back(worker);
            for (auto& th : pool) th.join();

            if (failed.load(std::memory_order_acquire)) {
                return std::unexpected(error::optimization_failed);
            }

            // Compute prefix-sum offsets.
            std::vector<uint64_t> offsets(P + 1, 0);
            for (size_t i = 0; i < P; ++i) {
                offsets[i + 1] = offsets[i] + static_cast<uint64_t>(shards[i].range_size());
            }

            partitioned_phf r;
            r.shards_ = std::move(shards);
            r.offsets_ = std::move(offsets);
            r.seed_ = seed_;
            r.num_keys_ = n;
            r.range_size_ = static_cast<size_t>(r.offsets_.back());
            r.num_shards_ = P;
            return r;
        }
    };
};

} // namespace maph
