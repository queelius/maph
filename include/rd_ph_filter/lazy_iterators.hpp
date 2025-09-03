#pragma once

#include <iterator>
#include <functional>
#include <optional>
#include <cmath>

namespace approximate {

/**
 * @brief Iterator that lazily generates values from a function
 * 
 * Instead of storing all values, compute them on-the-fly during iteration
 */
template <typename T, typename Generator>
class lazy_generator_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T;
    
private:
    Generator gen_;
    std::size_t index_;
    std::size_t count_;
    mutable std::optional<T> current_;
    
public:
    lazy_generator_iterator(Generator gen, std::size_t index, std::size_t count)
        : gen_(gen), index_(index), count_(count) {}
    
    reference operator*() const {
        if (!current_ || index_ < count_) {
            current_ = gen_(index_);
        }
        return *current_;
    }
    
    lazy_generator_iterator& operator++() {
        ++index_;
        current_.reset();
        return *this;
    }
    
    lazy_generator_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const lazy_generator_iterator& other) const {
        return index_ == other.index_;
    }
    
    bool operator!=(const lazy_generator_iterator& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Range for lazy generation
 */
template <typename T, typename Generator>
class lazy_range {
private:
    Generator gen_;
    std::size_t size_;
    
public:
    using iterator = lazy_generator_iterator<T, Generator>;
    
    lazy_range(Generator gen, std::size_t size)
        : gen_(gen), size_(size) {}
    
    iterator begin() const {
        return iterator(gen_, 0, size_);
    }
    
    iterator end() const {
        return iterator(gen_, size_, size_);
    }
};

/**
 * @brief Create a lazy range from a generator function
 */
template <typename T, typename Generator>
auto make_lazy_range(Generator gen, std::size_t size) {
    return lazy_range<T, Generator>(gen, size);
}

/**
 * @brief Iterator that filters another iterator based on a predicate
 */
template <typename BaseIterator, typename Predicate>
class filter_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename std::iterator_traits<BaseIterator>::value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type;
    
private:
    BaseIterator current_;
    BaseIterator end_;
    Predicate pred_;
    
    void advance_to_next_valid() {
        while (current_ != end_ && !pred_(*current_)) {
            ++current_;
        }
    }
    
public:
    filter_iterator(BaseIterator current, BaseIterator end, Predicate pred)
        : current_(current), end_(end), pred_(pred) {
        advance_to_next_valid();
    }
    
    value_type operator*() const {
        return *current_;
    }
    
    filter_iterator& operator++() {
        ++current_;
        advance_to_next_valid();
        return *this;
    }
    
    filter_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const filter_iterator& other) const {
        return current_ == other.current_;
    }
    
    bool operator!=(const filter_iterator& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Transform iterator that applies a function to each element
 */
template <typename BaseIterator, typename Transform>
class transform_iterator {
public:
    using base_value_type = typename std::iterator_traits<BaseIterator>::value_type;
    using iterator_category = std::forward_iterator_tag;
    using value_type = decltype(std::declval<Transform>()(std::declval<base_value_type>()));
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type;
    
private:
    BaseIterator current_;
    Transform transform_;
    
public:
    transform_iterator(BaseIterator current, Transform transform)
        : current_(current), transform_(transform) {}
    
    reference operator*() const {
        return transform_(*current_);
    }
    
    transform_iterator& operator++() {
        ++current_;
        return *this;
    }
    
    transform_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const transform_iterator& other) const {
        return current_ == other.current_;
    }
    
    bool operator!=(const transform_iterator& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Sampling iterator that only yields every nth element
 */
template <typename BaseIterator>
class sampling_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename std::iterator_traits<BaseIterator>::value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type;
    
private:
    BaseIterator current_;
    BaseIterator end_;
    std::size_t step_;
    
public:
    sampling_iterator(BaseIterator current, BaseIterator end, std::size_t step)
        : current_(current), end_(end), step_(step) {}
    
    value_type operator*() const {
        return *current_;
    }
    
    sampling_iterator& operator++() {
        for (std::size_t i = 0; i < step_ && current_ != end_; ++i) {
            ++current_;
        }
        return *this;
    }
    
    sampling_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const sampling_iterator& other) const {
        return current_ == other.current_;
    }
    
    bool operator!=(const sampling_iterator& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Composite iterator that chains multiple ranges
 */
template <typename Iterator1, typename Iterator2>
class chain_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename std::iterator_traits<Iterator1>::value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type;
    
private:
    Iterator1 current1_;
    Iterator1 end1_;
    Iterator2 current2_;
    Iterator2 end2_;
    bool in_first_;
    
public:
    chain_iterator(Iterator1 curr1, Iterator1 end1, Iterator2 curr2, Iterator2 end2, bool in_first)
        : current1_(curr1), end1_(end1), current2_(curr2), end2_(end2), in_first_(in_first) {}
    
    reference operator*() const {
        return in_first_ ? *current1_ : *current2_;
    }
    
    chain_iterator& operator++() {
        if (in_first_) {
            ++current1_;
            if (current1_ == end1_) {
                in_first_ = false;
            }
        } else {
            ++current2_;
        }
        return *this;
    }
    
    chain_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const chain_iterator& other) const {
        return in_first_ == other.in_first_ &&
               current1_ == other.current1_ &&
               current2_ == other.current2_;
    }
    
    bool operator!=(const chain_iterator& other) const {
        return !(*this == other);
    }
};

// Helper functions for creating iterators

template <typename BaseIterator, typename Predicate>
auto make_filter_iterator(BaseIterator begin, BaseIterator end, Predicate pred) {
    return filter_iterator<BaseIterator, Predicate>(begin, end, pred);
}

template <typename BaseIterator, typename Transform>
auto make_transform_iterator(BaseIterator it, Transform transform) {
    return transform_iterator<BaseIterator, Transform>(it, transform);
}

template <typename BaseIterator>
auto make_sampling_iterator(BaseIterator begin, BaseIterator end, std::size_t step) {
    return sampling_iterator<BaseIterator>(begin, end, step);
}

template <typename Iterator1, typename Iterator2>
auto make_chain_iterator(Iterator1 begin1, Iterator1 end1, 
                         Iterator2 begin2, Iterator2 end2, bool in_first = true) {
    return chain_iterator<Iterator1, Iterator2>(begin1, end1, begin2, end2, in_first);
}

} // namespace approximate