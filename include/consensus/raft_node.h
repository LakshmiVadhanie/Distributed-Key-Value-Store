#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <condition_variable>
#include <random>
#include <chrono>

namespace kvstore {

enum class RaftRole { Follower, Candidate, Leader };

struct RaftLogEntry {
    int64_t     term;
    int64_t     index;
    std::string command;   // "PUT key value" or "DELETE key"
};

struct PeerInfo {
    std::string id;
    std::string address;   // host:port
};

/**
 * RaftNode implements the Raft consensus algorithm.
 *
 * Responsibilities:
 *  - Leader election via randomised election timeouts
 *  - Log replication via AppendEntries RPCs to all peers
 *  - Commit entries once a majority acknowledges them
 *  - Notify the state-machine via apply_callback when entries commit
 */
class RaftNode {
public:
    using ApplyCallback = std::function<void(const RaftLogEntry&)>;

    RaftNode(const std::string& node_id,
             const std::vector<PeerInfo>& peers,
             ApplyCallback apply_cb);
    ~RaftNode();

    // Start / stop background threads
    void start();
    void stop();

    // --- RPC handlers (called by the gRPC service) ---
    struct VoteResult   { int64_t term; bool granted; };
    struct AppendResult { int64_t term; bool success; };

    VoteResult   handle_vote_request(int64_t term,
                                     const std::string& candidate_id,
                                     int64_t last_log_index,
                                     int64_t last_log_term);

    AppendResult handle_append_entries(int64_t term,
                                       const std::string& leader_id,
                                       int64_t prev_log_index,
                                       int64_t prev_log_term,
                                       const std::vector<RaftLogEntry>& entries,
                                       int64_t leader_commit);

    // --- Client-facing helpers ---
    // Propose a command (only succeeds on leader). Returns false if not leader.
    bool propose(const std::string& command);

    // Accessors
    bool        is_leader()   const { return role_ == RaftRole::Leader; }
    std::string role_str()    const;
    std::string node_id()     const { return node_id_; }
    std::string leader_id()   const { return leader_id_; }
    int64_t     current_term() const { return current_term_; }

private:
    // --- Internal timers & election ---
    void run_timer();          // election-timeout loop
    void run_heartbeat();      // leader heartbeat loop
    void start_election();
    void become_leader();
    void step_down(int64_t new_term);

    // --- Replication ---
    void replicate_to_peer(const PeerInfo& peer);
    void check_commit();

    // Logging helpers
    int64_t last_log_index() const;
    int64_t last_log_term()  const;

    // --- State ---
    std::string           node_id_;
    std::vector<PeerInfo> peers_;
    ApplyCallback         apply_cb_;

    // Persistent Raft state (simplified: in-memory for this implementation)
    std::atomic<int64_t>  current_term_{0};
    std::string           voted_for_;
    std::vector<RaftLogEntry> log_;

    // Volatile state
    std::atomic<int64_t>  commit_index_{0};
    std::atomic<int64_t>  last_applied_{0};

    // Leader state
    std::map<std::string, int64_t> next_index_;
    std::map<std::string, int64_t> match_index_;

    std::atomic<RaftRole> role_{RaftRole::Follower};
    std::string           leader_id_;

    // Timing
    std::chrono::steady_clock::time_point last_heartbeat_;
    std::mt19937                           rng_;

    mutable std::mutex mu_;
    std::condition_variable cv_;

    std::atomic<bool>  running_{false};
    std::thread        timer_thread_;
    std::thread        heartbeat_thread_;

    // Election timeout: random in [150, 300] ms
    static constexpr int kElectionTimeoutMinMs = 150;
    static constexpr int kElectionTimeoutMaxMs = 300;
    static constexpr int kHeartbeatIntervalMs  = 50;
};

} // namespace kvstore
