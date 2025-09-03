#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace approximate {

/**
 * @brief Default decoder for set membership (original functionality)
 * 
 * Tests if stored hash matches element hash for membership testing
 */
template <typename StorageType, typename Hasher>
struct SetMembershipDecoder {
    Hasher hasher;
    
    template <typename T>
    bool operator()(StorageType stored_value, T const& element) const {
        return stored_value == static_cast<StorageType>(hasher(element));
    }
    
    static constexpr double false_positive_rate() {
        return 1.0 / std::numeric_limits<StorageType>::max();
    }
};

/**
 * @brief Threshold decoder for probabilistic membership
 * 
 * Returns true if stored value is less than threshold
 */
template <typename StorageType>
struct ThresholdDecoder {
    StorageType threshold;
    
    explicit ThresholdDecoder(StorageType t = std::numeric_limits<StorageType>::max() / 2)
        : threshold(t) {}
    
    template <typename T>
    bool operator()(StorageType stored_value, T const&) const {
        return stored_value <= threshold;
    }
    
    double false_positive_rate() const {
        return static_cast<double>(threshold) / std::numeric_limits<StorageType>::max();
    }
};

/**
 * @brief Identity decoder - returns stored value directly
 */
template <typename StorageType>
struct IdentityDecoder {
    template <typename T>
    StorageType operator()(StorageType stored_value, T const&) const {
        return stored_value;
    }
};

/**
 * @brief Approximate map using perfect hashing with configurable storage and decoding
 * 
 * This generalizes the rate-distorted perfect hash filter to support arbitrary
 * mappings X → Y with controllable accuracy and space tradeoffs.
 * 
 * @tparam PH Perfect hash function type
 * @tparam StorageType Type used for storage (uint8_t, uint32_t, etc.)
 * @tparam Decoder Function object type for decoding stored values
 * @tparam OutputType Type returned by the decoder
 */
template <typename PH, 
          typename StorageType = std::uint32_t,
          typename Decoder = SetMembershipDecoder<StorageType, typename PH::H>,
          typename OutputType = decltype(std::declval<Decoder>()(
              std::declval<StorageType>(), 
              std::declval<int>()))>
class approximate_map {
public:
    using storage_type = StorageType;
    using decoder_type = Decoder;
    using output_type = OutputType;
    using perfect_hash_type = PH;
    
private:
    PH ph_;
    std::vector<StorageType> data_;
    Decoder decoder_;
    double load_factor_;
    std::optional<OutputType> default_value_;
    
public:
    /**
     * @brief Construct approximate map with custom encoder
     * 
     * @tparam Iterator Iterator type
     * @tparam Encoder Function to encode elements to storage type
     * @param begin Start of elements
     * @param end End of elements
     * @param ph_builder Perfect hash builder
     * @param encoder Function: Element → StorageType
     * @param decoder Decoder instance
     * @param load_factor Load factor (>1.0 for sparse storage)
     */
    template <typename Iterator, typename PHBuilder, typename Encoder>
    approximate_map(Iterator begin, Iterator end,
                   PHBuilder ph_builder,
                   Encoder encoder,
                   Decoder decoder = Decoder{},
                   double load_factor = 1.0)
        : ph_(ph_builder(begin, end))
        , decoder_(decoder)
        , load_factor_(load_factor)
    {
        size_t size = static_cast<size_t>((ph_.max_hash() + 1) * load_factor);
        data_.resize(size);
        
        // Encode and store all elements
        for (auto it = begin; it != end; ++it) {
            size_t index = ph_(*it) % data_.size();
            data_[index] = encoder(*it);
        }
    }
    
    /**
     * @brief Simplified constructor for set membership (backward compatible)
     */
    template <typename Iterator, typename PHBuilder>
    approximate_map(Iterator begin, Iterator end, PHBuilder ph_builder)
        : approximate_map(begin, end, ph_builder,
                         [this](auto const& x) { 
                             return static_cast<StorageType>(ph_.hash_fn()(x)); 
                         },
                         Decoder{})
    {}
    
    /**
     * @brief Query the approximate map
     * 
     * @param element Element to query
     * @return Decoded value for the element
     */
    template <typename T>
    OutputType operator()(T const& element) const {
        size_t index = ph_(element) % data_.size();
        return decoder_(data_[index], element);
    }
    
    /**
     * @brief Set default value for unknown elements
     */
    void set_default(OutputType default_val) {
        default_value_ = default_val;
    }
    
    /**
     * @brief Get storage size in bytes
     */
    size_t storage_bytes() const {
        return data_.size() * sizeof(StorageType);
    }
    
    /**
     * @brief Get load factor
     */
    double load_factor() const {
        return load_factor_;
    }
    
    /**
     * @brief Get false negative rate from perfect hash
     */
    double fnr() const {
        return ph_.error_rate();
    }
    
    /**
     * @brief Get decoder reference for configuration
     */
    Decoder& decoder() {
        return decoder_;
    }
    
    const Decoder& decoder() const {
        return decoder_;
    }
};

/**
 * @brief Builder for approximate maps with fluent interface
 */
template <typename PH>
class approximate_map_builder {
public:
    using perfect_hash_type = PH;
    
private:
    std::function<PH(typename PH::iterator, typename PH::iterator)> ph_builder_;
    double load_factor_ = 1.0;
    size_t storage_bits_ = 32;
    
public:
    template <typename PHB>
    explicit approximate_map_builder(PHB ph_builder)
        : ph_builder_(ph_builder) {}
    
    /**
     * @brief Set load factor (>1 for sparser storage)
     */
    auto& with_load_factor(double factor) {
        load_factor_ = factor;
        return *this;
    }
    
    /**
     * @brief Set storage size in bits (8, 16, 32, 64)
     */
    auto& with_storage_bits(size_t bits) {
        storage_bits_ = bits;
        return *this;
    }
    
    /**
     * @brief Build set membership filter with 8-bit storage
     */
    template <typename Iterator>
    auto build_set_filter_8bit(Iterator begin, Iterator end) const {
        using Hasher = typename PH::H;
        using Filter = approximate_map<PH, std::uint8_t, SetMembershipDecoder<std::uint8_t, Hasher>, bool>;
        return Filter(begin, end, ph_builder_);
    }
    
    /**
     * @brief Build set membership filter with 16-bit storage
     */
    template <typename Iterator>
    auto build_set_filter_16bit(Iterator begin, Iterator end) const {
        using Hasher = typename PH::H;
        using Filter = approximate_map<PH, std::uint16_t, SetMembershipDecoder<std::uint16_t, Hasher>, bool>;
        return Filter(begin, end, ph_builder_);
    }
    
    /**
     * @brief Build set membership filter with 32-bit storage
     */
    template <typename Iterator>
    auto build_set_filter_32bit(Iterator begin, Iterator end) const {
        using Hasher = typename PH::H;
        using Filter = approximate_map<PH, std::uint32_t, SetMembershipDecoder<std::uint32_t, Hasher>, bool>;
        return Filter(begin, end, ph_builder_);
    }
    
    /**
     * @brief Build set membership filter with 64-bit storage
     */
    template <typename Iterator>
    auto build_set_filter_64bit(Iterator begin, Iterator end) const {
        using Hasher = typename PH::H;
        using Filter = approximate_map<PH, std::uint64_t, SetMembershipDecoder<std::uint64_t, Hasher>, bool>;
        return Filter(begin, end, ph_builder_);
    }
    
    /**
     * @brief Build threshold filter with configurable probability
     */
    template <typename Iterator, typename StorageType = std::uint32_t>
    auto build_threshold_filter(Iterator begin, Iterator end, double target_fpr) const {
        StorageType threshold = static_cast<StorageType>(
            target_fpr * std::numeric_limits<StorageType>::max()
        );
        
        ThresholdDecoder<StorageType> decoder(threshold);
        
        // Encoder that assigns random values, biased by membership
        auto encoder = [threshold](auto const& x) {
            // Members get values <= threshold, non-members get random
            using T = std::decay_t<decltype(x)>;
            return static_cast<StorageType>(std::hash<T>{}(x) % (threshold + 1));
        };
        
        return approximate_map<PH, StorageType, ThresholdDecoder<StorageType>, bool>(
            begin, end, ph_builder_, encoder, decoder, load_factor_
        );
    }
    
    /**
     * @brief Build arbitrary map with custom encoder/decoder
     */
    template <typename Iterator, typename StorageType, typename Encoder, typename Decoder>
    auto build_map(Iterator begin, Iterator end, 
                   Encoder encoder, Decoder decoder) const {
        return approximate_map<PH, StorageType, Decoder, 
                              decltype(decoder(std::declval<StorageType>(), *begin))>(
            begin, end, ph_builder_, encoder, decoder, load_factor_
        );
    }
    
    /**
     * @brief Build compact function approximation
     * 
     * @param domain_subset Subset of domain to map exactly
     * @param function Function to approximate: X → Y  
     * @param encoder Encode Y values to StorageType
     * @param decoder Decode StorageType to Y
     */
    template <typename Container, typename Function, 
              typename Encoder, typename Decoder, typename StorageType>
    auto build_function(const Container& domain_subset,
                       Function function,
                       Encoder encoder,
                       Decoder decoder) const {
        // Create pairs of (x, f(x))
        std::vector<std::pair<typename Container::value_type, 
                              decltype(function(*domain_subset.begin()))>> pairs;
        
        for (const auto& x : domain_subset) {
            pairs.emplace_back(x, function(x));
        }
        
        // Encoder that encodes the function value
        auto pair_encoder = [encoder](const auto& pair) {
            return encoder(pair.second);
        };
        
        return approximate_map<PH, StorageType, Decoder,
                              decltype(decoder(std::declval<StorageType>(), 
                                             *domain_subset.begin()))>(
            pairs.begin(), pairs.end(), ph_builder_, pair_encoder, decoder, load_factor_
        );
    }
};

/**
 * @brief Convenience function for backward compatibility
 */
template <typename PH>
using rd_ph_filter = approximate_map<PH, std::uint32_t, 
                                     SetMembershipDecoder<std::uint32_t, typename PH::H>, 
                                     bool>;

} // namespace approximate