#include <rd_ph_filter/approximate_map.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <random>

using namespace approximate;

// Mock perfect hash for examples
template <typename T>
class SimplePerfectHash {
public:
    struct H {
        using hash_type = std::size_t;
        std::size_t operator()(const T& x) const {
            return std::hash<T>{}(x);
        }
    };
    
    using iterator = typename std::vector<T>::const_iterator;
    
private:
    std::unordered_map<T, std::size_t> mapping_;
    std::size_t max_hash_;
    H hasher_;
    
public:
    SimplePerfectHash(iterator begin, iterator end) {
        std::size_t index = 0;
        for (auto it = begin; it != end; ++it) {
            mapping_[*it] = index++;
        }
        max_hash_ = (index > 0) ? index - 1 : 0;
    }
    
    std::size_t operator()(const T& x) const {
        auto it = mapping_.find(x);
        return (it != mapping_.end()) ? it->second : hasher_(x) % (max_hash_ + 1);
    }
    
    std::size_t max_hash() const { return max_hash_; }
    double error_rate() const { return 0.0; }
    H hash_fn() const { return hasher_; }
};

// Example 1: Classic set membership with different storage sizes
void example_set_membership() {
    std::cout << "\n=== Set Membership with Different Storage Sizes ===\n";
    
    std::vector<int> members = {1, 5, 10, 15, 20, 25, 30};
    
    auto ph_builder = [](auto begin, auto end) {
        return SimplePerfectHash<int>(begin, end);
    };
    
    // 8-bit storage (FPR = 1/256)
    {
        approximate_map<SimplePerfectHash<int>, uint8_t> filter8(
            members.begin(), members.end(), ph_builder
        );
        
        std::cout << "8-bit storage:\n";
        std::cout << "  Storage: " << filter8.storage_bytes() << " bytes\n";
        std::cout << "  Member test (5): " << filter8(5) << "\n";
        std::cout << "  Non-member test (7): " << filter8(7) << "\n";
        std::cout << "  FPR: ~" << (1.0/256) << "\n";
    }
    
    // 32-bit storage (FPR = 1/2^32)
    {
        approximate_map<SimplePerfectHash<int>, uint32_t> filter32(
            members.begin(), members.end(), ph_builder
        );
        
        std::cout << "\n32-bit storage:\n";
        std::cout << "  Storage: " << filter32.storage_bytes() << " bytes\n";
        std::cout << "  Member test (5): " << filter32(5) << "\n";
        std::cout << "  Non-member test (7): " << filter32(7) << "\n";
        std::cout << "  FPR: ~" << (1.0/4294967296.0) << "\n";
    }
}

// Example 2: Threshold-based membership with tunable FPR
void example_threshold_membership() {
    std::cout << "\n=== Threshold-based Membership ===\n";
    
    std::vector<std::string> allowlist = {"admin", "user1", "user2", "guest"};
    
    auto ph_builder = [](auto begin, auto end) {
        return SimplePerfectHash<std::string>(begin, end);
    };
    
    // Create filter with 10% false positive rate
    double target_fpr = 0.1;
    uint32_t threshold = static_cast<uint32_t>(target_fpr * std::numeric_limits<uint32_t>::max());
    
    ThresholdDecoder<uint32_t> decoder(threshold);
    
    auto encoder = [](const std::string& s) {
        // For simplicity, just hash the string
        return static_cast<uint32_t>(std::hash<std::string>{}(s));
    };
    
    approximate_map<SimplePerfectHash<std::string>, uint32_t, ThresholdDecoder<uint32_t>, bool> 
        filter(allowlist.begin(), allowlist.end(), ph_builder, encoder, decoder);
    
    std::cout << "Allowlist with " << (target_fpr * 100) << "% FPR:\n";
    std::cout << "  'admin' allowed: " << filter("admin") << "\n";
    std::cout << "  'hacker' allowed: " << filter("hacker") << "\n";
}

// Example 3: Compact function approximation
void example_function_approximation() {
    std::cout << "\n=== Function Approximation ===\n";
    
    // Approximate f(x) = x^2 for x in [0, 100]
    // But only store values for x in {0, 10, 20, ..., 100}
    std::vector<int> sample_points;
    for (int i = 0; i <= 100; i += 10) {
        sample_points.push_back(i);
    }
    
    auto ph_builder = [](auto begin, auto end) {
        return SimplePerfectHash<int>(begin, end);
    };
    
    // Encoder: store sqrt(f(x)) to save space (since f(x) = x^2)
    auto encoder = [](int x) -> uint16_t {
        return static_cast<uint16_t>(x); // We know x^2, so store x
    };
    
    // Decoder: reconstruct x^2 from stored x
    struct SquareDecoder {
        int operator()(uint16_t stored, int) const {
            return stored * stored;
        }
    };
    
    approximate_map<SimplePerfectHash<int>, uint16_t, SquareDecoder, int>
        approx_square(sample_points.begin(), sample_points.end(), 
                     ph_builder, encoder, SquareDecoder{});
    
    std::cout << "Approximating f(x) = x^2:\n";
    std::cout << "  f(10) = " << approx_square(10) << " (exact: 100)\n";
    std::cout << "  f(20) = " << approx_square(20) << " (exact: 400)\n";
    std::cout << "  f(15) = " << approx_square(15) << " (not stored, returns hash collision)\n";
}

// Example 4: Compact color palette mapping
void example_color_mapping() {
    std::cout << "\n=== Color Palette Mapping ===\n";
    
    struct Color {
        uint8_t r, g, b;
        bool operator==(const Color& other) const {
            return r == other.r && g == other.g && b == other.b;
        }
    };
    
    // Define a palette of web-safe colors
    std::vector<Color> palette = {
        {255, 0, 0},    // Red
        {0, 255, 0},    // Green  
        {0, 0, 255},    // Blue
        {255, 255, 0},  // Yellow
        {255, 0, 255},  // Magenta
        {0, 255, 255},  // Cyan
        {0, 0, 0},      // Black
        {255, 255, 255} // White
    };
    
    // Custom hash for Color
    struct ColorHash {
        std::size_t operator()(const Color& c) const {
            return (c.r << 16) | (c.g << 8) | c.b;
        }
    };
    
    // Mock PH for colors - move outside function
    struct ColorPerfectHash {
    public:
        struct H {
            using hash_type = std::size_t;
            std::size_t operator()(const Color& c) const {
                return ColorHash{}(c);
            }
        };
        
        using iterator = std::vector<Color>::const_iterator;
        
    private:
        std::unordered_map<std::size_t, std::size_t> mapping_;
        std::size_t max_hash_;
        H hasher_;
        
    public:
        ColorPerfectHash(iterator begin, iterator end) {
            std::size_t index = 0;
            for (auto it = begin; it != end; ++it) {
                mapping_[ColorHash{}(*it)] = index++;
            }
            max_hash_ = (index > 0) ? index - 1 : 0;
        }
        
        std::size_t operator()(const Color& c) const {
            auto hash = ColorHash{}(c);
            auto it = mapping_.find(hash);
            return (it != mapping_.end()) ? it->second : hash % (max_hash_ + 1);
        }
        
        std::size_t max_hash() const { return max_hash_; }
        double error_rate() const { return 0.0; }
        H hash_fn() const { return hasher_; }
    };
    
    auto ph_builder = [](auto begin, auto end) {
        return ColorPerfectHash(begin, end);
    };
    
    // Store palette index (3 bits would suffice for 8 colors)
    auto encoder = [&palette](const Color& c) -> uint8_t {
        auto it = std::find(palette.begin(), palette.end(), c);
        return static_cast<uint8_t>(std::distance(palette.begin(), it));
    };
    
    // Decode back to color
    struct PaletteDecoder {
        const std::vector<Color>* palette;
        
        Color operator()(uint8_t index, const Color&) const {
            if (index < palette->size()) {
                return (*palette)[index];
            }
            return {128, 128, 128}; // Default gray for unknown
        }
    };
    
    PaletteDecoder decoder{&palette};
    
    approximate_map<ColorPerfectHash, uint8_t, PaletteDecoder, Color>
        color_map(palette.begin(), palette.end(), ph_builder, encoder, decoder);
    
    std::cout << "Color palette mapping (8 colors in 1 byte each):\n";
    Color red = {255, 0, 0};
    Color result = color_map(red);
    std::cout << "  Red lookup: RGB(" << (int)result.r << ", " 
              << (int)result.g << ", " << (int)result.b << ")\n";
    
    Color unknown = {128, 64, 192};
    result = color_map(unknown);
    std::cout << "  Unknown color: RGB(" << (int)result.r << ", "
              << (int)result.g << ", " << (int)result.b << ")\n";
}

// Example 5: Sparse matrix row storage
void example_sparse_matrix() {
    std::cout << "\n=== Sparse Matrix Row Storage ===\n";
    
    // Store only non-zero elements of a sparse matrix row
    struct MatrixEntry {
        int col;
        double value;
        bool operator==(const MatrixEntry& other) const {
            return col == other.col;
        }
    };
    
    // Non-zero entries in row 0 of a sparse matrix
    std::vector<MatrixEntry> row_entries = {
        {1, 3.14},
        {5, 2.71},
        {10, 1.41},
        {100, 0.577}
    };
    
    // For simplicity, we'll just work with column indices
    std::vector<int> cols;
    for (const auto& entry : row_entries) {
        cols.push_back(entry.col);
    }
    
    auto ph_builder = [](auto begin, auto end) {
        return SimplePerfectHash<int>(begin, end);
    };
    
    // Store quantized values (16-bit fixed point)
    auto encoder = [&row_entries](int col) -> uint16_t {
        auto it = std::find_if(row_entries.begin(), row_entries.end(),
                               [col](const MatrixEntry& e) { return e.col == col; });
        if (it != row_entries.end()) {
            return static_cast<uint16_t>(it->value * 1000); // Fixed point
        }
        return 0;
    };
    
    struct ValueDecoder {
        double operator()(uint16_t stored, int) const {
            return stored / 1000.0;
        }
    };
    
    approximate_map<SimplePerfectHash<int>, uint16_t, ValueDecoder, double>
        sparse_row(cols.begin(), cols.end(), ph_builder, encoder, ValueDecoder{});
    
    std::cout << "Sparse matrix row (4 non-zero elements):\n";
    std::cout << "  Storage: " << sparse_row.storage_bytes() << " bytes\n";
    std::cout << "  M[0,1] = " << sparse_row(1) << " (stored: 3.14)\n";
    std::cout << "  M[0,5] = " << sparse_row(5) << " (stored: 2.71)\n";
    std::cout << "  M[0,50] = " << sparse_row(50) << " (not stored, collision)\n";
}

int main() {
    example_set_membership();
    example_threshold_membership();
    example_function_approximation();
    example_color_mapping();
    example_sparse_matrix();
    
    return 0;
}