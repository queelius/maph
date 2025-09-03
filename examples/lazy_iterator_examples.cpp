#include <rd_ph_filter/approximate_map.hpp>
#include <rd_ph_filter/lazy_iterators.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <random>

using namespace approximate;

// Simple perfect hash for examples
template <typename T>
class SimplePH {
public:
    struct H {
        using hash_type = std::size_t;
        std::size_t operator()(const T& x) const { return std::hash<T>{}(x); }
    };
    using iterator = typename std::vector<T>::const_iterator;
    
private:
    std::vector<T> elements_;
    
public:
    template <typename It>
    SimplePH(It begin, It end) : elements_(begin, end) {}
    
    std::size_t operator()(const T& x) const {
        auto it = std::find(elements_.begin(), elements_.end(), x);
        if (it != elements_.end()) {
            return std::distance(elements_.begin(), it);
        }
        return H{}(x) % elements_.size();
    }
    
    std::size_t max_hash() const { return elements_.size() - 1; }
    double error_rate() const { return 0.0; }
    H hash_fn() const { return H{}; }
};

// Example 1: Lazy generation of prime numbers
void example_lazy_primes() {
    std::cout << "\n=== Lazy Prime Number Generation ===\n";
    
    // Generator that computes nth prime
    auto prime_generator = [](std::size_t n) -> int {
        if (n == 0) return 2;
        if (n == 1) return 3;
        
        int count = 2;
        int candidate = 5;
        while (count <= n) {
            bool is_prime = true;
            for (int i = 2; i * i <= candidate; ++i) {
                if (candidate % i == 0) {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) {
                if (count == n) return candidate;
                count++;
            }
            candidate += 2;
        }
        return candidate;
    };
    
    // Create lazy range of first 100 primes
    auto prime_range = make_lazy_range<int>(prime_generator, 100);
    
    // Build approximate set of primes without storing them all
    auto ph_builder = [](auto begin, auto end) {
        std::vector<int> temp(begin, end);
        return SimplePH<int>(temp.begin(), temp.end());
    };
    
    approximate_map<SimplePH<int>, uint16_t> prime_filter(
        prime_range.begin(), prime_range.end(), ph_builder
    );
    
    std::cout << "Created filter for first 100 primes (lazily generated)\n";
    std::cout << "Storage: " << prime_filter.storage_bytes() << " bytes\n";
    std::cout << "Is 17 prime? " << prime_filter(17) << "\n";
    std::cout << "Is 18 prime? " << prime_filter(18) << "\n";
}

// Example 2: Filtering and transforming on-the-fly
void example_filter_transform() {
    std::cout << "\n=== Filter and Transform Pipeline ===\n";
    
    // Start with a range of numbers
    std::vector<int> numbers;
    for (int i = 1; i <= 1000; ++i) {
        numbers.push_back(i);
    }
    
    // Filter: only numbers divisible by 3 or 5
    auto is_fizzbuzz = [](int n) { return n % 3 == 0 || n % 5 == 0; };
    auto filtered_begin = make_filter_iterator(numbers.begin(), numbers.end(), is_fizzbuzz);
    auto filtered_end = make_filter_iterator(numbers.end(), numbers.end(), is_fizzbuzz);
    
    // Transform: square the filtered numbers
    auto square = [](int n) { return n * n; };
    auto transform_begin = make_transform_iterator(filtered_begin, square);
    auto transform_end = make_transform_iterator(filtered_end, square);
    
    // Build filter from transformed values
    auto ph_builder = [](auto begin, auto end) {
        std::vector<int> temp(begin, end);
        return SimplePH<int>(temp.begin(), temp.end());
    };
    
    approximate_map<SimplePH<int>, uint8_t> squared_fizzbuzz(
        transform_begin, transform_end, ph_builder
    );
    
    std::cout << "Filter for squared FizzBuzz numbers (3 or 5 divisible, then squared)\n";
    std::cout << "Storage: " << squared_fizzbuzz.storage_bytes() << " bytes\n";
    std::cout << "Is 9 (3²) in set? " << squared_fizzbuzz(9) << "\n";
    std::cout << "Is 25 (5²) in set? " << squared_fizzbuzz(25) << "\n";
    std::cout << "Is 16 (4²) in set? " << squared_fizzbuzz(16) << "\n";
}

// Example 3: Sampling from large dataset
void example_sampling() {
    std::cout << "\n=== Sampling from Large Dataset ===\n";
    
    // Simulate large dataset with generator
    auto data_generator = [](std::size_t i) -> double {
        return std::sin(i * 0.1) * std::cos(i * 0.05) * 1000;
    };
    
    auto full_range = make_lazy_range<double>(data_generator, 10000);
    
    // Sample every 10th element - collect into vector first
    std::vector<double> full_data(full_range.begin(), full_range.end());
    std::vector<double> sampled_data;
    for (size_t i = 0; i < full_data.size(); i += 10) {
        sampled_data.push_back(full_data[i]);
    }
    
    // Build compact representation of sampled data
    auto ph_builder = [](auto begin, auto end) {
        std::vector<double> temp(begin, end);
        return SimplePH<double>(temp.begin(), temp.end());
    };
    
    // Quantize to 16-bit integers
    auto encoder = [](double d) -> uint16_t {
        return static_cast<uint16_t>(std::abs(d));
    };
    
    struct Decoder {
        double operator()(uint16_t val, double) const {
            return static_cast<double>(val);
        }
    };
    
    approximate_map<SimplePH<double>, uint16_t, Decoder, double> 
        sampled_map(sampled_data.begin(), sampled_data.end(), ph_builder, encoder, Decoder{});
    
    std::cout << "Sampled dataset (every 10th element from 10,000)\n";
    std::cout << "Storage: " << sampled_map.storage_bytes() << " bytes\n";
    std::cout << "Original size would be: " << 10000 * sizeof(double) << " bytes\n";
}

// Example 4: Composite ranges with chaining
void example_composite_ranges() {
    std::cout << "\n=== Composite Ranges ===\n";
    
    // Two different sources of allowed IDs
    std::vector<int> admin_ids = {1001, 1002, 1003};
    std::vector<int> user_ids = {2001, 2002, 2003, 2004, 2005};
    
    // Chain them together
    auto chain_begin = make_chain_iterator(
        admin_ids.begin(), admin_ids.end(),
        user_ids.begin(), user_ids.end(), true
    );
    auto chain_end = make_chain_iterator(
        admin_ids.end(), admin_ids.end(),
        user_ids.end(), user_ids.end(), false
    );
    
    auto ph_builder = [](auto begin, auto end) {
        std::vector<int> temp(begin, end);
        return SimplePH<int>(temp.begin(), temp.end());
    };
    
    approximate_map<SimplePH<int>, uint32_t> allowed_ids(
        chain_begin, chain_end, ph_builder
    );
    
    std::cout << "Allowed IDs (admin + user ranges chained)\n";
    std::cout << "Is 1002 (admin) allowed? " << allowed_ids(1002) << "\n";
    std::cout << "Is 2003 (user) allowed? " << allowed_ids(2003) << "\n";
    std::cout << "Is 3001 (neither) allowed? " << allowed_ids(3001) << "\n";
}

// Example 5: Mathematical function sampling
void example_function_sampling() {
    std::cout << "\n=== Mathematical Function Sampling ===\n";
    
    // Define a complex mathematical function
    struct Point2D {
        double x, y;
        bool operator==(const Point2D& other) const {
            return x == other.x && y == other.y;
        }
    };
    
    // Generate points on a parametric curve
    auto curve_generator = [](std::size_t t) -> Point2D {
        double theta = t * 0.1;
        return {
            std::cos(theta) * (1 + 0.5 * std::cos(3 * theta)),
            std::sin(theta) * (1 + 0.5 * std::cos(3 * theta))
        };
    };
    
    auto curve_range = make_lazy_range<Point2D>(curve_generator, 628); // ~2π * 100
    
    // Filter points in first quadrant
    auto first_quadrant = [](const Point2D& p) { return p.x >= 0 && p.y >= 0; };
    auto filtered_begin = make_filter_iterator(curve_range.begin(), curve_range.end(), first_quadrant);
    auto filtered_end = make_filter_iterator(curve_range.end(), curve_range.end(), first_quadrant);
    
    // Transform to polar coordinates  
    struct PolarPoint {
        double r, theta;
        bool operator==(const PolarPoint& other) const {
            return std::abs(r - other.r) < 0.001 && std::abs(theta - other.theta) < 0.001;
        }
    };
    
    auto to_polar = [](const Point2D& p) -> PolarPoint {
        return {
            std::sqrt(p.x * p.x + p.y * p.y),
            std::atan2(p.y, p.x)
        };
    };
    
    auto polar_begin = make_transform_iterator(filtered_begin, to_polar);
    auto polar_end = make_transform_iterator(filtered_end, to_polar);
    
    // Build compact representation
    // Move this outside the function
    struct PolarPointPH {
    public:
        struct H {
            using hash_type = std::size_t;
            std::size_t operator()(const PolarPoint& p) const {
                return std::hash<double>{}(p.r) ^ std::hash<double>{}(p.theta);
            }
        };
        using iterator = typename std::vector<PolarPoint>::const_iterator;
        
    private:
        std::vector<PolarPoint> elements_;
        
    public:
        template <typename It>
        PolarPointPH(It begin, It end) : elements_(begin, end) {}
        
        std::size_t operator()(const PolarPoint& p) const {
            auto it = std::find(elements_.begin(), elements_.end(), p);
            if (it != elements_.end()) {
                return std::distance(elements_.begin(), it);
            }
            return H{}(p) % elements_.size();
        }
        
        std::size_t max_hash() const { return elements_.empty() ? 0 : elements_.size() - 1; }
        double error_rate() const { return 0.0; }
        H hash_fn() const { return H{}; }
    };
    
    auto ph_builder = [](auto begin, auto end) {
        std::vector<PolarPoint> temp(begin, end);
        return PolarPointPH(temp.begin(), temp.end());
    };
    
    // Store quantized radius (0-255 range)
    auto encoder = [](const PolarPoint& p) -> uint8_t {
        return static_cast<uint8_t>(std::min(255.0, p.r * 100));
    };
    
    struct RadiusDecoder {
        double operator()(uint8_t val, const PolarPoint&) const {
            return val / 100.0;
        }
    };
    
    approximate_map<PolarPointPH, uint8_t, RadiusDecoder, double>
        curve_filter(polar_begin, polar_end, ph_builder, encoder, RadiusDecoder{});
    
    std::cout << "Parametric curve (first quadrant, polar coordinates)\n";
    std::cout << "Storage: " << curve_filter.storage_bytes() << " bytes\n";
    std::cout << "Samples stored from infinite curve generation\n";
}

int main() {
    example_lazy_primes();
    example_filter_transform();
    example_sampling();
    example_composite_ranges();
    example_function_sampling();
    
    return 0;
}