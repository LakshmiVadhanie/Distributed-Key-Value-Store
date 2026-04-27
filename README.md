# Distributed Key-Value Store

A production-grade, fault-tolerant distributed key-value store written in **C++17** using:

- **Consistent Hashing** - FNV-1a ring with 150 virtual nodes per physical node for even load distribution
- **Raft Consensus** - Leader election, log replication, and commit across a 5-node cluster
- **gRPC** - Type-safe RPC layer for both client ↔ node and node ↔ node communication
- **Docker / Docker Compose** - One-command cluster launch

---

## Architecture

```
Client
  │  gRPC (KVStore service)
  ▼
┌─────────────────────────────────────┐
│  RequestRouter (consistent hashing) │  ← routes key to owning node
└────────┬───────────────────────────┘
         │ forward if not local
    ┌────▼────┐   ┌─────────┐   ┌─────────┐
    │  node1  │◄──│  node2  │◄──│  node3  │  …5 nodes total
    │ (leader)│──►│follower │──►│follower │
    └────┬────┘   └─────────┘   └─────────┘
         │  Raft AppendEntries (log replication)
         ▼
    KVStateMachine (in-memory HashMap)
```

### Key Design Decisions

| Component          | Choice                       | Why                                  |
| ------------------ | ---------------------------- | ------------------------------------ |
| Hash function      | FNV-1a 32-bit                | Fast, low collision, no dependencies |
| Virtual nodes      | 150 per physical node        | Balances load within ±5% for 5 nodes |
| Election timeout   | 150–300 ms random            | Avoids split-votes                   |
| Heartbeat interval | 50 ms                        | Well below election timeout floor    |
| Failover           | Try primary → replica on GET | Reads can be served by any node      |

---

## Using the CLI Client

```bash
./build/kv_client localhost:50051
```

```
> put foo bar          # write
OK
> get foo              # read
bar
> del foo              # delete
OK
> status               # node info (role, term, key count)
Node:   node1
Role:   leader
Leader: node1
Term:   3
Keys:   42
> bench 10000          # write 10K keys and report ops/sec
Wrote 10000/10000 keys in 0.9s  →  11111.1 ops/sec
> quit
```

---

## Running Tests

```bash
cmake --build build --target kv_tests
./build/kv_tests
```

Expected output:

```
=== KV-Store Unit Tests ===

[PASS] ring: single node always returns that node
[PASS] ring: two nodes split keys
[PASS] ring: remove node reroutes keys
[PASS] ring: get_nodes returns correct count
[PASS] ring: size tracks physical nodes
[PASS] raft single-node: becomes leader
[PASS] raft single-node: propose succeeds as leader
[PASS] raft single-node: apply callback fired

8/8 tests passed.
```

---

## Network Partition Simulation (Linux, requires root)

```bash
sudo ./scripts/partition_test.sh
```

The script uses `iptables` to drop traffic to individual nodes, verifies that
writes still succeed while a minority is partitioned, and confirms data
survives after the partition heals.

---

## Project Structure

```
.
├── proto/
│   └── kvstore.proto          # KVStore + Raft gRPC service definitions
├── include/
│   ├── consensus/
│   │   └── raft_node.h        # Raft state machine header
│   ├── hashing/
│   │   └── consistent_hash.h  # Ring header
│   └── routing/
│       └── request_router.h   # Router header
├── src/
│   ├── consensus/
│   │   └── raft_node.cpp      # Leader election, log replication
│   ├── hashing/
│   │   └── consistent_hash.cpp
│   ├── routing/
│   │   └── request_router.cpp # gRPC forwarding + failover
│   ├── server/
│   │   └── node_server.cpp    # gRPC server entry point
│   └── client/
│       └── kv_client.cpp      # Interactive CLI + benchmark
├── tests/
│   └── test_kvstore.cpp       # Unit tests (no external framework)
├── scripts/
│   ├── run_cluster.sh         # Local 5-node cluster launcher
│   └── partition_test.sh      # Network partition simulator
├── docker/
│   ├── Dockerfile
│   └── docker-compose.yml     # 5-node compose cluster
└── CMakeLists.txt
```
