#!/usr/bin/env bash
# partition_test.sh – simulate network partitions and verify recovery
# Requires: iptables (Linux) or pfctl (macOS), and the cluster already running.

set -euo pipefail

PORTS=(50051 50052 50053 50054 50055)
CLIENT="$(cd "$(dirname "$0")/.." && pwd)/build/kv_client"

info()  { echo "  [INFO]  $*"; }
ok()    { echo "  [OK]    $*"; }
fail()  { echo "  [FAIL]  $*"; }

check_deps() {
    [[ -f "$CLIENT" ]] || { echo "kv_client not found – build first."; exit 1; }
    command -v iptables &>/dev/null || { echo "iptables not found (Linux only)."; exit 1; }
    [[ $EUID -eq 0 ]] || { echo "Run as root for iptables."; exit 1; }
}

# ─── Partition: block traffic to node (port) ──────────────────────────────────
partition_node() {
    local port=$1
    info "Partitioning node on port $port"
    iptables -A INPUT  -p tcp --dport "$port" -j DROP
    iptables -A OUTPUT -p tcp --sport "$port" -j DROP
}

heal_node() {
    local port=$1
    info "Healing node on port $port"
    iptables -D INPUT  -p tcp --dport "$port" -j DROP 2>/dev/null || true
    iptables -D OUTPUT -p tcp --sport "$port" -j DROP 2>/dev/null || true
}

heal_all() {
    for p in "${PORTS[@]}"; do heal_node "$p"; done
}

# ─── Write a key and verify it's readable ────────────────────────────────────
verify_kv() {
    local key=$1 value=$2
    echo "put $key $value" | "$CLIENT" localhost:50051 &>/dev/null
    local got
    got=$(echo "get $key" | "$CLIENT" localhost:50051 2>/dev/null | grep -v "^>" | head -1)
    if [[ "$got" == "$value" ]]; then
        ok "key=$key value=$value ✓"
    else
        fail "key=$key expected=$value got=$got"
    fi
}

# ─── Main test sequence ───────────────────────────────────────────────────────
run_tests() {
    echo ""
    echo "=== Partition Test ==="
    echo ""

    info "Writing baseline key before any partition"
    verify_kv "baseline" "hello"

    echo ""
    info "Partitioning node5 (port 50055)"
    partition_node 50055
    sleep 1

    info "Writing key during partition"
    verify_kv "during_partition" "world"

    echo ""
    info "Healing partition"
    heal_node 50055
    sleep 2   # allow log replication to catch up

    info "Verifying key is still readable"
    verify_kv "during_partition" "world"

    echo ""
    info "Partitioning majority (nodes 4 & 5)"
    partition_node 50054
    partition_node 50055
    sleep 0.5
    info "Writing should still succeed (3 of 5 nodes alive)"
    verify_kv "majority_test" "still_works"

    echo ""
    info "Healing all"
    heal_all

    echo ""
    echo "=== Partition test complete ==="
}

check_deps
trap heal_all EXIT
run_tests
