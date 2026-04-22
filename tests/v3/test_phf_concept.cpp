/**
 * @file test_phf_concept.cpp
 * @brief Tests for perfect_hash_function and phf_builder concepts
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/concepts/perfect_hash_function.hpp>
#include <optional>

using namespace maph;

namespace {

struct mock_phf {
    slot_index slot_for(std::string_view) const noexcept { return slot_index{0}; }
    size_t num_keys() const noexcept { return 0; }
    size_t range_size() const noexcept { return 0; }
    double bits_per_key() const noexcept { return 0.0; }
    size_t memory_bytes() const noexcept { return 0; }
    std::vector<std::byte> serialize() const { return {}; }
    static result<mock_phf> deserialize(std::span<const std::byte>) {
        return mock_phf{};
    }
};

struct mock_builder {
    mock_builder& add(std::string_view) { return *this; }
    mock_builder& add_all(const std::vector<std::string>&) { return *this; }
    result<mock_phf> build() { return mock_phf{}; }
};

} // namespace

static_assert(perfect_hash_function<mock_phf>, "mock_phf must satisfy perfect_hash_function");
static_assert(phf_builder<mock_builder, mock_phf>, "mock_builder must satisfy phf_builder");

TEST_CASE("perfect_hash_function concept: mock satisfies", "[phf_concept]") {
    mock_phf phf;
    REQUIRE(phf.slot_for("test").value == 0);
    REQUIRE(phf.num_keys() == 0);
    REQUIRE(phf.range_size() == 0);
    REQUIRE(phf.bits_per_key() == 0.0);
    REQUIRE(phf.memory_bytes() == 0);
    REQUIRE(phf.serialize().empty());
}

TEST_CASE("phf_builder concept: mock satisfies", "[phf_concept]") {
    mock_builder b;
    auto& ref = b.add("key");
    REQUIRE(&ref == &b);

    std::vector<std::string> keys = {"a", "b"};
    auto& ref2 = b.add_all(keys);
    REQUIRE(&ref2 == &b);

    auto result = b.build();
    REQUIRE(result.has_value());
}

// Negative concept checks: types missing required methods should NOT satisfy the concept.

namespace {

struct missing_slot_for {
    size_t num_keys() const noexcept { return 0; }
    size_t range_size() const noexcept { return 0; }
    double bits_per_key() const noexcept { return 0.0; }
    size_t memory_bytes() const noexcept { return 0; }
    std::vector<std::byte> serialize() const { return {}; }
};

struct missing_num_keys {
    slot_index slot_for(std::string_view) const noexcept { return slot_index{0}; }
    size_t range_size() const noexcept { return 0; }
    double bits_per_key() const noexcept { return 0.0; }
    size_t memory_bytes() const noexcept { return 0; }
    std::vector<std::byte> serialize() const { return {}; }
};

struct missing_serialize {
    slot_index slot_for(std::string_view) const noexcept { return slot_index{0}; }
    size_t num_keys() const noexcept { return 0; }
    size_t range_size() const noexcept { return 0; }
    double bits_per_key() const noexcept { return 0.0; }
    size_t memory_bytes() const noexcept { return 0; }
};

struct missing_build {
    missing_build& add(std::string_view) { return *this; }
    missing_build& add_all(const std::vector<std::string>&) { return *this; }
};

struct wrong_add_return {
    void add(std::string_view) {}
    wrong_add_return& add_all(const std::vector<std::string>&) { return *this; }
    result<mock_phf> build() { return mock_phf{}; }
};

} // namespace

static_assert(!perfect_hash_function<missing_slot_for>,
    "missing slot_for must NOT satisfy perfect_hash_function");
static_assert(!perfect_hash_function<missing_num_keys>,
    "missing num_keys must NOT satisfy perfect_hash_function");
static_assert(!perfect_hash_function<missing_serialize>,
    "missing serialize must NOT satisfy perfect_hash_function");
static_assert(!phf_builder<missing_build, mock_phf>,
    "missing build must NOT satisfy phf_builder");
static_assert(!phf_builder<wrong_add_return, mock_phf>,
    "wrong add return type must NOT satisfy phf_builder");

TEST_CASE("perfect_hash_function concept: negative checks compile", "[phf_concept]") {
    // These are compile-time checks via static_assert above.
    // This test exists to confirm the negative static_asserts are reachable.
    REQUIRE(!perfect_hash_function<missing_slot_for>);
    REQUIRE(!perfect_hash_function<missing_num_keys>);
    REQUIRE(!perfect_hash_function<missing_serialize>);
    REQUIRE(!phf_builder<missing_build, mock_phf>);
    REQUIRE(!phf_builder<wrong_add_return, mock_phf>);
}
