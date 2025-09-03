#include <catch2/catch_test_macros.hpp>
#include <rd_ph_filter/builder.hpp>
#include "test_mock_ph.hpp"
#include <vector>
#include <array>

using namespace bernoulli;

TEST_CASE("rd_ph_filter_builder construction", "[builder]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Basic builder construction") {
        MockPerfectHashBuilder<int> ph_builder(0.0);
        auto filter_builder = make_filter_builder<PH>(ph_builder);
        
        std::vector<int> data = {1, 2, 3, 4, 5};
        auto filter = filter_builder.build(data.begin(), data.end());
        
        for (int x : data) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("Builder with configuration") {
        MockPerfectHashBuilder<int> ph_builder(0.0);
        auto filter_builder = make_filter_builder<PH>(ph_builder)
            .with_target_fpr(0.01)
            .with_target_fnr(0.001)
            .with_max_iterations(100)
            .with_space_overhead(2);
        
        std::vector<int> data = {10, 20, 30};
        auto filter = filter_builder.build(data.begin(), data.end());
        
        REQUIRE(filter(10) == true);
        REQUIRE(filter(20) == true);
        REQUIRE(filter(30) == true);
    }
}

TEST_CASE("rd_ph_filter_builder methods", "[builder]") {
    using PH = MockPerfectHash<int>;
    
    MockPerfectHashBuilder<int> ph_builder(0.0);
    auto filter_builder = make_filter_builder<PH>(ph_builder);
    
    SECTION("build_from container") {
        std::vector<int> vec = {1, 2, 3};
        auto filter1 = filter_builder.build_from(vec);
        
        std::array<int, 3> arr = {1, 2, 3};
        auto filter2 = filter_builder.build_from(arr);
        
        for (int x : vec) {
            REQUIRE(filter1(x) == true);
            REQUIRE(filter2(x) == true);
        }
    }
    
    SECTION("build_from initializer list") {
        auto filter = filter_builder.build_from({10, 20, 30, 40});
        
        REQUIRE(filter(10) == true);
        REQUIRE(filter(20) == true);
        REQUIRE(filter(30) == true);
        REQUIRE(filter(40) == true);
        REQUIRE(filter(50) == false);
    }
    
    SECTION("Builder reset") {
        auto configured = filter_builder
            .with_target_fpr(0.1)
            .with_max_iterations(50);
        
        configured.reset();
        
        // After reset, should work with default settings
        std::vector<int> data = {1, 2, 3};
        auto filter = configured.build(data.begin(), data.end());
        
        for (int x : data) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("Builder clone") {
        auto original = filter_builder
            .with_target_fpr(0.05)
            .with_target_fnr(0.01);
        
        auto cloned = original.clone();
        
        std::vector<int> data = {100, 200};
        auto filter1 = original.build(data.begin(), data.end());
        auto filter2 = cloned.build(data.begin(), data.end());
        
        // Both should produce same results
        REQUIRE(filter1(100) == filter2(100));
        REQUIRE(filter1(200) == filter2(200));
    }
}

TEST_CASE("rd_ph_filter_query operations", "[query]") {
    using PH = MockPerfectHash<int>;
    
    std::vector<int> data = {1, 3, 5, 7, 9};
    MockPerfectHashBuilder<int> ph_builder(0.0);
    auto filter_builder = make_filter_builder<PH>(ph_builder);
    auto filter = filter_builder.build(data.begin(), data.end());
    
    SECTION("Basic contains query") {
        auto q = query(filter);
        
        REQUIRE(q.contains(1) == true);
        REQUIRE(q.contains(2) == false);
        REQUIRE(q.contains(3) == true);
        REQUIRE(q.contains(4) == false);
        REQUIRE(q.contains(5) == true);
    }
    
    SECTION("contains_all query") {
        auto q = query(filter);
        
        std::vector<int> test_set = {1, 3, 5};
        auto results = q.contains_all(test_set);
        
        REQUIRE(results.size() == 3);
        REQUIRE(results[0] == true);
        REQUIRE(results[1] == true);
        REQUIRE(results[2] == true);
        
        std::vector<int> mixed_set = {1, 2, 3, 4};
        auto mixed_results = q.contains_all(mixed_set);
        
        REQUIRE(mixed_results[0] == true);
        REQUIRE(mixed_results[1] == false);
        REQUIRE(mixed_results[2] == true);
        REQUIRE(mixed_results[3] == false);
    }
    
    SECTION("contains_any query") {
        auto q = query(filter);
        
        std::vector<int> has_members = {2, 4, 5, 6};
        REQUIRE(q.contains_any(has_members) == true);
        
        std::vector<int> no_members = {2, 4, 6, 8};
        REQUIRE(q.contains_any(no_members) == false);
    }
    
    SECTION("count_members query") {
        auto q = query(filter);
        
        std::vector<int> test_set = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        size_t count = q.count_members(test_set);
        
        REQUIRE(count == 5);  // 1, 3, 5, 7, 9
    }
    
    SECTION("Error rate queries") {
        auto q = query(filter);
        
        double fpr = q.false_positive_rate();
        double fnr = q.false_negative_rate();
        double acc = q.accuracy();
        
        REQUIRE(fpr > 0.0);
        REQUIRE(fpr < 1.0);
        REQUIRE(fnr == 0.0);  // Perfect hash with no errors
        REQUIRE(acc > 0.0);
        REQUIRE(acc <= 1.0);
    }
    
    SECTION("Query with different filter") {
        auto q = query(filter);
        
        std::vector<int> other_data = {2, 4, 6};
        auto other_filter = filter_builder.build(other_data.begin(), other_data.end());
        
        auto q2 = q.with_filter(other_filter);
        
        REQUIRE(q2.contains(2) == true);
        REQUIRE(q2.contains(1) == false);
    }
}

TEST_CASE("rd_ph_filter_batch operations", "[batch]") {
    using PH = MockPerfectHash<int>;
    
    MockPerfectHashBuilder<int> ph_builder(0.0);
    auto filter_builder = make_filter_builder<PH>(ph_builder);
    
    SECTION("Batch test_all") {
        std::vector<int> set1 = {1, 2, 3};
        std::vector<int> set2 = {2, 3, 4};
        std::vector<int> set3 = {3, 4, 5};
        
        auto filter1 = filter_builder.build(set1.begin(), set1.end());
        auto filter2 = filter_builder.build(set2.begin(), set2.end());
        auto filter3 = filter_builder.build(set3.begin(), set3.end());
        
        rd_ph_filter_batch<PH> batch;
        batch.add(filter1).add(filter2).add(filter3);
        
        auto results = batch.test_all(3);
        REQUIRE(results.size() == 3);
        REQUIRE(results[0] == true);   // in set1
        REQUIRE(results[1] == true);   // in set2
        REQUIRE(results[2] == true);   // in set3
        
        auto results2 = batch.test_all(1);
        REQUIRE(results2[0] == true);  // in set1
        REQUIRE(results2[1] == false); // not in set2
        REQUIRE(results2[2] == false); // not in set3
    }
    
    SECTION("Batch test_any") {
        std::vector<int> set1 = {1, 2, 3};
        std::vector<int> set2 = {4, 5, 6};
        
        auto filter1 = filter_builder.build(set1.begin(), set1.end());
        auto filter2 = filter_builder.build(set2.begin(), set2.end());
        
        rd_ph_filter_batch<PH> batch;
        batch.add(filter1).add(filter2);
        
        REQUIRE(batch.test_any(1) == true);   // in filter1
        REQUIRE(batch.test_any(4) == true);   // in filter2
        REQUIRE(batch.test_any(7) == false);  // in neither
    }
    
    SECTION("Batch management") {
        rd_ph_filter_batch<PH> batch;
        
        REQUIRE(batch.size() == 0);
        
        std::vector<int> data = {1, 2};
        auto filter = filter_builder.build(data.begin(), data.end());
        
        batch.add(filter);
        REQUIRE(batch.size() == 1);
        
        batch.add(filter);
        REQUIRE(batch.size() == 2);
        
        batch.clear();
        REQUIRE(batch.size() == 0);
    }
}