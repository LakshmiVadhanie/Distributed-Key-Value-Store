cmake_minimum_required(VERSION 3.16)
project(kvstore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Find dependencies ────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# gRPC / Protobuf (installed via apt or brew)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# ─── Protobuf / gRPC codegen ──────────────────────────────────────────────────
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_FILE "${PROTO_DIR}/kvstore.proto")

set(PROTO_SRCS "${CMAKE_BINARY_DIR}/kvstore.pb.cc")
set(PROTO_HDRS "${CMAKE_BINARY_DIR}/kvstore.pb.h")
set(GRPC_SRCS  "${CMAKE_BINARY_DIR}/kvstore.grpc.pb.cc")
set(GRPC_HDRS  "${CMAKE_BINARY_DIR}/kvstore.grpc.pb.h")

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND $<TARGET_FILE:protobuf::protoc>
    --grpc_out="${CMAKE_BINARY_DIR}"
    --cpp_out="${CMAKE_BINARY_DIR}"
    -I "${PROTO_DIR}"
    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
  COMMENT "Generating protobuf & gRPC stubs"
)

add_library(proto_gen STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(proto_gen PUBLIC "${CMAKE_BINARY_DIR}")
target_link_libraries(proto_gen PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

# ─── Include dirs ─────────────────────────────────────────────────────────────
include_directories(
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}"
)

# ─── Core libraries ───────────────────────────────────────────────────────────
add_library(consistent_hash
    src/hashing/consistent_hash.cpp
)
target_include_directories(consistent_hash PUBLIC include)

add_library(raft_consensus
    src/consensus/raft_node.cpp
)
target_include_directories(raft_consensus PUBLIC include)
target_link_libraries(raft_consensus PUBLIC proto_gen Threads::Threads)

add_library(request_router
    src/routing/request_router.cpp
)
target_include_directories(request_router PUBLIC include)
target_link_libraries(request_router PUBLIC consistent_hash proto_gen)

# ─── Node server binary ───────────────────────────────────────────────────────
add_executable(kv_node src/server/node_server.cpp)
target_link_libraries(kv_node PRIVATE
    raft_consensus
    consistent_hash
    request_router
    proto_gen
    Threads::Threads
)

# ─── CLI client binary ────────────────────────────────────────────────────────
add_executable(kv_client src/client/kv_client.cpp)
target_link_libraries(kv_client PRIVATE proto_gen Threads::Threads)

# ─── Tests ────────────────────────────────────────────────────────────────────
add_executable(kv_tests tests/test_kvstore.cpp)
target_include_directories(kv_tests PRIVATE include)
target_link_libraries(kv_tests PRIVATE
    raft_consensus
    consistent_hash
    proto_gen
    Threads::Threads
)

enable_testing()
add_test(NAME unit_tests COMMAND kv_tests)
