#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <sstream>

#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include "consensus/raft_node.h"
#include "hashing/consistent_hash.h"
#include "routing/request_router.h"

namespace kvstore {

// ─── In-memory state machine ──────────────────────────────────────────────────

class KVStateMachine {
public:
    void apply(const std::string& command) {
        std::istringstream ss(command);
        std::string op, key, value;
        ss >> op >> key;

        std::lock_guard<std::mutex> lk(mu_);
        if (op == "PUT") {
            ss >> value;
            store_[key] = value;
        } else if (op == "DELETE") {
            store_.erase(key);
        }
    }

    std::pair<std::string, bool> get(const std::string& key) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = store_.find(key);
        if (it == store_.end()) return {"", false};
        return {it->second, true};
    }

    int size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return static_cast<int>(store_.size());
    }

private:
    mutable std::mutex                       mu_;
    std::unordered_map<std::string, std::string> store_;
};

// ─── gRPC service implementations ─────────────────────────────────────────────

class KVStoreServiceImpl final : public KVStore::Service {
public:
    KVStoreServiceImpl(const std::string& node_id,
                       KVStateMachine& sm,
                       RaftNode& raft,
                       RequestRouter& router)
        : node_id_(node_id), sm_(sm), raft_(raft), router_(router) {}

    grpc::Status Get(grpc::ServerContext*, const GetRequest* req,
                     GetResponse* resp) override {
        auto [val, found] = sm_.get(req->key());
        resp->set_found(found);
        resp->set_value(val);
        return grpc::Status::OK;
    }

    grpc::Status Put(grpc::ServerContext*, const PutRequest* req,
                     PutResponse* resp) override {
        if (!raft_.is_leader()) {
            // Proxy to leader if known
            auto leader = raft_.leader_id();
            if (!leader.empty() && leader != node_id_) {
                std::string leader_addr = router_.get_address(leader);
                auto result = router_.try_put(leader_addr, req->key(), req->value());
                resp->set_success(result.success);
                resp->set_error(result.error);
                return grpc::Status::OK;
            }
            resp->set_success(false);
            resp->set_error("not leader, leader=" + leader);
            return grpc::Status::OK;
        }
        std::string cmd = "PUT " + req->key() + " " + req->value();
        bool ok = raft_.propose(cmd);
        resp->set_success(ok);
        if (!ok) resp->set_error("propose failed");
        return grpc::Status::OK;
    }

    grpc::Status Delete(grpc::ServerContext*, const DeleteRequest* req,
                        DeleteResponse* resp) override {
        if (!raft_.is_leader()) {
            auto leader = raft_.leader_id();
            if (!leader.empty() && leader != node_id_) {
                std::string leader_addr = router_.get_address(leader);
                auto result = router_.try_delete(leader_addr, req->key());
                resp->set_success(result.success);
                resp->set_error(result.error);
                return grpc::Status::OK;
            }
            resp->set_success(false);
            resp->set_error("not leader, leader=" + leader);
            return grpc::Status::OK;
        }
        bool ok = raft_.propose("DELETE " + req->key());
        resp->set_success(ok);
        if (!ok) resp->set_error("propose failed");
        return grpc::Status::OK;
    }

    grpc::Status Status(grpc::ServerContext*, const StatusRequest*,
                        StatusResponse* resp) override {
        resp->set_node_id(node_id_);
        resp->set_role(raft_.role_str());
        resp->set_leader_id(raft_.leader_id());
        resp->set_term(raft_.current_term());
        resp->set_keys_held(sm_.size());
        return grpc::Status::OK;
    }

private:
    std::string      node_id_;
    KVStateMachine&  sm_;
    RaftNode&        raft_;
    RequestRouter&   router_;
};

class RaftServiceImpl final : public RaftService::Service {
public:
    explicit RaftServiceImpl(RaftNode& raft) : raft_(raft) {}

    grpc::Status RequestVote(grpc::ServerContext*, const VoteRequest* req,
                             VoteResponse* resp) override {
        auto result = raft_.handle_vote_request(
            req->term(), req->candidate_id(),
            req->last_log_index(), req->last_log_term());
        resp->set_term(result.term);
        resp->set_vote_granted(result.granted);
        return grpc::Status::OK;
    }

    grpc::Status AppendEntries(grpc::ServerContext*, const AppendEntriesRequest* req,
                               AppendEntriesResponse* resp) override {
        std::vector<RaftLogEntry> entries;
        for (auto& e : req->entries())
            entries.push_back({e.term(), e.index(), e.command()});

        auto result = raft_.handle_append_entries(
            req->term(), req->leader_id(),
            req->prev_log_index(), req->prev_log_term(),
            entries, req->leader_commit());
        resp->set_term(result.term);
        resp->set_success(result.success);
        return grpc::Status::OK;
    }

private:
    RaftNode& raft_;
};

} // namespace kvstore

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <node_id> <listen_addr> [peer_id:peer_addr ...]\n";
        return 1;
    }

    std::string node_id    = argv[1];
    std::string listen_addr = argv[2];

    std::vector<kvstore::PeerInfo> peers;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        auto colon = arg.find(':');
        if (colon != std::string::npos) {
            // Format: id:host:port
            auto second_colon = arg.find(':', colon + 1);
            if (second_colon != std::string::npos) {
                peers.push_back({arg.substr(0, colon),
                                 arg.substr(colon + 1)});
            }
        }
    }

    // ── State machine
    kvstore::KVStateMachine sm;

    // ── Raft node
    kvstore::RaftNode raft(node_id, peers,
        [&sm](const kvstore::RaftLogEntry& e) { sm.apply(e.command); });

    // ── Consistent hash ring & router
    kvstore::ConsistentHashRing ring;
    kvstore::RequestRouter router(node_id, ring);

    // Register self
    router.register_node(node_id, listen_addr);
    // Register peers
    for (auto& p : peers)
        router.register_node(p.id, p.address);

    // ── gRPC server
    kvstore::KVStoreServiceImpl kv_svc(node_id, sm, raft, router);
    kvstore::RaftServiceImpl    raft_svc(raft);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&kv_svc);
    builder.RegisterService(&raft_svc);

    auto server = builder.BuildAndStart();
    std::cout << "[" << node_id << "] Listening on " << listen_addr << "\n";

    raft.start();
    server->Wait();
    raft.stop();
    return 0;
}
