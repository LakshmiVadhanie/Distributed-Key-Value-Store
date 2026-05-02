#pragma once
#include "hashing/consistent_hash.h"
#include <string>
#include <map>
#include <mutex>
#include <optional>

namespace kvstore {

/**
 * RequestRouter
 *
 * Uses the consistent hash ring to decide which node owns each key,
 * then forwards the request via gRPC if the current node is not the
 * correct owner (automatic failover included).
 */
class RequestRouter {
public:
    struct RouteResult {
        bool        success;
        std::string value;   // for GET
        std::string error;
    };

    RequestRouter(const std::string& local_node_id,
                  ConsistentHashRing& ring);

    // Register a node's gRPC endpoint
    void register_node(const std::string& node_id, const std::string& address);
    void deregister_node(const std::string& node_id);

    // Returns the address for a given key's primary node
    std::string route_key(const std::string& key) const;

    // Forward GET to the responsible node (with one failover retry)
    RouteResult forward_get(const std::string& key);

    // Forward PUT/DELETE to the responsible node's leader
    RouteResult forward_put(const std::string& key, const std::string& value);
    RouteResult forward_delete(const std::string& key);

    bool is_local(const std::string& key) const;

    RouteResult try_get(const std::string& address, const std::string& key);
    RouteResult try_put(const std::string& address, const std::string& key, const std::string& value);
    RouteResult try_delete(const std::string& address, const std::string& key);
    std::string get_address(const std::string& node_id) const;

private:
    std::string                         local_node_id_;
    ConsistentHashRing&                 ring_;
    std::map<std::string, std::string>  node_addresses_;  // node_id → host:port
    mutable std::mutex                  mu_;

};

} // namespace kvstore
