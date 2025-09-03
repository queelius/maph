#include <catch2/catch_test_macros.hpp>
#include <rd_ph_filter/lazy_iterators.hpp>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace approximate;

TEST_CASE("lazy_generator_iterator", "[lazy_iterators]") {
    SECTION("Generate sequence of squares") {
        auto square_gen = [](std::size_t n) { return n * n; };
        auto range = make_lazy_range<int>(square_gen, 5);
        
        std::vector<int> result(range.begin(), range.end());
        std::vector<int> expected = {0, 1, 4, 9, 16};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Generate Fibonacci sequence") {
        auto fib_gen = [](std::size_t n) -> int {
            if (n <= 1) return n;
            int a = 0, b = 1;
            for (std::size_t i = 2; i <= n; ++i) {
                int temp = a + b;
                a = b;
                b = temp;
            }
            return b;
        };
        
        auto range = make_lazy_range<int>(fib_gen, 8);
        std::vector<int> result(range.begin(), range.end());
        std::vector<int> expected = {0, 1, 1, 2, 3, 5, 8, 13};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Lazy evaluation - values computed on demand") {
        int compute_count = 0;
        auto counting_gen = [&compute_count](std::size_t n) {
            compute_count++;
            return n * 2;
        };
        
        auto range = make_lazy_range<int>(counting_gen, 10);
        
        // No computation yet
        REQUIRE(compute_count == 0);
        
        // Access first element
        auto it = range.begin();
        int first = *it;
        REQUIRE(first == 0);
        REQUIRE(compute_count == 1);
        
        // Access same element again - may recompute
        int first_again = *it;
        REQUIRE(first_again == 0);
        // Count may be 1 or 2 depending on caching
        
        // Move to next
        ++it;
        int second = *it;
        REQUIRE(second == 2);
        REQUIRE(compute_count >= 2);
    }
}

TEST_CASE("filter_iterator", "[lazy_iterators]") {
    SECTION("Filter even numbers") {
        std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        auto is_even = [](int n) { return n % 2 == 0; };
        
        auto filtered_begin = make_filter_iterator(numbers.begin(), numbers.end(), is_even);
        auto filtered_end = make_filter_iterator(numbers.end(), numbers.end(), is_even);
        
        std::vector<int> result(filtered_begin, filtered_end);
        std::vector<int> expected = {2, 4, 6, 8, 10};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Filter with no matches") {
        std::vector<int> numbers = {1, 3, 5, 7, 9};
        auto is_even = [](int n) { return n % 2 == 0; };
        
        auto filtered_begin = make_filter_iterator(numbers.begin(), numbers.end(), is_even);
        auto filtered_end = make_filter_iterator(numbers.end(), numbers.end(), is_even);
        
        std::vector<int> result(filtered_begin, filtered_end);
        REQUIRE(result.empty());
    }
    
    SECTION("Filter strings by length") {
        std::vector<std::string> words = {"a", "bb", "ccc", "dd", "eeeee"};
        auto has_length_2 = [](const std::string& s) { return s.length() == 2; };
        
        auto filtered_begin = make_filter_iterator(words.begin(), words.end(), has_length_2);
        auto filtered_end = make_filter_iterator(words.end(), words.end(), has_length_2);
        
        std::vector<std::string> result(filtered_begin, filtered_end);
        std::vector<std::string> expected = {"bb", "dd"};
        
        REQUIRE(result == expected);
    }
}

TEST_CASE("transform_iterator", "[lazy_iterators]") {
    SECTION("Transform integers to their squares") {
        std::vector<int> numbers = {1, 2, 3, 4, 5};
        auto square = [](int n) { return n * n; };
        
        auto trans_begin = make_transform_iterator(numbers.begin(), square);
        auto trans_end = make_transform_iterator(numbers.end(), square);
        
        std::vector<int> result(trans_begin, trans_end);
        std::vector<int> expected = {1, 4, 9, 16, 25};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Transform strings to lengths") {
        std::vector<std::string> words = {"hello", "world", "test"};
        auto get_length = [](const std::string& s) { return s.length(); };
        
        auto trans_begin = make_transform_iterator(words.begin(), get_length);
        auto trans_end = make_transform_iterator(words.end(), get_length);
        
        std::vector<std::size_t> result(trans_begin, trans_end);
        std::vector<std::size_t> expected = {5, 5, 4};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Chain transforms") {
        std::vector<int> numbers = {1, 2, 3};
        auto double_it = [](int n) { return n * 2; };
        auto square_it = [](int n) { return n * n; };
        
        // First double, then square
        auto doubled_begin = make_transform_iterator(numbers.begin(), double_it);
        auto doubled_end = make_transform_iterator(numbers.end(), double_it);
        
        auto squared_begin = make_transform_iterator(doubled_begin, square_it);
        auto squared_end = make_transform_iterator(doubled_end, square_it);
        
        std::vector<int> result(squared_begin, squared_end);
        std::vector<int> expected = {4, 16, 36}; // (1*2)^2, (2*2)^2, (3*2)^2
        
        REQUIRE(result == expected);
    }
}

TEST_CASE("sampling_iterator", "[lazy_iterators]") {
    SECTION("Sample every 2nd element") {
        std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        
        auto sample_begin = make_sampling_iterator(numbers.begin(), numbers.end(), 2);
        auto sample_end = make_sampling_iterator(numbers.end(), numbers.end(), 2);
        
        std::vector<int> result(sample_begin, sample_end);
        std::vector<int> expected = {1, 3, 5, 7, 9};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Sample every 3rd element") {
        std::vector<int> numbers = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        
        auto sample_begin = make_sampling_iterator(numbers.begin(), numbers.end(), 3);
        auto sample_end = make_sampling_iterator(numbers.end(), numbers.end(), 3);
        
        std::vector<int> result(sample_begin, sample_end);
        std::vector<int> expected = {0, 3, 6};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Sample with step larger than container") {
        std::vector<int> numbers = {1, 2, 3};
        
        auto sample_begin = make_sampling_iterator(numbers.begin(), numbers.end(), 5);
        auto sample_end = make_sampling_iterator(numbers.end(), numbers.end(), 5);
        
        std::vector<int> result(sample_begin, sample_end);
        std::vector<int> expected = {1}; // Only first element
        
        REQUIRE(result == expected);
    }
}

TEST_CASE("chain_iterator", "[lazy_iterators]") {
    SECTION("Chain two vectors") {
        std::vector<int> first = {1, 2, 3};
        std::vector<int> second = {4, 5, 6};
        
        auto chain_begin = make_chain_iterator(
            first.begin(), first.end(),
            second.begin(), second.end(), true
        );
        auto chain_end = make_chain_iterator(
            first.end(), first.end(),
            second.end(), second.end(), false
        );
        
        std::vector<int> result(chain_begin, chain_end);
        std::vector<int> expected = {1, 2, 3, 4, 5, 6};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Chain with empty first range") {
        std::vector<int> first;
        std::vector<int> second = {1, 2, 3};
        
        auto chain_begin = make_chain_iterator(
            first.begin(), first.end(),
            second.begin(), second.end(), 
            first.begin() != first.end()  // false, start with second
        );
        auto chain_end = make_chain_iterator(
            first.end(), first.end(),
            second.end(), second.end(), false
        );
        
        std::vector<int> result(chain_begin, chain_end);
        std::vector<int> expected = {1, 2, 3};
        
        REQUIRE(result == expected);
    }
    
    SECTION("Chain with empty second range") {
        std::vector<int> first = {1, 2, 3};
        std::vector<int> second;
        
        auto chain_begin = make_chain_iterator(
            first.begin(), first.end(),
            second.begin(), second.end(), true
        );
        auto chain_end = make_chain_iterator(
            first.end(), first.end(),
            second.end(), second.end(), false
        );
        
        std::vector<int> result(chain_begin, chain_end);
        std::vector<int> expected = {1, 2, 3};
        
        REQUIRE(result == expected);
    }
}

TEST_CASE("Composed iterators", "[lazy_iterators]") {
    SECTION("Filter then transform") {
        std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        
        // Filter even numbers
        auto is_even = [](int n) { return n % 2 == 0; };
        auto filtered_begin = make_filter_iterator(numbers.begin(), numbers.end(), is_even);
        auto filtered_end = make_filter_iterator(numbers.end(), numbers.end(), is_even);
        
        // Square the filtered numbers
        auto square = [](int n) { return n * n; };
        auto squared_begin = make_transform_iterator(filtered_begin, square);
        auto squared_end = make_transform_iterator(filtered_end, square);
        
        std::vector<int> result(squared_begin, squared_end);
        std::vector<int> expected = {4, 16, 36, 64, 100}; // 2^2, 4^2, 6^2, 8^2, 10^2
        
        REQUIRE(result == expected);
    }
    
    SECTION("Generate, filter, sample") {
        // Generate numbers 0-19
        auto gen = [](std::size_t n) { return static_cast<int>(n); };
        auto range = make_lazy_range<int>(gen, 20);
        
        // Filter multiples of 3
        auto is_multiple_3 = [](int n) { return n % 3 == 0; };
        auto filtered_begin = make_filter_iterator(range.begin(), range.end(), is_multiple_3);
        auto filtered_end = make_filter_iterator(range.end(), range.end(), is_multiple_3);
        
        // Sample every other one
        auto sampled_begin = make_sampling_iterator(filtered_begin, filtered_end, 2);
        auto sampled_end = make_sampling_iterator(filtered_end, filtered_end, 2);
        
        std::vector<int> result(sampled_begin, sampled_end);
        std::vector<int> expected = {0, 6, 12, 18}; // Every other multiple of 3
        
        REQUIRE(result == expected);
    }
    
    SECTION("Complex pipeline") {
        // Start with numbers
        std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        
        // Transform to double
        auto to_double = [](int n) { return n * 2; };
        auto doubled_begin = make_transform_iterator(numbers.begin(), to_double);
        auto doubled_end = make_transform_iterator(numbers.end(), to_double);
        
        // Filter > 10
        auto greater_10 = [](int n) { return n > 10; };
        auto filtered_begin = make_filter_iterator(doubled_begin, doubled_end, greater_10);
        auto filtered_end = make_filter_iterator(doubled_end, doubled_end, greater_10);
        
        // Sample every 2nd
        auto sampled_begin = make_sampling_iterator(filtered_begin, filtered_end, 2);
        auto sampled_end = make_sampling_iterator(filtered_end, filtered_end, 2);
        
        std::vector<int> result(sampled_begin, sampled_end);
        std::vector<int> expected = {12, 16, 20}; // 6*2, 8*2, 10*2 (every 2nd of filtered)
        
        REQUIRE(result == expected);
    }
}

TEST_CASE("Iterator edge cases", "[lazy_iterators]") {
    SECTION("Empty ranges") {
        std::vector<int> empty;
        
        // Filter on empty
        auto is_even = [](int n) { return n % 2 == 0; };
        auto filtered_begin = make_filter_iterator(empty.begin(), empty.end(), is_even);
        auto filtered_end = make_filter_iterator(empty.end(), empty.end(), is_even);
        std::vector<int> filter_result(filtered_begin, filtered_end);
        REQUIRE(filter_result.empty());
        
        // Transform on empty
        auto square = [](int n) { return n * n; };
        auto trans_begin = make_transform_iterator(empty.begin(), square);
        auto trans_end = make_transform_iterator(empty.end(), square);
        std::vector<int> trans_result(trans_begin, trans_end);
        REQUIRE(trans_result.empty());
        
        // Sample on empty
        auto sample_begin = make_sampling_iterator(empty.begin(), empty.end(), 2);
        auto sample_end = make_sampling_iterator(empty.end(), empty.end(), 2);
        std::vector<int> sample_result(sample_begin, sample_end);
        REQUIRE(sample_result.empty());
    }
    
    SECTION("Single element") {
        std::vector<int> single = {42};
        
        // Filter that matches
        auto is_even = [](int n) { return n % 2 == 0; };
        auto filtered_begin = make_filter_iterator(single.begin(), single.end(), is_even);
        auto filtered_end = make_filter_iterator(single.end(), single.end(), is_even);
        std::vector<int> filter_result(filtered_begin, filtered_end);
        REQUIRE(filter_result == std::vector<int>{42});
        
        // Transform
        auto negate = [](int n) { return -n; };
        auto trans_begin = make_transform_iterator(single.begin(), negate);
        auto trans_end = make_transform_iterator(single.end(), negate);
        std::vector<int> trans_result(trans_begin, trans_end);
        REQUIRE(trans_result == std::vector<int>{-42});
    }
    
    SECTION("Iterator comparison") {
        std::vector<int> numbers = {1, 2, 3};
        
        // Use the same lambda instance for comparison
        auto identity = [](int n) { return n; };
        auto it1 = make_transform_iterator(numbers.begin(), identity);
        auto it2 = make_transform_iterator(numbers.begin(), identity);
        auto it3 = make_transform_iterator(numbers.end(), identity);
        
        REQUIRE(it1 == it2);
        REQUIRE(it1 != it3);
        
        ++it1;
        REQUIRE(it1 != it2);
    }
}