#pragma once

#include <memory>
#include <functional>
#include <optional>
#include "rd_ph_filter.hpp"

namespace bernoulli {

/**
 * @brief Builder class for constructing rd_ph_filter with fluent interface
 * 
 * This builder provides a fluent API for configuring and constructing
 * rate-distorted perfect hash filters. It supports method chaining and
 * allows for flexible configuration of the underlying perfect hash function.
 * 
 * @tparam PH Type of the perfect hash function
 */
template <typename PH>
class rd_ph_filter_builder {
public:
    using filter_type = rd_ph_filter<PH>;
    using hash_type = typename PH::H::hash_type;
    using builder_func = std::function<PH(typename PH::iterator, typename PH::iterator)>;
    
private:
    builder_func ph_builder_;
    std::optional<double> target_fpr_;
    std::optional<double> target_fnr_;
    std::optional<size_t> max_iterations_;
    std::optional<size_t> space_overhead_;
    
public:
    /**
     * @brief Construct a new builder with a perfect hash function builder
     * 
     * @param ph_builder Function that constructs PH from iterator range
     */
    explicit rd_ph_filter_builder(builder_func ph_builder)
        : ph_builder_(std::move(ph_builder))
        , target_fpr_(std::nullopt)
        , target_fnr_(std::nullopt)
        , max_iterations_(std::nullopt)
        , space_overhead_(std::nullopt)
    {}
    
    /**
     * @brief Set target false positive rate
     * 
     * @param rate Target false positive rate (0.0 to 1.0)
     * @return Reference to builder for chaining
     */
    rd_ph_filter_builder& with_target_fpr(double rate) {
        target_fpr_ = rate;
        return *this;
    }
    
    /**
     * @brief Set target false negative rate
     * 
     * @param rate Target false negative rate (0.0 to 1.0)
     * @return Reference to builder for chaining
     */
    rd_ph_filter_builder& with_target_fnr(double rate) {
        target_fnr_ = rate;
        return *this;
    }
    
    /**
     * @brief Set maximum iterations for perfect hash construction
     * 
     * @param iterations Maximum number of iterations
     * @return Reference to builder for chaining
     */
    rd_ph_filter_builder& with_max_iterations(size_t iterations) {
        max_iterations_ = iterations;
        return *this;
    }
    
    /**
     * @brief Set space overhead factor
     * 
     * @param factor Space overhead factor (>1.0)
     * @return Reference to builder for chaining
     */
    rd_ph_filter_builder& with_space_overhead(size_t factor) {
        space_overhead_ = factor;
        return *this;
    }
    
    /**
     * @brief Build filter from iterator range
     * 
     * @tparam I Iterator type
     * @param begin Start of element range
     * @param end End of element range
     * @return Constructed filter
     */
    template <typename I>
    filter_type build(I begin, I end) const {
        // Apply configuration to builder if needed
        auto configured_builder = configure_builder();
        return filter_type(begin, end, configured_builder);
    }
    
    /**
     * @brief Build filter from container
     * 
     * @tparam Container Container type with begin() and end() methods
     * @param container Container of elements
     * @return Constructed filter
     */
    template <typename Container>
    filter_type build_from(const Container& container) const {
        return build(std::begin(container), std::end(container));
    }
    
    /**
     * @brief Build filter from initializer list
     * 
     * @tparam T Element type
     * @param elements Initializer list of elements
     * @return Constructed filter
     */
    template <typename T>
    filter_type build_from(std::initializer_list<T> elements) const {
        return build(elements.begin(), elements.end());
    }
    
    /**
     * @brief Reset builder to default configuration
     * 
     * @return Reference to builder for chaining
     */
    rd_ph_filter_builder& reset() {
        target_fpr_ = std::nullopt;
        target_fnr_ = std::nullopt;
        max_iterations_ = std::nullopt;
        space_overhead_ = std::nullopt;
        return *this;
    }
    
    /**
     * @brief Clone builder with current configuration
     * 
     * @return New builder instance with same configuration
     */
    rd_ph_filter_builder clone() const {
        return *this;
    }
    
private:
    builder_func configure_builder() const {
        // In a real implementation, this would apply the configuration
        // options to the PH builder. For now, we return the original.
        return ph_builder_;
    }
};

/**
 * @brief Factory function to create a builder
 * 
 * @tparam PH Perfect hash function type
 * @param ph_builder Function that constructs PH from iterator range
 * @return Builder instance
 */
template <typename PH>
auto make_filter_builder(typename rd_ph_filter_builder<PH>::builder_func ph_builder) {
    return rd_ph_filter_builder<PH>(std::move(ph_builder));
}

/**
 * @brief Fluent API wrapper for rd_ph_filter operations
 * 
 * This class provides a fluent interface for querying and manipulating
 * rd_ph_filter instances.
 * 
 * @tparam PH Perfect hash function type
 */
template <typename PH>
class rd_ph_filter_query {
private:
    const rd_ph_filter<PH>* filter_;
    
public:
    explicit rd_ph_filter_query(const rd_ph_filter<PH>& filter)
        : filter_(&filter) {}
    
    /**
     * @brief Test if element is member of set
     * 
     * @tparam X Element type
     * @param x Element to test
     * @return Query object with result
     */
    template <typename X>
    auto contains(const X& x) const {
        return (*filter_)(x);
    }
    
    /**
     * @brief Test multiple elements for membership
     * 
     * @tparam Container Container type
     * @param elements Container of elements to test
     * @return Vector of membership results
     */
    template <typename Container>
    auto contains_all(const Container& elements) const {
        std::vector<bool> results;
        results.reserve(std::size(elements));
        for (const auto& elem : elements) {
            results.push_back(contains(elem));
        }
        return results;
    }
    
    /**
     * @brief Test if any of the elements are members
     * 
     * @tparam Container Container type
     * @param elements Container of elements to test
     * @return True if any element is a member
     */
    template <typename Container>
    bool contains_any(const Container& elements) const {
        for (const auto& elem : elements) {
            if (contains(elem)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Count how many elements are members
     * 
     * @tparam Container Container type
     * @param elements Container of elements to test
     * @return Number of elements that are members
     */
    template <typename Container>
    size_t count_members(const Container& elements) const {
        size_t count = 0;
        for (const auto& elem : elements) {
            if (contains(elem)) {
                ++count;
            }
        }
        return count;
    }
    
    /**
     * @brief Get false positive rate
     * 
     * @return False positive rate
     */
    double false_positive_rate() const {
        return filter_->fpr();
    }
    
    /**
     * @brief Get false negative rate
     * 
     * @return False negative rate
     */
    double false_negative_rate() const {
        return filter_->fnr();
    }
    
    /**
     * @brief Get accuracy (1 - (FPR + FNR))
     * 
     * @return Overall accuracy
     */
    double accuracy() const {
        return 1.0 - (false_positive_rate() + false_negative_rate());
    }
    
    /**
     * @brief Create a query with a different filter
     * 
     * @param other Other filter to query
     * @return New query object
     */
    rd_ph_filter_query with_filter(const rd_ph_filter<PH>& other) const {
        return rd_ph_filter_query(other);
    }
};

/**
 * @brief Create a query object for fluent API
 * 
 * @tparam PH Perfect hash function type
 * @param filter Filter to query
 * @return Query object
 */
template <typename PH>
auto query(const rd_ph_filter<PH>& filter) {
    return rd_ph_filter_query<PH>(filter);
}

/**
 * @brief Batch operations on rd_ph_filter
 * 
 * This class provides batch operations for efficient processing
 * of multiple queries.
 * 
 * @tparam PH Perfect hash function type
 */
template <typename PH>
class rd_ph_filter_batch {
private:
    std::vector<rd_ph_filter<PH>> filters_;
    
public:
    /**
     * @brief Add a filter to the batch
     * 
     * @param filter Filter to add
     * @return Reference to batch for chaining
     */
    rd_ph_filter_batch& add(rd_ph_filter<PH> filter) {
        filters_.push_back(std::move(filter));
        return *this;
    }
    
    /**
     * @brief Test element against all filters
     * 
     * @tparam X Element type
     * @param x Element to test
     * @return Vector of membership results
     */
    template <typename X>
    std::vector<bool> test_all(const X& x) const {
        std::vector<bool> results;
        results.reserve(filters_.size());
        for (const auto& filter : filters_) {
            results.push_back(filter(x));
        }
        return results;
    }
    
    /**
     * @brief Test if element is in any filter
     * 
     * @tparam X Element type
     * @param x Element to test
     * @return True if element is in any filter
     */
    template <typename X>
    bool test_any(const X& x) const {
        for (const auto& filter : filters_) {
            if (filter(x)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Get number of filters in batch
     * 
     * @return Number of filters
     */
    size_t size() const {
        return filters_.size();
    }
    
    /**
     * @brief Clear all filters from batch
     * 
     * @return Reference to batch for chaining
     */
    rd_ph_filter_batch& clear() {
        filters_.clear();
        return *this;
    }
};

} // namespace bernoulli