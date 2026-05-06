#include "hashing/consistent_hash.h"
#include <stdexcept>
#include <sstream>
#include <functional>
#include <algorithm>

namespace kvstore {

ConsistentHashRing::ConsistentHashRing(int virtual_nodes)
    : virtual_nodes_(virtual_nodes) {}

uint32_t ConsistentHashRing::hash(const std::string& s) const {
    // FNV-1a 32-bit
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

void ConsistentHashRing::add_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < virtual_nodes_; ++i) {
        std::string vkey = node_id + "#" + std::to_string(i);
        ring_[hash(vkey)] = node_id;
    }
}

void ConsistentHashRing::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < virtual_nodes_; ++i) {
        std::string vkey = node_id + "#" + std::to_string(i);
        ring_.erase(hash(vkey));
    }
}

std::string ConsistentHashRing::get_node(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (ring_.empty()) throw std::runtime_error("Hash ring is empty");
    uint32_t h = hash(key);
    auto it = ring_.lower_bound(h);
    if (it == ring_.end()) it = ring_.begin();
    return it->second;
}

std::vector<std::string> ConsistentHashRing::get_nodes(const std::string& key, int count) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (ring_.empty()) return {};

    uint32_t h = hash(key);
    auto it = ring_.lower_bound(h);

    std::vector<std::string> result;
    std::vector<std::string> seen;

    auto add_if_new = [&](const std::string& id) {
        if (std::find(seen.begin(), seen.end(), id) == seen.end()) {
            seen.push_back(id);
            result.push_back(id);
        }
    };

    // Walk clockwise from the key's position
    for (auto cur = it; cur != ring_.end() && (int)result.size() < count; ++cur)
        add_if_new(cur->second);
    for (auto cur = ring_.begin(); cur != it && (int)result.size() < count; ++cur)
        add_if_new(cur->second);

    return result;
}

std::vector<std::string> ConsistentHashRing::all_nodes() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> nodes;
    for (auto& [h, id] : ring_) {
        if (std::find(nodes.begin(), nodes.end(), id) == nodes.end())
            nodes.push_back(id);
    }
    return nodes;
}

size_t ConsistentHashRing::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    // Count unique physical nodes
    std::vector<std::string> seen;
    for (auto& [h, id] : ring_)
        if (std::find(seen.begin(), seen.end(), id) == seen.end())
            seen.push_back(id);
    return seen.size();
}

} // namespace kvstore
