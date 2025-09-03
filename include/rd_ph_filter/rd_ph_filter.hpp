#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

/**
 * @file rd_ph_filter.hpp
 * @brief Rate-distorted perfect hash filter implementation
 * @author RD PH Filter Contributors
 * @version 1.0.0
 * 
 * This file contains the main implementation of the rate-distorted perfect
 * hash filter, which models the concept of a Bernoulli set with controllable
 * false positive and false negative rates.
 */

namespace bernoulli {
/**
 * @brief Rate-distorted perfect hash filter models the concept of a
 * (immutable) Bernoulli set.
 *
 * As a type of bernoulli set, it has a fase positive rate and a false
 * negative rate. In this case, the false positive rate is an expectation
 * that may be specified precisely and the false negative rate is a function
 * of a specified time and space complexity.
 *
 * @tparam PH a type that models the concept of a rate-distrted perfect hash
 * function, Hashable -> [0,m], where m is the maximum hash value.
 */
template <typename PH>
struct rd_ph_filter {
    using hash_type = typename PH::H::hash_type;

    auto perfect_hash_fn() const { return ph; }
    auto hash_fn() const { return ph.hash_fn(); }

    template <typename I>
    static auto build_filter(PH const& ph, I begin, I end)
    {
        std::vector<hash_type> hashes(ph.max_hash() + 1);
        for (auto x = begin; x != end; ++x) hashes[ph(*x)] = ph.hash_fn()(*x);
        return hashes;
    }

    /**
     * @brief Construct object from iterator range [begin,end),
     * which is viewed as representing a set (duplicates and
     * order does not matter).
     *
     * It takes an object `builder` that models the concept of a
     * Builder. It is a functor that accepts an iterator range [begin,end)
     * and constructs a PH object for it with a pre-specified set of
     * parameters.
     *
     * @param begin start of elements to build a filter for
     * @param end end of elements
     * @tparam I models the concept of a forward iterator
     * @tparam Builder models the concept of a builder for PH
     */
    template <typename I, typename Builder>
    rd_ph_filter(I begin, I end, Builder builder)
        : ph(builder(begin, end))
        , hashes(build_filter(ph, begin, end))
    {
    }

    /**
     * @brief Test element x for membership in the set.
     * 
     * @tparam X Type of the element to test
     * @param x Element to test for membership
     * @return true if x is likely a member, false otherwise
     * @note May return false positives with rate fpr()
     * @note May return false negatives with rate fnr()
     */
    template <typename X>
    auto operator()(X const& x) const
    {
        return hashes[ph(x)] == ph.hash_fn()(x);
    }

    /**
     * @brief Get the false positive rate of the filter
     * 
     * The false positive rate is determined by the hash function's
     * output space. It represents the probability that a non-member
     * element will be incorrectly identified as a member.
     * 
     * @return False positive rate as a probability [0,1]
     */
    static auto fpr()
    {
        return 1.0 / std::numeric_limits<hash_type>::max();
    }

    /**
     * @brief Get the false negative rate of the filter
     * 
     * The false negative rate depends on the perfect hash function's
     * error rate. It represents the probability that a member element
     * will be incorrectly identified as a non-member.
     * 
     * @return False negative rate as a probability [0,1]
     */
    auto fnr() const
    {
        return ph.error_rate() * (1 - fpr());
    }

    PH const ph;
    std::vector<hash_type> const hashes;
};

/**
 * @brief Free function to get false positive rate
 * @tparam PH Perfect hash function type
 * @param filter Filter instance
 * @return False positive rate
 */
template <typename PH>
auto fpr(rd_ph_filter<PH> const&)
{
    return rd_ph_filter<PH>::fpr();
}

/**
 * @brief Free function to get false negative rate
 * @tparam PH Perfect hash function type
 * @param s Filter instance
 * @return False negative rate
 */
template <typename PH>
auto fnr(rd_ph_filter<PH> const& s)
{
    return s.fnr();
}

/**
 * @brief Free function to test membership
 * @tparam PH Perfect hash function type
 * @param x Element to test
 * @param s Filter instance
 * @return true if x is likely a member, false otherwise
 */
template <typename PH>
auto is_member(auto const& x, rd_ph_filter<PH> const& s)
{
    return s(x);
}

/**
 * @brief the equality predicate.
 *
 * Representational equality implies equality.
 * if Hashable(PH::H) is finite, then
 * different representations could be equal.
 * However, we make the simplifying assumption
 * that this is not the case.
 *
 * @tparam PH perfect hash function type
 * @param lhs left-hand-side of equality
 * @param rhs right-hand-side of equality
 */
template <typename PH>
auto operator==(rd_ph_filter<PH> const& lhs, rd_ph_filter<PH> const& rhs)
{
    return (lhs.ph == rhs.ph) && (lhs.hashes == rhs.hashes);
}

template <typename PH>
auto operator!=(rd_ph_filter<PH> const& lhs, rd_ph_filter<PH> const& rhs)
{
    return !(lhs == rhs);
}

template <typename PH>
auto operator<=(rd_ph_filter<PH> const& lhs, rd_ph_filter<PH> const& rhs)
{
    return lhs == rhs;
}

template <typename PH>
auto operator<(rd_ph_filter<PH> const&, rd_ph_filter<PH> const&)
{
    return false;
}

template <typename PH>
auto operator>=(rd_ph_filter<PH> const& lhs, rd_ph_filter<PH> const& rhs)
{
    return lhs == rhs;
}

template <typename PH>
auto operator>(rd_ph_filter<PH> const&, rd_ph_filter<PH> const&)
{
    return false;
}
}
