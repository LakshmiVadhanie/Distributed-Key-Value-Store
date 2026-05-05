#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#include "hashing/consistent_hash.h"
#include "consensus/raft_node.h"

// ─── Color helpers ────────────────────────────────────────────────────────────
#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

static int tests_run = 0, tests_passed = 0;

#define TEST(name) \
    { bool _ok = true; std::string _name = name; tests_run++;
#define EXPECT(cond) \
    if (!(cond)) { std::cout << RED "[FAIL] " RESET << _name << " – " #cond "\n"; _ok = false; }
#define END_TEST \
    if (_ok) { tests_passed++; std::cout << GREEN "[PASS] " RESET << _name << "\n"; } }

// ─── ConsistentHashRing tests ─────────────────────────────────────────────────

void test_ring_basic() {
    using namespace kvstore;
    ConsistentHashRing ring;

    TEST("ring: single node always returns that node")
        ring.add_node("n1");
        EXPECT(ring.get_node("any_key") == "n1");
        EXPECT(ring.get_node("another") == "n1");
    END_TEST

    TEST("ring: two nodes split keys")
        ring.add_node("n2");
        std::string a = ring.get_node("apple");
        std::string b = ring.get_node("banana");
        // At least one key per node over many keys
        int n1_count = 0, n2_count = 0;
        for (int i = 0; i < 1000; ++i) {
            auto n = ring.get_node("key" + std::to_string(i));
            if (n == "n1") n1_count++;
            else           n2_count++;
        }
        EXPECT(n1_count > 300 && n2_count > 300);
    END_TEST

    TEST("ring: remove node reroutes keys")
        ConsistentHashRing r2;
        r2.add_node("n1"); r2.add_node("n2"); r2.add_node("n3");
        auto before = r2.get_node("test_key");
        r2.remove_node(before);
        auto after = r2.get_node("test_key");
        EXPECT(before != after);
    END_TEST

    TEST("ring: get_nodes returns correct count")
        ConsistentHashRing r3;
        r3.add_node("a"); r3.add_node("b"); r3.add_node("c");
        auto nodes = r3.get_nodes("mykey", 2);
        EXPECT(nodes.size() == 2);
        // All unique
        EXPECT(nodes[0] != nodes[1]);
    END_TEST

    TEST("ring: size tracks physical nodes")
        ConsistentHashRing r4;
        r4.add_node("x"); r4.add_node("y");
        EXPECT(r4.size() == 2);
        r4.remove_node("x");
        EXPECT(r4.size() == 1);
    END_TEST
}

// ─── Raft basic tests (single node) ──────────────────────────────────────────

void test_raft_single_node() {
    using namespace kvstore;

    std::vector<std::string> applied;
    std::mutex applied_mu;

    RaftNode node("solo", {}, [&](const RaftLogEntry& e) {
        std::lock_guard<std::mutex> lk(applied_mu);
        applied.push_back(e.command);
    });
    node.start();

    // Give time to self-elect (single-node majority = 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    TEST("raft single-node: becomes leader")
        EXPECT(node.is_leader());
    END_TEST

    TEST("raft single-node: propose succeeds as leader")
        bool ok = node.propose("PUT foo bar");
        EXPECT(ok);
    END_TEST

    // Let it commit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TEST("raft single-node: apply callback fired")
        std::lock_guard<std::mutex> lk(applied_mu);
        EXPECT(!applied.empty());
        EXPECT(applied[0] == "PUT foo bar");
    END_TEST

    node.stop();
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n=== KV-Store Unit Tests ===\n\n";
    test_ring_basic();
    test_raft_single_node();
    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
