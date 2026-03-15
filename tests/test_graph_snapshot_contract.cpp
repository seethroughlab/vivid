#include "ui/graph_snapshot.h"
#include <cstdio>

using namespace vivid::ui;

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    GraphSnapshot snap;

    NodeSnapshot node_a;
    node_a.node_id = "a";
    NodeSnapshot node_b;
    node_b.node_id = "b";
    snap.nodes = {node_a, node_b};
    snap.node_index["a"] = 0;
    snap.node_index["b"] = 1;

    ConnectionSnapshot valid;
    valid.from_node = "a";
    valid.from_port = "out";
    valid.to_node = "b";
    valid.to_port = "in";

    ConnectionSnapshot broken;
    broken.from_node = "ghost";
    broken.from_port = "out";
    broken.to_node = "b";
    broken.to_port = "gain";
    broken.invalid = true;
    broken.from_endpoint_missing = true;
    broken.invalid_reason = "missing source node";

    snap.connections = {valid, broken};

    check(snap.find_node("a") != nullptr, "find_node resolves existing node");
    check(snap.find_node("missing") == nullptr, "find_node returns null for missing node");

    auto* valid_conn = snap.find_connection("a", "out", "b", "in");
    check(valid_conn != nullptr, "find_connection resolves valid wire");
    check(valid_conn && !valid_conn->is_broken(), "valid wire is not marked broken");

    auto* broken_conn = snap.find_connection("ghost", "out", "b", "gain");
    check(broken_conn != nullptr, "find_connection resolves broken wire");
    check(broken_conn && broken_conn->is_broken(), "broken wire remains visible in snapshot");
    check(broken_conn && broken_conn->invalid_reason == "missing source node",
          "broken wire preserves invalid_reason");

    check(snap.broken_connection_count() == 1, "broken_connection_count reports invalid wires");
    check(snap.has_broken_connections(), "has_broken_connections returns true when present");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
