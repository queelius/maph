#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rd_ph_filter/approximate_map.hpp>
#include "test_mock_ph.hpp"
#include <vector>
#include <set>
#include <random>
#include <cmath>

using namespace approximate;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

TEST_CASE("approximate_map with SetMembershipDecoder", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Basic set membership with uint32_t storage") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        approximate_map<PH, uint32_t> filter(elements.begin(), elements.end(), builder);
        
        // All elements should be members
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
        
        // Non-members should mostly be false
        REQUIRE(filter(10) == false);
        REQUIRE(filter(100) == false);
    }
    
    SECTION("Set membership with uint8_t storage") {
        std::vector<int> elements = {10, 20, 30};
        MockPerfectHashBuilder<int> builder(0.0);
        
        approximate_map<PH, uint8_t> filter(elements.begin(), elements.end(), builder);
        
        // Storage should be based on max_hash + 1
        REQUIRE(filter.storage_bytes() == 3 * sizeof(uint8_t)); // max_hash = 2, so 3 elements
        
        // Members should still be detected
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("False positive rate with different storage sizes") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        // Test with uint8_t (FPR ~ 1/256)
        {
            approximate_map<PH, uint8_t> filter8(elements.begin(), elements.end(), builder);
            
            int false_positives = 0;
            int tests = 1000;
            std::mt19937 gen(42);
            std::uniform_int_distribution<> dist(100, 1100);
            
            for (int i = 0; i < tests; ++i) {
                if (filter8(dist(gen))) {
                    false_positives++;
                }
            }
            
            double observed_fpr = static_cast<double>(false_positives) / tests;
            REQUIRE(observed_fpr < 0.01); // Should be around 1/256 ≈ 0.004
        }
        
        // Test with uint16_t (FPR ~ 1/65536)
        {
            approximate_map<PH, uint16_t> filter16(elements.begin(), elements.end(), builder);
            
            int false_positives = 0;
            int tests = 10000;
            std::mt19937 gen(42);
            std::uniform_int_distribution<> dist(100, 10100);
            
            for (int i = 0; i < tests; ++i) {
                if (filter16(dist(gen))) {
                    false_positives++;
                }
            }
            
            double observed_fpr = static_cast<double>(false_positives) / tests;
            REQUIRE(observed_fpr < 0.001); // Should be around 1/65536 ≈ 0.000015
        }
    }
}

TEST_CASE("approximate_map with ThresholdDecoder", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Threshold decoder with 50% probability") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        uint32_t threshold = std::numeric_limits<uint32_t>::max() / 2;
        ThresholdDecoder<uint32_t> decoder(threshold);
        
        auto encoder = [](int x) -> uint32_t {
            // Elements get low values (below threshold)
            return static_cast<uint32_t>(x * 1000);
        };
        
        approximate_map<PH, uint32_t, ThresholdDecoder<uint32_t>, bool> 
            filter(elements.begin(), elements.end(), builder, encoder, decoder);
        
        // All encoded elements should pass threshold
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
        
        REQUIRE_THAT(decoder.false_positive_rate(), WithinAbs(0.5, 0.01));
    }
    
    SECTION("Threshold decoder with custom probability") {
        std::vector<int> elements = {10, 20, 30};
        MockPerfectHashBuilder<int> builder(0.0);
        
        double target_fpr = 0.1;
        uint32_t threshold = static_cast<uint32_t>(target_fpr * std::numeric_limits<uint32_t>::max());
        ThresholdDecoder<uint32_t> decoder(threshold);
        
        auto encoder = [threshold](int x) -> uint32_t {
            // Ensure elements are below threshold
            return threshold / 2;
        };
        
        approximate_map<PH, uint32_t, ThresholdDecoder<uint32_t>, bool>
            filter(elements.begin(), elements.end(), builder, encoder, decoder);
        
        // Members should be detected
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
        
        REQUIRE_THAT(decoder.false_positive_rate(), WithinAbs(target_fpr, 0.01));
    }
}

TEST_CASE("approximate_map with IdentityDecoder", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Identity decoder returns stored values") {
        std::vector<int> keys = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        IdentityDecoder<uint16_t> decoder;
        
        // Encode values as their squares
        auto encoder = [](int x) -> uint16_t {
            return static_cast<uint16_t>(x * x);
        };
        
        approximate_map<PH, uint16_t, IdentityDecoder<uint16_t>, uint16_t>
            square_map(keys.begin(), keys.end(), builder, encoder, decoder);
        
        // Should return encoded values
        REQUIRE(square_map(1) == 1);
        REQUIRE(square_map(2) == 4);
        REQUIRE(square_map(3) == 9);
        REQUIRE(square_map(4) == 16);
        REQUIRE(square_map(5) == 25);
    }
    
    SECTION("Identity decoder for compact lookup table") {
        struct Entry {
            int id;
            int value;
        };
        
        std::vector<Entry> entries = {
            {100, 42},
            {200, 84},
            {300, 126}
        };
        
        // Extract IDs for perfect hash
        std::vector<int> ids;
        for (const auto& e : entries) {
            ids.push_back(e.id);
        }
        
        MockPerfectHashBuilder<int> builder(0.0);
        IdentityDecoder<uint8_t> decoder;
        
        auto encoder = [&entries, &ids](int id) -> uint8_t {
            auto it = std::find_if(entries.begin(), entries.end(),
                                  [id](const Entry& e) { return e.id == id; });
            if (it != entries.end()) {
                return static_cast<uint8_t>(it->value);
            }
            return 0;
        };
        
        approximate_map<PH, uint8_t, IdentityDecoder<uint8_t>, uint8_t>
            lookup(ids.begin(), ids.end(), builder, encoder, decoder);
        
        REQUIRE(lookup(100) == 42);
        REQUIRE(lookup(200) == 84);
        REQUIRE(lookup(300) == 126);
    }
}

TEST_CASE("approximate_map with load factor", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Load factor affects storage size") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        auto encoder = [](int x) -> uint32_t {
            return static_cast<uint32_t>(x);
        };
        
        IdentityDecoder<uint32_t> decoder;
        
        // Load factor 1.0 (default)
        {
            approximate_map<PH, uint32_t, IdentityDecoder<uint32_t>, uint32_t>
                map1(elements.begin(), elements.end(), builder, encoder, decoder, 1.0);
            
            REQUIRE(map1.load_factor() == 1.0);
            REQUIRE(map1.storage_bytes() == 5 * sizeof(uint32_t));
        }
        
        // Load factor 2.0 (double the space)
        {
            approximate_map<PH, uint32_t, IdentityDecoder<uint32_t>, uint32_t>
                map2(elements.begin(), elements.end(), builder, encoder, decoder, 2.0);
            
            REQUIRE(map2.load_factor() == 2.0);
            REQUIRE(map2.storage_bytes() == 10 * sizeof(uint32_t));
        }
    }
    
    SECTION("Load factor affects collision behavior") {
        std::vector<int> elements = {1, 2, 3};
        MockPerfectHashBuilder<int> builder(0.0);
        
        // With high load factor, fewer collisions
        approximate_map<PH, uint32_t, SetMembershipDecoder<uint32_t, PH::H>, bool>
            sparse_map(elements.begin(), elements.end(), builder);
        
        REQUIRE(sparse_map.load_factor() == 1.0);
        
        // Verify members are still detected correctly
        for (int x : elements) {
            REQUIRE(sparse_map(x) == true);
        }
    }
}

TEST_CASE("approximate_map custom decoders", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Custom decoder for logarithmic values") {
        struct LogDecoder {
            double operator()(uint16_t stored, int) const {
                // Decode from log space
                return std::exp(stored / 1000.0);
            }
        };
        
        std::vector<int> keys = {1, 2, 3, 4, 5};
        MockPerfectHashBuilder<int> builder(0.0);
        
        auto encoder = [](int x) -> uint16_t {
            // Encode in log space
            double value = x * 10.0;
            return static_cast<uint16_t>(std::log(value) * 1000);
        };
        
        approximate_map<PH, uint16_t, LogDecoder, double>
            log_map(keys.begin(), keys.end(), builder, encoder, LogDecoder{});
        
        // Values should be approximately recovered
        REQUIRE_THAT(log_map(1), WithinRel(10.0, 0.01));
        REQUIRE_THAT(log_map(2), WithinRel(20.0, 0.01));
        REQUIRE_THAT(log_map(3), WithinRel(30.0, 0.01));
    }
    
    SECTION("Custom decoder with context") {
        struct ScalingDecoder {
            double scale;
            explicit ScalingDecoder(double s = 1.0) : scale(s) {}
            
            double operator()(uint8_t stored, int) const {
                return stored * scale;
            }
        };
        
        std::vector<int> keys = {10, 20, 30};
        MockPerfectHashBuilder<int> builder(0.0);
        
        auto encoder = [](int x) -> uint8_t {
            return static_cast<uint8_t>(x / 10); // Scale down
        };
        
        ScalingDecoder decoder(10.0); // Scale back up
        
        approximate_map<PH, uint8_t, ScalingDecoder, double>
            scaled_map(keys.begin(), keys.end(), builder, encoder, decoder);
        
        REQUIRE(scaled_map(10) == 10.0);
        REQUIRE(scaled_map(20) == 20.0);
        REQUIRE(scaled_map(30) == 30.0);
    }
}

TEST_CASE("approximate_map error rates", "[approximate_map]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("False negative rate from perfect hash") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        
        // Perfect hash with 10% error rate
        MockPerfectHashBuilder<int> builder(0.1);
        
        approximate_map<PH, uint32_t> filter(elements.begin(), elements.end(), builder);
        
        REQUIRE(filter.fnr() == 0.1);
    }
    
    SECTION("Combined error rates") {
        std::vector<int> elements = {1, 2, 3, 4, 5};
        
        // Perfect hash with 5% error rate
        MockPerfectHashBuilder<int> builder(0.05);
        
        // Use uint8_t for higher FPR
        approximate_map<PH, uint8_t> filter(elements.begin(), elements.end(), builder);
        
        double fnr = filter.fnr();
        REQUIRE(fnr == 0.05);
        
        // FPR should be approximately 1/256
        SetMembershipDecoder<uint8_t, PH::H> decoder;
        double fpr = decoder.false_positive_rate();
        REQUIRE_THAT(fpr, WithinAbs(1.0/256, 0.001));
    }
}

TEST_CASE("approximate_map_builder", "[builder]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Builder with load factor") {
        MockPerfectHashBuilder<int> ph_builder(0.0);
        auto map_builder = approximate_map_builder<PH>(ph_builder)
            .with_load_factor(2.0);
        
        std::vector<int> elements = {1, 2, 3};
        auto filter = map_builder.build_set_filter_32bit(elements.begin(), elements.end());
        
        // Verify filter works
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
    }
    
    SECTION("Builder with storage bits selection") {
        MockPerfectHashBuilder<int> ph_builder(0.0);
        auto map_builder = approximate_map_builder<PH>(ph_builder);
        
        std::vector<int> elements = {10, 20, 30};
        auto filter8 = map_builder.build_set_filter_8bit(elements.begin(), elements.end());
        auto filter32 = map_builder.build_set_filter_32bit(elements.begin(), elements.end());
        
        // Test that they work correctly
        for (int x : elements) {
            REQUIRE(filter8(x) == true);
            REQUIRE(filter32(x) == true);
        }
    }
    
    SECTION("Build threshold filter") {
        MockPerfectHashBuilder<int> ph_builder(0.0);
        auto map_builder = approximate_map_builder<PH>(ph_builder);
        
        std::vector<int> elements = {1, 2, 3, 4, 5};
        double target_fpr = 0.2;
        
        auto filter = map_builder.build_threshold_filter<decltype(elements.begin()), uint32_t>(
            elements.begin(), elements.end(), target_fpr
        );
        
        // All members should be detected
        for (int x : elements) {
            REQUIRE(filter(x) == true);
        }
    }
}

TEST_CASE("approximate_map stress tests", "[approximate_map][stress]") {
    using PH = MockPerfectHash<int>;
    
    SECTION("Large dataset with uint8_t storage") {
        std::vector<int> large_set;
        for (int i = 0; i < 1000; ++i) {
            large_set.push_back(i * 7); // Sparse values
        }
        
        MockPerfectHashBuilder<int> builder(0.01);
        approximate_map<PH, uint8_t> filter(large_set.begin(), large_set.end(), builder);
        
        // Test random members
        std::mt19937 gen(42);
        std::uniform_int_distribution<> member_dist(0, 999);
        
        int correct = 0;
        for (int i = 0; i < 100; ++i) {
            int val = member_dist(gen) * 7;
            if (filter(val)) {
                correct++;
            }
        }
        
        // Should detect most members (accounting for FNR)
        REQUIRE(correct > 95);
        
        // Test false positive rate
        int false_positives = 0;
        for (int i = 0; i < 1000; ++i) {
            int val = i * 7 + 1; // Values not in set
            if (filter(val)) {
                false_positives++;
            }
        }
        
        // FPR should be around 1/256
        double observed_fpr = static_cast<double>(false_positives) / 1000;
        REQUIRE(observed_fpr < 0.01);
    }
    
    SECTION("Different storage types comparison") {
        std::vector<int> elements;
        for (int i = 0; i < 100; ++i) {
            elements.push_back(i);
        }
        
        MockPerfectHashBuilder<int> builder(0.0);
        
        // Compare storage sizes
        approximate_map<PH, uint8_t> filter8(elements.begin(), elements.end(), builder);
        approximate_map<PH, uint16_t> filter16(elements.begin(), elements.end(), builder);
        approximate_map<PH, uint32_t> filter32(elements.begin(), elements.end(), builder);
        approximate_map<PH, uint64_t> filter64(elements.begin(), elements.end(), builder);
        
        REQUIRE(filter8.storage_bytes() < filter16.storage_bytes());
        REQUIRE(filter16.storage_bytes() < filter32.storage_bytes());
        REQUIRE(filter32.storage_bytes() < filter64.storage_bytes());
        
        // All should correctly identify members
        for (int i = 0; i < 10; ++i) {
            REQUIRE(filter8(i) == true);
            REQUIRE(filter16(i) == true);
            REQUIRE(filter32(i) == true);
            REQUIRE(filter64(i) == true);
        }
    }
}