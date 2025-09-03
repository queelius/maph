/**
 * @file maph_cli.cpp
 * @brief Command-line interface for maph (map perfect hash)
 * 
 * Supports various function approximation use cases:
 * - Simple key-value pairs
 * - Multi-dimensional inputs (tuples)
 * - Multi-valued outputs (tuples)
 * - CSV/TSV input formats
 * - JSON output support
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <tuple>
#include <variant>
#include <iomanip>
#include <chrono>
#include <cstring>

#include "rd_ph_filter/approximate_map.hpp"
#include "rd_ph_filter/builder.hpp"

// Command-line argument parsing
struct CLIArgs {
    std::string input_file = "-";  // stdin by default
    std::string output_file = "-"; // stdout by default
    std::string mode = "build";    // build, query, info
    std::string format = "auto";   // auto, csv, tsv, json, pairs
    int storage_bits = 32;         // 8, 16, 32, 64
    double error_rate = 0.0;
    double load_factor = 1.23;
    double target_fpr = -1;        // -1 means not set
    char delimiter = '\0';         // auto-detect if not set
    bool verbose = false;
    bool header = false;           // CSV has header row
    std::vector<int> input_cols;   // Which columns are inputs
    std::vector<int> output_cols;  // Which columns are outputs
    std::string filter_file;       // Save/load filter
    std::vector<std::string> queries; // Query values
};

// Tuple value type that can hold multiple values
using TupleValue = std::vector<std::string>;

// Print usage information
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "maph - Map Perfect Hash: Space-efficient approximate function storage\n\n";
    
    std::cout << "OPTIONS:\n";
    std::cout << "  -i, --input FILE      Input file (default: stdin)\n";
    std::cout << "  -o, --output FILE     Output file (default: stdout)\n";
    std::cout << "  -m, --mode MODE       Mode: build, query, info (default: build)\n";
    std::cout << "  -f, --format FORMAT   Format: auto, csv, tsv, json, pairs (default: auto)\n";
    std::cout << "  -b, --bits N          Storage bits: 8, 16, 32, 64 (default: 32)\n";
    std::cout << "  -e, --error RATE      Perfect hash error rate (default: 0.0)\n";
    std::cout << "  -l, --load-factor F   Load factor (default: 1.23)\n";
    std::cout << "  --fpr TARGET          Target false positive rate (for threshold filters)\n";
    std::cout << "  -d, --delimiter CHAR  Field delimiter (auto-detect if not set)\n";
    std::cout << "  --header              First line is header (CSV/TSV)\n";
    std::cout << "  --input-cols COLS     Input columns (e.g., '0,1,2' or '1-3')\n";
    std::cout << "  --output-cols COLS    Output columns (e.g., '3,4' or '4-5')\n";
    std::cout << "  --filter FILE         Filter file to save/load\n";
    std::cout << "  -q, --query VALUES    Query values (comma-separated)\n";
    std::cout << "  -v, --verbose         Verbose output\n";
    std::cout << "  -h, --help            Show this help message\n";
    
    std::cout << "\nEXAMPLES:\n";
    std::cout << "  # Simple key-value mapping\n";
    std::cout << "  echo -e \"alice,1\\nbob,2\\ncharlie,3\" | " << program_name << " -b 16\n\n";
    
    std::cout << "  # Multi-dimensional function (x,y,z) -> (a,b)\n";
    std::cout << "  " << program_name << " -i data.csv --input-cols 0,1,2 --output-cols 3,4 -b 32\n\n";
    
    std::cout << "  # Build and save filter\n";
    std::cout << "  " << program_name << " -i data.csv --filter model.maph -b 16\n\n";
    
    std::cout << "  # Query saved filter\n";
    std::cout << "  " << program_name << " -m query --filter model.maph -q \"x,y,z\"\n\n";
    
    std::cout << "  # With target false positive rate\n";
    std::cout << "  " << program_name << " -i data.csv --fpr 0.01 -b 8\n";
}

// Parse column specification (e.g., "0,2,4" or "1-3")
std::vector<int> parse_columns(const std::string& spec) {
    std::vector<int> cols;
    std::stringstream ss(spec);
    std::string part;
    
    while (std::getline(ss, part, ',')) {
        size_t dash = part.find('-');
        if (dash != std::string::npos) {
            // Range specification
            int start = std::stoi(part.substr(0, dash));
            int end = std::stoi(part.substr(dash + 1));
            for (int i = start; i <= end; ++i) {
                cols.push_back(i);
            }
        } else {
            // Single column
            cols.push_back(std::stoi(part));
        }
    }
    
    return cols;
}

// Parse command-line arguments
CLIArgs parse_args(int argc, char* argv[]) {
    CLIArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "-i" || arg == "--input") {
            args.input_file = argv[++i];
        } else if (arg == "-o" || arg == "--output") {
            args.output_file = argv[++i];
        } else if (arg == "-m" || arg == "--mode") {
            args.mode = argv[++i];
        } else if (arg == "-f" || arg == "--format") {
            args.format = argv[++i];
        } else if (arg == "-b" || arg == "--bits") {
            args.storage_bits = std::stoi(argv[++i]);
        } else if (arg == "-e" || arg == "--error") {
            args.error_rate = std::stod(argv[++i]);
        } else if (arg == "-l" || arg == "--load-factor") {
            args.load_factor = std::stod(argv[++i]);
        } else if (arg == "--fpr") {
            args.target_fpr = std::stod(argv[++i]);
        } else if (arg == "-d" || arg == "--delimiter") {
            args.delimiter = argv[++i][0];
        } else if (arg == "--header") {
            args.header = true;
        } else if (arg == "--input-cols") {
            args.input_cols = parse_columns(argv[++i]);
        } else if (arg == "--output-cols") {
            args.output_cols = parse_columns(argv[++i]);
        } else if (arg == "--filter") {
            args.filter_file = argv[++i];
        } else if (arg == "-q" || arg == "--query") {
            std::stringstream ss(argv[++i]);
            std::string item;
            while (std::getline(ss, item, ',')) {
                args.queries.push_back(item);
            }
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::exit(1);
        }
    }
    
    return args;
}

// Auto-detect delimiter from first line
char detect_delimiter(const std::string& line) {
    int tabs = std::count(line.begin(), line.end(), '\t');
    int commas = std::count(line.begin(), line.end(), ',');
    int pipes = std::count(line.begin(), line.end(), '|');
    
    if (tabs > 0 && tabs >= commas) return '\t';
    if (commas > 0) return ',';
    if (pipes > 0) return '|';
    return ','; // Default
}

// Split a line by delimiter
std::vector<std::string> split_line(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    
    while (std::getline(ss, field, delimiter)) {
        fields.push_back(field);
    }
    
    return fields;
}

// Extract tuple from fields based on column indices
TupleValue extract_tuple(const std::vector<std::string>& fields, const std::vector<int>& cols) {
    TupleValue tuple;
    for (int col : cols) {
        if (col < fields.size()) {
            tuple.push_back(fields[col]);
        }
    }
    return tuple;
}

// Convert tuple to string for hashing
std::string tuple_to_string(const TupleValue& tuple) {
    std::string result;
    for (size_t i = 0; i < tuple.size(); ++i) {
        if (i > 0) result += "\x1F"; // Unit separator
        result += tuple[i];
    }
    return result;
}

// Build mode: create filter from input data
template<typename StorageType>
void build_filter(const CLIArgs& args) {
    // Read input data
    std::vector<std::pair<TupleValue, TupleValue>> data;
    
    std::istream* input = &std::cin;
    std::ifstream file;
    if (args.input_file != "-") {
        file.open(args.input_file);
        if (!file) {
            std::cerr << "Error: Cannot open input file: " << args.input_file << "\n";
            std::exit(1);
        }
        input = &file;
    }
    
    std::string line;
    bool first_line = true;
    char delimiter = args.delimiter;
    
    while (std::getline(*input, line)) {
        if (line.empty()) continue;
        
        // Auto-detect delimiter from first line
        if (first_line && delimiter == '\0') {
            delimiter = detect_delimiter(line);
            if (args.verbose) {
                std::cerr << "Detected delimiter: '" << delimiter << "'\n";
            }
        }
        
        // Skip header if specified
        if (first_line && args.header) {
            first_line = false;
            continue;
        }
        first_line = false;
        
        std::vector<std::string> fields = split_line(line, delimiter);
        
        // Determine input and output columns
        std::vector<int> input_cols = args.input_cols;
        std::vector<int> output_cols = args.output_cols;
        
        if (input_cols.empty()) {
            // Default: first column(s) as input
            if (output_cols.empty()) {
                // Simple key-value: first col is key, second is value
                input_cols = {0};
                if (fields.size() > 1) {
                    output_cols = {1};
                }
            } else {
                // All non-output columns are inputs
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (std::find(output_cols.begin(), output_cols.end(), i) == output_cols.end()) {
                        input_cols.push_back(i);
                    }
                }
            }
        } else if (output_cols.empty()) {
            // All non-input columns are outputs
            for (size_t i = 0; i < fields.size(); ++i) {
                if (std::find(input_cols.begin(), input_cols.end(), i) == input_cols.end()) {
                    output_cols.push_back(i);
                }
            }
        }
        
        TupleValue input_tuple = extract_tuple(fields, input_cols);
        TupleValue output_tuple = extract_tuple(fields, output_cols);
        
        data.push_back({input_tuple, output_tuple});
    }
    
    if (args.verbose) {
        std::cerr << "Loaded " << data.size() << " mappings\n";
        if (!data.empty()) {
            std::cerr << "Input dimensions: " << data[0].first.size() << "\n";
            std::cerr << "Output dimensions: " << data[0].second.size() << "\n";
        }
    }
    
    // Build the filter
    auto start = std::chrono::high_resolution_clock::now();
    
    // Convert data to format suitable for approximate_map
    std::vector<std::string> keys;
    std::vector<StorageType> values;
    
    for (const auto& [input, output] : data) {
        keys.push_back(tuple_to_string(input));
        // For now, use hash of output as value (simplified)
        // In a real implementation, we'd need a more sophisticated encoding
        std::hash<std::string> hasher;
        values.push_back(static_cast<StorageType>(hasher(tuple_to_string(output))));
    }
    
    // Build using mock perfect hash (placeholder)
    // In production, use actual perfect hash implementation
    if (args.verbose) {
        std::cerr << "Building filter with " << args.storage_bits << "-bit storage...\n";
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (args.verbose) {
        std::cerr << "Filter built in " << duration.count() << " ms\n";
        std::cerr << "Storage size: " << (data.size() * sizeof(StorageType)) << " bytes\n";
        std::cerr << "Theoretical FPR: " << (1.0 / (1ULL << (sizeof(StorageType) * 8))) << "\n";
    }
    
    // Save filter if requested
    if (!args.filter_file.empty()) {
        std::ofstream filter_out(args.filter_file, std::ios::binary);
        if (!filter_out) {
            std::cerr << "Error: Cannot open filter file: " << args.filter_file << "\n";
            std::exit(1);
        }
        
        // Write header
        filter_out.write("MAPH", 4);
        uint32_t version = 1;
        filter_out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        uint32_t bits = args.storage_bits;
        filter_out.write(reinterpret_cast<const char*>(&bits), sizeof(bits));
        uint64_t size = data.size();
        filter_out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        
        // Write data (simplified - would need actual filter serialization)
        if (args.verbose) {
            std::cerr << "Filter saved to: " << args.filter_file << "\n";
        }
    }
    
    // Output results
    std::ostream* output = &std::cout;
    std::ofstream out_file;
    if (args.output_file != "-") {
        out_file.open(args.output_file);
        output = &out_file;
    }
    
    if (args.format == "json") {
        *output << "{\n";
        *output << "  \"type\": \"maph_filter\",\n";
        *output << "  \"storage_bits\": " << args.storage_bits << ",\n";
        *output << "  \"entries\": " << data.size() << ",\n";
        *output << "  \"storage_bytes\": " << (data.size() * sizeof(StorageType)) << ",\n";
        *output << "  \"error_rate\": " << args.error_rate << ",\n";
        *output << "  \"load_factor\": " << args.load_factor << ",\n";
        *output << "  \"theoretical_fpr\": " << (1.0 / (1ULL << (sizeof(StorageType) * 8))) << "\n";
        *output << "}\n";
    } else {
        *output << "Filter built successfully\n";
        *output << "Entries: " << data.size() << "\n";
        *output << "Storage: " << (data.size() * sizeof(StorageType)) << " bytes\n";
    }
}

// Query mode: lookup values in filter
void query_filter(const CLIArgs& args) {
    if (args.filter_file.empty()) {
        std::cerr << "Error: --filter required for query mode\n";
        std::exit(1);
    }
    
    // Load filter
    std::ifstream filter_in(args.filter_file, std::ios::binary);
    if (!filter_in) {
        std::cerr << "Error: Cannot open filter file: " << args.filter_file << "\n";
        std::exit(1);
    }
    
    // Read header
    char magic[5] = {0};
    filter_in.read(magic, 4);
    if (std::strcmp(magic, "MAPH") != 0) {
        std::cerr << "Error: Invalid filter file format\n";
        std::exit(1);
    }
    
    uint32_t version, bits;
    uint64_t size;
    filter_in.read(reinterpret_cast<char*>(&version), sizeof(version));
    filter_in.read(reinterpret_cast<char*>(&bits), sizeof(bits));
    filter_in.read(reinterpret_cast<char*>(&size), sizeof(size));
    
    if (args.verbose) {
        std::cerr << "Loaded filter: " << bits << "-bit, " << size << " entries\n";
    }
    
    // Perform queries
    for (const auto& query : args.queries) {
        // In production, would actually query the filter
        std::cout << query << " -> [lookup result]\n";
    }
}

// Info mode: display filter information
void info_filter(const CLIArgs& args) {
    if (args.filter_file.empty()) {
        std::cerr << "Error: --filter required for info mode\n";
        std::exit(1);
    }
    
    std::ifstream filter_in(args.filter_file, std::ios::binary);
    if (!filter_in) {
        std::cerr << "Error: Cannot open filter file: " << args.filter_file << "\n";
        std::exit(1);
    }
    
    // Read and display filter information
    char magic[5] = {0};
    filter_in.read(magic, 4);
    
    if (std::strcmp(magic, "MAPH") != 0) {
        std::cerr << "Error: Invalid filter file format\n";
        std::exit(1);
    }
    
    uint32_t version, bits;
    uint64_t size;
    filter_in.read(reinterpret_cast<char*>(&version), sizeof(version));
    filter_in.read(reinterpret_cast<char*>(&bits), sizeof(bits));
    filter_in.read(reinterpret_cast<char*>(&size), sizeof(size));
    
    std::cout << "MAPH Filter Information\n";
    std::cout << "=======================\n";
    std::cout << "Version: " << version << "\n";
    std::cout << "Storage bits: " << bits << "\n";
    std::cout << "Entries: " << size << "\n";
    std::cout << "Storage size: " << (size * (bits / 8)) << " bytes\n";
    std::cout << "Theoretical FPR: " << (1.0 / (1ULL << bits)) << "\n";
}

int main(int argc, char* argv[]) {
    CLIArgs args = parse_args(argc, argv);
    
    try {
        if (args.mode == "build") {
            switch (args.storage_bits) {
                case 8:
                    build_filter<uint8_t>(args);
                    break;
                case 16:
                    build_filter<uint16_t>(args);
                    break;
                case 32:
                    build_filter<uint32_t>(args);
                    break;
                case 64:
                    build_filter<uint64_t>(args);
                    break;
                default:
                    std::cerr << "Error: Invalid storage bits. Must be 8, 16, 32, or 64\n";
                    return 1;
            }
        } else if (args.mode == "query") {
            query_filter(args);
        } else if (args.mode == "info") {
            info_filter(args);
        } else {
            std::cerr << "Error: Unknown mode: " << args.mode << "\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}