#include "routing/request_router.h"
#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include <iostream>

namespace kvstore {

RequestRouter::RequestRouter(const std::string& local_node_id,
                             ConsistentHashRing& ring)
    : local_node_id_(local_node_id), ring_(ring) {}

void RequestRouter::register_node(const std::string& node_id,
                                   const std::string& address) {
    std::lock_guard<std::mutex> lk(mu_);
    node_addresses_[node_id] = address;
    ring_.add_node(node_id);
}

void RequestRouter::deregister_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lk(mu_);
    node_addresses_.erase(node_id);
    ring_.remove_node(node_id);
}

std::string RequestRouter::route_key(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string node = ring_.get_node(key);
    auto it = node_addresses_.find(node);
    return (it != node_addresses_.end()) ? it->second : "";
}

std::string RequestRouter::get_address(const std::string& node_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = node_addresses_.find(node_id);
    return (it != node_addresses_.end()) ? it->second : "";
}

bool RequestRouter::is_local(const std::string& key) const {
    return ring_.get_node(key) == local_node_id_;
}

// ─── Forwarding helpers ────────────────────────────────────────────────────────

RequestRouter::RouteResult RequestRouter::try_get(const std::string& address,
                                                   const std::string& key) {
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub    = KVStore::NewStub(channel);

    GetRequest req; req.set_key(key);
    GetResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

    auto status = stub->Get(&ctx, req, &resp);
    if (!status.ok())
        return {false, "", "RPC failed: " + status.error_message()};
    if (!resp.found())
        return {false, "", "key not found"};
    return {true, resp.value(), ""};
}

RequestRouter::RouteResult RequestRouter::try_put(const std::string& address,
                                                   const std::string& key,
                                                   const std::string& value) {
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub    = KVStore::NewStub(channel);

    PutRequest req; req.set_key(key); req.set_value(value);
    PutResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

    auto status = stub->Put(&ctx, req, &resp);
    if (!status.ok() || !resp.success())
        return {false, "", status.ok() ? resp.error() : status.error_message()};
    return {true, "", ""};
}

RequestRouter::RouteResult RequestRouter::try_delete(const std::string& address,
                                                      const std::string& key) {
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub    = KVStore::NewStub(channel);

    DeleteRequest req; req.set_key(key);
    DeleteResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

    auto status = stub->Delete(&ctx, req, &resp);
    if (!status.ok() || !resp.success())
        return {false, "", status.ok() ? resp.error() : status.error_message()};
    return {true, "", ""};
}

// ─── Public routing methods ────────────────────────────────────────────────────

RequestRouter::RouteResult RequestRouter::forward_get(const std::string& key) {
    auto nodes = ring_.get_nodes(key, 2);   // primary + one replica for failover
    for (auto& node_id : nodes) {
        std::string addr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = node_addresses_.find(node_id);
            if (it == node_addresses_.end()) continue;
            addr = it->second;
        }
        auto result = try_get(addr, key);
        if (result.success) return result;
        std::cout << "[router] GET failover: " << node_id << " failed, trying next\n";
    }
    return {false, "", "all replicas unreachable"};
}

RequestRouter::RouteResult RequestRouter::forward_put(const std::string& key,
                                                       const std::string& value) {
    std::string addr = route_key(key);
    if (addr.empty()) return {false, "", "no node found for key"};
    return try_put(addr, key, value);
}

RequestRouter::RouteResult RequestRouter::forward_delete(const std::string& key) {
    std::string addr = route_key(key);
    if (addr.empty()) return {false, "", "no node found for key"};
    return try_delete(addr, key);
}

} // namespace kvstore
