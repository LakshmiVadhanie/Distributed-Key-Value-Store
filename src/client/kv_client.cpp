#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>

#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"

using kvstore::KVStore;
using kvstore::GetRequest;    using kvstore::GetResponse;
using kvstore::PutRequest;    using kvstore::PutResponse;
using kvstore::DeleteRequest; using kvstore::DeleteResponse;
using kvstore::StatusRequest; using kvstore::StatusResponse;

class KVClient {
public:
    explicit KVClient(const std::string& address)
        : stub_(KVStore::NewStub(
              grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {}

    bool put(const std::string& key, const std::string& value) {
        PutRequest req; req.set_key(key); req.set_value(value);
        PutResponse resp;
        grpc::ClientContext ctx;
        auto status = stub_->Put(&ctx, req, &resp);
        if (!status.ok()) { std::cerr << "RPC error: " << status.error_message() << "\n"; return false; }
        if (!resp.success()) { std::cerr << "Put failed: " << resp.error() << "\n"; return false; }
        return true;
    }

    std::pair<std::string, bool> get(const std::string& key) {
        GetRequest req; req.set_key(key);
        GetResponse resp;
        grpc::ClientContext ctx;
        auto status = stub_->Get(&ctx, req, &resp);
        if (!status.ok()) return {"", false};
        return {resp.value(), resp.found()};
    }

    bool del(const std::string& key) {
        DeleteRequest req; req.set_key(key);
        DeleteResponse resp;
        grpc::ClientContext ctx;
        auto status = stub_->Delete(&ctx, req, &resp);
        if (!status.ok() || !resp.success()) return false;
        return true;
    }

    void print_status() {
        StatusRequest req;
        StatusResponse resp;
        grpc::ClientContext ctx;
        auto status = stub_->Status(&ctx, req, &resp);
        if (!status.ok()) { std::cerr << "Status RPC failed\n"; return; }
        std::cout << "Node:     " << resp.node_id()   << "\n"
                  << "Role:     " << resp.role()      << "\n"
                  << "Leader:   " << resp.leader_id() << "\n"
                  << "Term:     " << resp.term()      << "\n"
                  << "Keys:     " << resp.keys_held() << "\n";
    }

private:
    std::unique_ptr<KVStore::Stub> stub_;
};

// ─── Benchmark mode ───────────────────────────────────────────────────────────

void run_bench(KVClient& client, int ops) {
    std::mt19937 rng(42);
    auto start = std::chrono::steady_clock::now();
    int ok = 0;

    for (int i = 0; i < ops; ++i) {
        std::string key   = "bench_key_" + std::to_string(rng() % 1000);
        std::string value = "value_" + std::to_string(i);
        if (client.put(key, value)) ok++;
    }

    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << std::fixed << std::setprecision(1)
              << "Wrote " << ok << "/" << ops << " keys in "
              << elapsed << "s  →  " << (ok / elapsed) << " ops/sec\n";
}

// ─── REPL ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string addr = "localhost:50051";
    if (argc >= 2) addr = argv[1];

    KVClient client(addr);
    std::cout << "KV-Client → " << addr << "\n"
              << "Commands: put <k> <v>  |  get <k>  |  del <k>  "
                 "|  status  |  bench <n>  |  quit\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "quit" || cmd == "exit") break;

        if (cmd == "put") {
            std::string k, v;
            ss >> k >> v;
            std::cout << (client.put(k, v) ? "OK" : "FAIL") << "\n";
        } else if (cmd == "get") {
            std::string k; ss >> k;
            auto [val, found] = client.get(k);
            std::cout << (found ? val : "(not found)") << "\n";
        } else if (cmd == "del") {
            std::string k; ss >> k;
            std::cout << (client.del(k) ? "OK" : "FAIL") << "\n";
        } else if (cmd == "status") {
            client.print_status();
        } else if (cmd == "bench") {
            int n = 1000; ss >> n;
            run_bench(client, n);
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }
    return 0;
}
