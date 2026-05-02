#pragma once
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace kvstore {

/**
 * ConsistentHashRing
 *
 * Maps keys to node IDs using a virtual-node ring.
 * Each physical node gets `virtual_nodes` slots on the ring so that
 * load is distributed evenly even with a small cluster.
 */
class ConsistentHashRing {
public:
    explicit ConsistentHashRing(int virtual_nodes = 150);

    // Add / remove a physical node
    void add_node(const std::string& node_id);
    void remove_node(const std::string& node_id);

    // Which physical node owns this key?
    std::string get_node(const std::string& key) const;

    // Returns the ordered list of physical nodes responsible for `key`
    // (primary + replicas up to `count`).
    std::vector<std::string> get_nodes(const std::string& key, int count) const;

    std::vector<std::string> all_nodes() const;
    size_t size() const;

private:
    uint32_t hash(const std::string& s) const;

    int                       virtual_nodes_;
    std::map<uint32_t, std::string> ring_;   // hash → node_id
    mutable std::mutex        mu_;
};

} // namespace kvstore
