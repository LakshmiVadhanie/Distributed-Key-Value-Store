#include "consensus/raft_node.h"
#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace kvstore {

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string role_to_str(RaftRole r) {
    switch (r) {
        case RaftRole::Leader:    return "leader";
        case RaftRole::Candidate: return "candidate";
        default:                  return "follower";
    }
}

// ─── Constructor / destructor ─────────────────────────────────────────────────

RaftNode::RaftNode(const std::string& node_id,
                   const std::vector<PeerInfo>& peers,
                   ApplyCallback apply_cb)
    : node_id_(node_id), peers_(peers), apply_cb_(std::move(apply_cb)),
      rng_(std::random_device{}())
{
    last_heartbeat_ = std::chrono::steady_clock::now();
}

RaftNode::~RaftNode() { stop(); }

void RaftNode::start() {
    running_ = true;
    timer_thread_     = std::thread(&RaftNode::run_timer,     this);
    heartbeat_thread_ = std::thread(&RaftNode::run_heartbeat, this);
}

void RaftNode::stop() {
    running_ = false;
    cv_.notify_all();
    if (timer_thread_.joinable())     timer_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
}

// ─── Log helpers ──────────────────────────────────────────────────────────────

int64_t RaftNode::last_log_index() const {
    return log_.empty() ? 0 : log_.back().index;
}

int64_t RaftNode::last_log_term() const {
    return log_.empty() ? 0 : log_.back().term;
}

std::string RaftNode::role_str() const {
    return role_to_str(role_.load());
}

// ─── Election timer ───────────────────────────────────────────────────────────

void RaftNode::run_timer() {
    while (running_) {
        std::uniform_int_distribution<int> dist(kElectionTimeoutMinMs,
                                                kElectionTimeoutMaxMs);
        int timeout_ms = dist(rng_);

        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));

        if (!running_) break;

        std::lock_guard<std::mutex> lk(mu_);
        if (role_ == RaftRole::Leader) continue;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_heartbeat_).count();

        if (elapsed >= timeout_ms) {
            std::cout << "[" << node_id_ << "] Election timeout – starting election\n";
            start_election();
        }
    }
}

// ─── Heartbeat (leader only) ──────────────────────────────────────────────────

void RaftNode::run_heartbeat() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kHeartbeatIntervalMs));
        if (!running_) break;

        std::lock_guard<std::mutex> lk(mu_);
        if (role_ != RaftRole::Leader) continue;

        for (auto& peer : peers_)
            replicate_to_peer(peer);
    }
}

// ─── Start election ───────────────────────────────────────────────────────────

void RaftNode::start_election() {
    // Already holding mu_
    current_term_++;
    role_     = RaftRole::Candidate;
    voted_for_ = node_id_;
    int64_t term = current_term_;
    int votes = 1;  // vote for self

    std::cout << "[" << node_id_ << "] Starting election for term " << term << "\n";

    for (auto& peer : peers_) {
        // Fire RPC without holding the lock (release temporarily)
        mu_.unlock();
        try {
            auto channel = grpc::CreateChannel(peer.address,
                                grpc::InsecureChannelCredentials());
            auto stub = RaftService::NewStub(channel);

            VoteRequest req;
            req.set_term(term);
            req.set_candidate_id(node_id_);
            req.set_last_log_index(last_log_index());
            req.set_last_log_term(last_log_term());

            VoteResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() +
                             std::chrono::milliseconds(100));
            auto status = stub->RequestVote(&ctx, req, &resp);

            mu_.lock();
            if (status.ok() && resp.vote_granted() && resp.term() == term) {
                votes++;
                std::cout << "[" << node_id_ << "] Got vote from " << peer.id
                          << " (total=" << votes << ")\n";
            } else if (status.ok() && resp.term() > current_term_) {
                step_down(resp.term());
                return;
            }
        } catch (...) {
            mu_.lock();
        }
    }

    int majority = static_cast<int>((peers_.size() + 1) / 2) + 1;
    if (role_ == RaftRole::Candidate && votes >= majority) {
        become_leader();
    }
}

void RaftNode::become_leader() {
    role_     = RaftRole::Leader;
    leader_id_ = node_id_;
    std::cout << "[" << node_id_ << "] Became leader for term " << current_term_ << "\n";

    for (auto& peer : peers_) {
        next_index_[peer.id]  = last_log_index() + 1;
        match_index_[peer.id] = 0;
    }
}

void RaftNode::step_down(int64_t new_term) {
    current_term_ = new_term;
    role_         = RaftRole::Follower;
    voted_for_    = "";
    last_heartbeat_ = std::chrono::steady_clock::now();
}

// ─── RPC Handlers ─────────────────────────────────────────────────────────────

RaftNode::VoteResult RaftNode::handle_vote_request(int64_t term,
                                                    const std::string& candidate_id,
                                                    int64_t last_log_index_c,
                                                    int64_t last_log_term_c) {
    std::lock_guard<std::mutex> lk(mu_);
    if (term < current_term_)
        return {current_term_, false};

    if (term > current_term_) step_down(term);

    bool log_ok = (last_log_term_c > last_log_term()) ||
                  (last_log_term_c == last_log_term() && last_log_index_c >= last_log_index());

    bool can_vote = (voted_for_.empty() || voted_for_ == candidate_id) && log_ok;
    if (can_vote) {
        voted_for_ = candidate_id;
        last_heartbeat_ = std::chrono::steady_clock::now();
        std::cout << "[" << node_id_ << "] Voted for " << candidate_id
                  << " in term " << term << "\n";
    }
    return {current_term_, can_vote};
}

RaftNode::AppendResult RaftNode::handle_append_entries(
        int64_t term, const std::string& leader_id,
        int64_t prev_log_index, int64_t prev_log_term,
        const std::vector<RaftLogEntry>& entries, int64_t leader_commit) {

    std::lock_guard<std::mutex> lk(mu_);

    if (term < current_term_)
        return {current_term_, false};

    if (term > current_term_) step_down(term);

    leader_id_ = leader_id;
    last_heartbeat_ = std::chrono::steady_clock::now();
    role_ = RaftRole::Follower;

    // Consistency check
    if (prev_log_index > 0) {
        if ((int64_t)log_.size() < prev_log_index)
            return {current_term_, false};
        if (log_[prev_log_index - 1].term != prev_log_term)
            return {current_term_, false};
    }

    // Append new entries
    for (auto& e : entries) {
        if (e.index <= (int64_t)log_.size()) {
            if (log_[e.index - 1].term != e.term) {
                log_.resize(e.index - 1);
                log_.push_back(e);
            }
        } else {
            log_.push_back(e);
        }
    }

    // Advance commit index
    if (leader_commit > commit_index_) {
        commit_index_ = std::min(leader_commit, (int64_t)log_.size());
        // Apply newly committed entries
        while (last_applied_ < commit_index_) {
            last_applied_++;
            apply_cb_(log_[last_applied_ - 1]);
        }
    }
    return {current_term_, true};
}

// ─── Replication ──────────────────────────────────────────────────────────────

void RaftNode::replicate_to_peer(const PeerInfo& peer) {
    // Called while holding mu_ – fire RPC without lock
    int64_t ni = next_index_.count(peer.id) ? next_index_[peer.id] : 1;
    int64_t prev_index = ni - 1;
    int64_t prev_term  = (prev_index > 0 && prev_index <= (int64_t)log_.size())
                         ? log_[prev_index - 1].term : 0;

    std::vector<RaftLogEntry> entries;
    for (int64_t i = ni; i <= (int64_t)log_.size(); ++i)
        entries.push_back(log_[i - 1]);

    int64_t term    = current_term_;
    int64_t commit  = commit_index_;
    std::string lid = node_id_;

    mu_.unlock();
    try {
        auto channel = grpc::CreateChannel(peer.address,
                            grpc::InsecureChannelCredentials());
        auto stub = RaftService::NewStub(channel);

        AppendEntriesRequest req;
        req.set_term(term);
        req.set_leader_id(lid);
        req.set_prev_log_index(prev_index);
        req.set_prev_log_term(prev_term);
        req.set_leader_commit(commit);
        for (auto& e : entries) {
            auto* pe = req.add_entries();
            pe->set_term(e.term);
            pe->set_index(e.index);
            pe->set_command(e.command);
        }

        AppendEntriesResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(100));
        auto status = stub->AppendEntries(&ctx, req, &resp);

        mu_.lock();
        if (status.ok()) {
            if (resp.success()) {
                if (!entries.empty()) {
                    match_index_[peer.id] = entries.back().index;
                    next_index_[peer.id]  = entries.back().index + 1;
                }
                check_commit();
            } else if (resp.term() > current_term_) {
                step_down(resp.term());
            } else {
                if (next_index_[peer.id] > 1)
                    next_index_[peer.id]--;
            }
        }
    } catch (...) {
        mu_.lock();
    }
}

void RaftNode::check_commit() {
    // Already holding mu_
    int64_t n = last_log_index();
    int majority = static_cast<int>((peers_.size() + 1) / 2) + 1;

    for (int64_t idx = commit_index_ + 1; idx <= n; ++idx) {
        if (log_[idx - 1].term != current_term_) continue;
        int count = 1;
        for (auto& [id, mi] : match_index_)
            if (mi >= idx) count++;
        if (count >= majority) {
            commit_index_ = idx;
            while (last_applied_ < commit_index_) {
                last_applied_++;
                apply_cb_(log_[last_applied_ - 1]);
            }
        }
    }
}

// ─── Propose (client path) ────────────────────────────────────────────────────

bool RaftNode::propose(const std::string& command) {
    std::lock_guard<std::mutex> lk(mu_);
    if (role_ != RaftRole::Leader) return false;

    RaftLogEntry e;
    e.term    = current_term_;
    e.index   = last_log_index() + 1;
    e.command = command;
    log_.push_back(e);
    std::cout << "[" << node_id_ << "] Proposed: " << command << "\n";
    return true;
}

} // namespace kvstore
