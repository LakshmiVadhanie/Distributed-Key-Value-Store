#!/usr/bin/env bash
# run_cluster.sh – starts a 5-node cluster locally for development
# Usage: ./scripts/run_cluster.sh [start|stop|status]

set -euo pipefail

BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)/build"
NODE="$BUILD_DIR/kv_node"

PIDS_FILE="/tmp/kvstore_pids"

BASE_PORT=50051

declare -a NODES=("node1" "node2" "node3" "node4" "node5")

# ─── Build check ─────────────────────────────────────────────────────────────
check_build() {
    if [[ ! -f "$NODE" ]]; then
        echo "Binary not found. Building..."
        cmake -S "$(dirname "$0")/.." -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
    fi
}

# ─── Build peer list for node $1 (index) ─────────────────────────────────────
peer_args() {
    local me=$1
    local args=""
    for i in "${!NODES[@]}"; do
        if [[ $i -ne $me ]]; then
            local port=$((BASE_PORT + i))
            args+=" ${NODES[$i]}:127.0.0.1:${port}"
        fi
    done
    echo "$args"
}

start_cluster() {
    check_build
    rm -f "$PIDS_FILE"
    echo "Starting 5-node KV cluster..."
    for i in "${!NODES[@]}"; do
        local id="${NODES[$i]}"
        local port=$((BASE_PORT + i))
        local addr="0.0.0.0:${port}"
        local peers
        peers=$(peer_args "$i")
        # shellcheck disable=SC2086
        "$NODE" "$id" "$addr" $peers &> "/tmp/kvstore_${id}.log" &
        echo $! >> "$PIDS_FILE"
        echo "  ✓ $id  →  localhost:$port  (pid $!)"
    done
    echo ""
    echo "Cluster running. Logs: /tmp/kvstore_node{1..5}.log"
    echo "Connect:  ./build/kv_client localhost:50051"
    echo "Stop:     $0 stop"
}

stop_cluster() {
    if [[ ! -f "$PIDS_FILE" ]]; then
        echo "No PID file found – cluster may not be running."
        return
    fi
    echo "Stopping cluster..."
    while IFS= read -r pid; do
        kill "$pid" 2>/dev/null && echo "  killed $pid" || true
    done < "$PIDS_FILE"
    rm -f "$PIDS_FILE"
}

show_status() {
    CLIENT="$BUILD_DIR/kv_client"
    [[ ! -f "$CLIENT" ]] && { echo "Client binary not found."; exit 1; }
    for i in "${!NODES[@]}"; do
        local port=$((BASE_PORT + i))
        echo "─── ${NODES[$i]} (localhost:$port) ───"
        echo "status" | "$CLIENT" "localhost:$port" 2>/dev/null || echo "  (unreachable)"
        echo ""
    done
}

case "${1:-start}" in
    start)  start_cluster ;;
    stop)   stop_cluster  ;;
    status) show_status   ;;
    *)      echo "Usage: $0 [start|stop|status]"; exit 1 ;;
esac
