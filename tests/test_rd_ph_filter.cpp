#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rd_ph_filter/rd_ph_filter.hpp>
#include "test_mock_ph.hpp"
#include <vector>
#include <set>
#include <random>

using namespace bernoulli;
using Catch::Matchers::WithinRel;

TEST_CASE("rd_ph_filter construction", "[rd_ph_filter]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    SECTION("Construction from vector") {
        std::vector<int> data = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        Filter filter(data.begin(), data.end(), builder);
        
        // Test that all elements are recognized
        for (int x : data) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("Construction from set") {
        std::set<int> data = {10, 20, 30, 40, 50};
        MockPerfectHashBuilder<int> builder(0.0);
        Filter filter(data.begin(), data.end(), builder);
        
        for (int x : data) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("Empty construction") {
        std::vector<int> empty;
        MockPerfectHashBuilder<int> builder(0.0);
        Filter filter(empty.begin(), empty.end(), builder);
        
        // Should handle queries on empty filter
        REQUIRE(filter(42) == false);
    }
}

TEST_CASE("rd_ph_filter membership testing", "[rd_ph_filter]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    std::vector<int> members = {1, 5, 10, 15, 20, 25, 30};
    std::vector<int> non_members = {2, 3, 4, 11, 12, 13, 31, 32};
    
    MockPerfectHashBuilder<int> builder(0.0);
    Filter filter(members.begin(), members.end(), builder);
    
    SECTION("True positives") {
        for (int x : members) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("True negatives (mostly)") {
        // Some may be false positives due to hash collisions
        int false_positives = 0;
        for (int x : non_members) {
            if (filter(x)) {
                false_positives++;
            }
        }
        // False positive rate should be very low
        double observed_fpr = static_cast<double>(false_positives) / non_members.size();
        REQUIRE(observed_fpr < 0.1);
    }
}

TEST_CASE("rd_ph_filter error rates", "[rd_ph_filter]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    std::vector<int> data = {1, 2, 3, 4, 5};
    
    SECTION("False positive rate calculation") {
        MockPerfectHashBuilder<int> builder(0.0);
        Filter filter(data.begin(), data.end(), builder);
        
        double fpr = filter.fpr();
        // FPR should be approximately 1/max(hash_type)
        REQUIRE(fpr > 0.0);
        REQUIRE(fpr < 1.0);
    }
    
    SECTION("False negative rate with perfect hash") {
        MockPerfectHashBuilder<int> builder(0.0);  // No errors
        Filter filter(data.begin(), data.end(), builder);
        
        double fnr = filter.fnr();
        REQUIRE(fnr == 0.0);
    }
    
    SECTION("False negative rate with imperfect hash") {
        MockPerfectHashBuilder<int> builder(0.1);  // 10% error rate
        Filter filter(data.begin(), data.end(), builder);
        
        double fnr = filter.fnr();
        REQUIRE(fnr > 0.0);
        REQUIRE(fnr <= 0.1);  // Should be less than error rate
    }
}

TEST_CASE("rd_ph_filter equality operators", "[rd_ph_filter]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    std::vector<int> data1 = {1, 2, 3};
    std::vector<int> data2 = {1, 2, 3};
    std::vector<int> data3 = {4, 5, 6};
    
    MockPerfectHashBuilder<int> builder(0.0);
    
    SECTION("Equality") {
        Filter filter1(data1.begin(), data1.end(), builder);
        Filter filter2(data2.begin(), data2.end(), builder);
        Filter filter3(data3.begin(), data3.end(), builder);
        
        REQUIRE(filter1 == filter1);  // Self equality
        REQUIRE(filter1 == filter2);  // Same data
        REQUIRE(!(filter1 == filter3));  // Different data
    }
    
    SECTION("Inequality") {
        Filter filter1(data1.begin(), data1.end(), builder);
        Filter filter3(data3.begin(), data3.end(), builder);
        
        REQUIRE(filter1 != filter3);
        REQUIRE(!(filter1 != filter1));
    }
    
    SECTION("Ordering operators") {
        Filter filter1(data1.begin(), data1.end(), builder);
        Filter filter2(data2.begin(), data2.end(), builder);
        
        // These are defined to be false for different filters
        REQUIRE(!(filter1 < filter2));
        REQUIRE(!(filter1 > filter2));
        
        // <= and >= are true only for equal filters
        REQUIRE(filter1 <= filter1);
        REQUIRE(filter1 >= filter1);
    }
}

TEST_CASE("rd_ph_filter free functions", "[rd_ph_filter]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    std::vector<int> data = {10, 20, 30};
    MockPerfectHashBuilder<int> builder(0.05);
    Filter filter(data.begin(), data.end(), builder);
    
    SECTION("fpr free function") {
        double rate = fpr(filter);
        REQUIRE(rate == filter.fpr());
    }
    
    SECTION("fnr free function") {
        double rate = fnr(filter);
        REQUIRE(rate == filter.fnr());
    }
    
    SECTION("is_member free function") {
        for (int x : data) {
            REQUIRE(is_member(x, filter) == filter(x));
        }
    }
}

TEST_CASE("rd_ph_filter stress test", "[rd_ph_filter][stress]") {
    using PH = MockPerfectHash<int>;
    using Filter = rd_ph_filter<PH>;
    
    SECTION("Large dataset") {
        std::vector<int> large_data;
        for (int i = 0; i < 10000; ++i) {
            large_data.push_back(i);
        }
        
        MockPerfectHashBuilder<int> builder(0.01);
        Filter filter(large_data.begin(), large_data.end(), builder);
        
        // Test sample of members
        std::mt19937 gen(42);
        std::uniform_int_distribution<> dist(0, 9999);
        
        int correct = 0;
        int tests = 1000;
        for (int i = 0; i < tests; ++i) {
            int val = dist(gen);
            if (filter(val)) {
                correct++;
            }
        }
        
        // Should recognize most members (accounting for FNR)
        double accuracy = static_cast<double>(correct) / tests;
        REQUIRE(accuracy > 0.95);
    }
    
    SECTION("Duplicate elements") {
        std::vector<int> data_with_dups = {1, 2, 2, 3, 3, 3, 4, 4, 4, 4};
        MockPerfectHashBuilder<int> builder(0.0);
        Filter filter(data_with_dups.begin(), data_with_dups.end(), builder);
        
        // Should handle duplicates correctly (treated as set)
        REQUIRE(filter(1) == true);
        REQUIRE(filter(2) == true);
        REQUIRE(filter(3) == true);
        REQUIRE(filter(4) == true);
        REQUIRE(filter(5) == false);
    }
}