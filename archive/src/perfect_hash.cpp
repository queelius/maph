/**
 * @file perfect_hash.cpp
 * @brief Implementation of perfect hash functionality
 */

#include "maph/perfect_hash.hpp"
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <filesystem>
#include <cassert>
#include <fstream>

namespace maph {

// Simple RecSplit implementation - in production would use full RecSplit library
struct RecSplitHash::RecSplitImpl {
    std::unordered_map<std::string, uint64_t> key_to_hash;
    uint64_t next_hash{0};
    PerfectHashConfig config;
    
    bool build(const std::vector<std::string>& keys, const PerfectHashConfig& cfg) {
        config = cfg;
        key_to_hash.clear();
        next_hash = 0;
        
        // Simple implementation: assign sequential hashes
        // In production, this would use the actual RecSplit algorithm
        for (const auto& key : keys) {
            if (key_to_hash.find(key) == key_to_hash.end()) {
                key_to_hash[key] = next_hash++;
            }
        }
        return true;
    }
    
    std::optional<uint64_t> hash(JsonView key) const {
        auto it = key_to_hash.find(std::string(key));
        if (it != key_to_hash.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    uint64_t max_hash() const {
        return next_hash > 0 ? next_hash - 1 : 0;
    }
    
    size_t memory_usage() const {
        return sizeof(*this) + key_to_hash.size() * (sizeof(std::string) + sizeof(uint64_t) + 64);
    }
    
    std::vector<uint8_t> serialize() const {
        std::ostringstream oss;
        oss.write(reinterpret_cast<const char*>(&config), sizeof(config));
        
        uint64_t num_keys = key_to_hash.size();
        oss.write(reinterpret_cast<const char*>(&num_keys), sizeof(num_keys));
        
        for (const auto& [key, hash] : key_to_hash) {
            uint32_t key_len = key.size();
            oss.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            oss.write(key.data(), key_len);
            oss.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        }
        
        std::string str = oss.str();
        return std::vector<uint8_t>(str.begin(), str.end());
    }
    
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(config)) return false;
        
        std::istringstream iss(std::string(data.begin(), data.end()));
        
        iss.read(reinterpret_cast<char*>(&config), sizeof(config));
        
        uint64_t num_keys;
        iss.read(reinterpret_cast<char*>(&num_keys), sizeof(num_keys));
        
        key_to_hash.clear();
        next_hash = 0;
        
        for (uint64_t i = 0; i < num_keys; ++i) {
            uint32_t key_len;
            if (!iss.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) return false;
            
            std::string key(key_len, '\0');
            if (!iss.read(key.data(), key_len)) return false;
            
            uint64_t hash_val;
            if (!iss.read(reinterpret_cast<char*>(&hash_val), sizeof(hash_val))) return false;
            
            key_to_hash[key] = hash_val;
            next_hash = std::max(next_hash, hash_val + 1);
        }
        
        return true;
    }
};

RecSplitHash::RecSplitHash(const PerfectHashConfig& config) 
    : impl_(std::make_unique<RecSplitImpl>()), config_(config) {
    impl_->config = config;
}

RecSplitHash::~RecSplitHash() = default;

bool RecSplitHash::build(const std::vector<std::string>& keys) {
    key_count_ = keys.size();
    return impl_->build(keys, config_);
}

std::optional<uint64_t> RecSplitHash::hash(JsonView key) const {
    return impl_->hash(key);
}

uint64_t RecSplitHash::max_hash() const {
    return impl_->max_hash();
}

std::vector<uint8_t> RecSplitHash::serialize() const {
    return impl_->serialize();
}

bool RecSplitHash::deserialize(const std::vector<uint8_t>& data) {
    return impl_->deserialize(data);
}

size_t RecSplitHash::memory_usage() const {
    return sizeof(*this) + impl_->memory_usage();
}

// StandardHash implementation
std::optional<uint64_t> StandardHash::hash(JsonView key) const {
    // FNV-1a hash
    uint32_t h = 2166136261u;
    for (unsigned char c : key) {
        h ^= c;
        h *= 16777619u;
    }
    if (h == 0) h = 1;  // Never return 0
    
    return h % static_cast<uint32_t>(num_slots_);
}

std::vector<uint8_t> StandardHash::serialize() const {
    std::vector<uint8_t> data(sizeof(num_slots_) + sizeof(key_count_));
    std::memcpy(data.data(), &num_slots_, sizeof(num_slots_));
    std::memcpy(data.data() + sizeof(num_slots_), &key_count_, sizeof(key_count_));
    return data;
}

bool StandardHash::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() != sizeof(num_slots_) + sizeof(key_count_)) return false;
    
    std::memcpy(&num_slots_, data.data(), sizeof(num_slots_));
    std::memcpy(&key_count_, data.data() + sizeof(num_slots_), sizeof(key_count_));
    return true;
}

// Factory implementation
std::unique_ptr<PerfectHashInterface> PerfectHashFactory::create(const PerfectHashConfig& config) {
    switch (config.type) {
        case PerfectHashType::RECSPLIT:
            return std::make_unique<RecSplitHash>(config);
        case PerfectHashType::CHD:
            // Would implement CHD here
            return std::make_unique<RecSplitHash>(config);
        case PerfectHashType::BBHASH:
            // Would implement BBHash here
            return std::make_unique<RecSplitHash>(config);
        case PerfectHashType::DISABLED:
        default:
            // Return nullptr for disabled - caller should use StandardHash
            return nullptr;
    }
}

std::unique_ptr<PerfectHashInterface> PerfectHashFactory::build(
    const std::vector<std::string>& keys,
    const PerfectHashConfig& config) {
    
    auto hash_func = create(config);
    if (!hash_func) return nullptr;
    
    if (auto* recsplit = dynamic_cast<RecSplitHash*>(hash_func.get())) {
        if (recsplit->build(keys)) {
            return hash_func;
        }
    }
    
    return nullptr;
}

std::unique_ptr<PerfectHashInterface> PerfectHashFactory::load(
    const std::vector<uint8_t>& data,
    PerfectHashType type) {
    
    PerfectHashConfig config{type};
    auto hash_func = create(config);
    if (!hash_func) return nullptr;
    
    if (hash_func->deserialize(data)) {
        return hash_func;
    }
    
    return nullptr;
}

// KeyJournal implementation
KeyJournal::KeyJournal(const std::string& journal_path) 
    : journal_path_(journal_path) {
    
    // Create directory if it doesn't exist
    std::filesystem::path path(journal_path_);
    std::filesystem::create_directories(path.parent_path());
    
    // Open journal file in append mode
    journal_file_.open(journal_path_, std::ios::app);
}

KeyJournal::~KeyJournal() {
    flush();
}

void KeyJournal::record_insert(JsonView key, uint32_t value_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Write to journal: I:<key_length>:<key>:<value_hash>\n
    journal_file_ << "I:" << key.size() << ":" << key << ":" << value_hash << "\n";
    
    if (caching_enabled_) {
        std::string key_str(key);
        auto it = std::find(cached_keys_.begin(), cached_keys_.end(), key_str);
        if (it == cached_keys_.end()) {
            cached_keys_.push_back(key_str);
        }
    }
}

void KeyJournal::record_remove(JsonView key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Write to journal: R:<key_length>:<key>\n
    journal_file_ << "R:" << key.size() << ":" << key << "\n";
    
    if (caching_enabled_) {
        std::string key_str(key);
        auto it = std::find(cached_keys_.begin(), cached_keys_.end(), key_str);
        if (it != cached_keys_.end()) {
            cached_keys_.erase(it);
        }
    }
}

std::vector<std::string> KeyJournal::get_active_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (caching_enabled_ && !cached_keys_.empty()) {
        return cached_keys_;
    }
    
    // Load from journal file
    std::unordered_set<std::string> active_keys;
    std::ifstream file(journal_path_);
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        char op = line[0];
        if (op != 'I' && op != 'R') continue;
        
        // Parse: OP:<key_length>:<key>:...
        size_t first_colon = line.find(':', 1);
        if (first_colon == std::string::npos) continue;
        
        size_t second_colon = line.find(':', first_colon + 1);
        if (second_colon == std::string::npos) continue;
        
        try {
            size_t key_len = std::stoul(line.substr(first_colon + 1, second_colon - first_colon - 1));
            std::string key = line.substr(second_colon + 1, key_len);
            
            if (op == 'I') {
                active_keys.insert(key);
            } else if (op == 'R') {
                active_keys.erase(key);
            }
        } catch (const std::exception&) {
            // Skip malformed lines
            continue;
        }
    }
    
    return std::vector<std::string>(active_keys.begin(), active_keys.end());
}

size_t KeyJournal::load_keys(bool force_reload) {
    if (!force_reload && caching_enabled_ && !cached_keys_.empty()) {
        return cached_keys_.size();
    }
    
    auto keys = get_active_keys();
    
    if (caching_enabled_) {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_keys_ = keys;
    }
    
    return keys.size();
}

void KeyJournal::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    journal_file_.close();
    std::filesystem::remove(journal_path_);
    
    journal_file_.open(journal_path_, std::ios::app);
    cached_keys_.clear();
}

KeyJournal::Stats KeyJournal::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Stats stats{};
    stats.total_keys = caching_enabled_ ? cached_keys_.size() : 0;
    stats.is_cached = caching_enabled_;
    
    if (std::filesystem::exists(journal_path_)) {
        stats.journal_size_bytes = std::filesystem::file_size(journal_path_);
    }
    
    if (caching_enabled_) {
        stats.memory_usage_bytes = cached_keys_.size() * 64;  // Rough estimate
        for (const auto& key : cached_keys_) {
            stats.memory_usage_bytes += key.size();
        }
    }
    
    if (stats.total_keys == 0) {
        // Count from file
        std::ifstream file(journal_path_);
        std::string line;
        std::unordered_set<std::string> unique_keys;
        
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == 'R') continue;
            if (line[0] == 'I') {
                size_t first_colon = line.find(':', 1);
                size_t second_colon = line.find(':', first_colon + 1);
                if (first_colon != std::string::npos && second_colon != std::string::npos) {
                    try {
                        size_t key_len = std::stoul(line.substr(first_colon + 1, second_colon - first_colon - 1));
                        std::string key = line.substr(second_colon + 1, key_len);
                        unique_keys.insert(key);
                    } catch (const std::exception&) {}
                }
            }
        }
        stats.total_keys = unique_keys.size();
    }
    
    return stats;
}

void KeyJournal::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    journal_file_.flush();
}

size_t KeyJournal::compact() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get active keys
    auto active_keys = get_active_keys();
    
    // Write compacted journal
    std::string temp_path = journal_path_ + ".tmp";
    std::ofstream temp_file(temp_path);
    
    size_t removed_count = 0;
    if (temp_file.is_open()) {
        for (const auto& key : active_keys) {
            temp_file << "I:" << key.size() << ":" << key << ":0\n";
        }
        temp_file.close();
        
        // Count old entries
        std::ifstream old_file(journal_path_);
        std::string line;
        while (std::getline(old_file, line)) {
            if (!line.empty()) removed_count++;
        }
        old_file.close();
        
        removed_count -= active_keys.size();
        
        // Replace old with new
        journal_file_.close();
        std::filesystem::rename(temp_path, journal_path_);
        journal_file_.open(journal_path_, std::ios::app);
    }
    
    return removed_count;
}

} // namespace maph