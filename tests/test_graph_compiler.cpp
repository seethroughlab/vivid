// Integration tests for GraphCompiler::compile() edge cases:
// - empty graph
// - missing operators (placeholder nodes)
// - cycle detection
// - diamond topology
// - mixed cadence partitioning

#include "runtime/graph_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/lane_types.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// ---------------------------------------------------------------------------
// Test 1: Empty graph compiles to valid empty CompiledGraph
// ---------------------------------------------------------------------------

static void test_empty_graph() {
    std::fprintf(stderr, "\n--- compile: empty graph ---\n");

    vivid::Graph g;
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "empty graph compiles successfully");
    if (cg) {
        check(cg->nodes.empty(), "no nodes");
        check(cg->edges.empty(), "no edges");
        check(cg->frame_order.empty(), "empty frame_order");
        check(cg->audio_order.empty(), "empty audio_order");
    }
}

// ---------------------------------------------------------------------------
// Test 2: Missing operator creates placeholder
// ---------------------------------------------------------------------------

static void test_missing_operator_placeholder() {
    std::fprintf(stderr, "\n--- compile: missing operator placeholder ---\n");

    vivid::Graph g;
    g.add_node("n1", "NonExistentOp");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compiles with missing operator");
    if (cg) {
        check(cg->nodes.size() == 1, "1 node");
        check(cg->nodes[0].missing_operator, "node marked missing_operator");
        check(cg->nodes[0].instance == nullptr, "no instance for missing op");
        check(cg->nodes[0].active_cadence == vivid::Cadence::Frame,
              "missing op defaults to Frame cadence");
    }
}

// ---------------------------------------------------------------------------
// Test 3: Missing operator port inference from connections
// ---------------------------------------------------------------------------

static void test_missing_operator_port_inference() {
    std::fprintf(stderr, "\n--- compile: missing operator port inference ---\n");

    vivid::Graph g;
    g.add_node("src", "UnknownSrc");
    g.add_node("dst", "UnknownDst");
    g.add_connection("src", "out_a", "dst", "in_x");
    g.add_connection("src", "out_b", "dst", "in_y");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compiles with unknown ops");
    if (cg) {
        auto* src = cg->find_node("src");
        auto* dst = cg->find_node("dst");
        check(src != nullptr && dst != nullptr, "both nodes found");
        if (src && dst) {
            check(src->output_port_count == 2, "src has 2 inferred output ports");
            check(dst->input_port_count == 2, "dst has 2 inferred input ports");
            check(src->output_port_indices.count("out_a") == 1, "out_a indexed");
            check(src->output_port_indices.count("out_b") == 1, "out_b indexed");
            check(dst->input_port_indices.count("in_x") == 1, "in_x indexed");
            check(dst->input_port_indices.count("in_y") == 1, "in_y indexed");
        }
    }
}

// ---------------------------------------------------------------------------
// Test 4: Cycle detection returns nullptr
// ---------------------------------------------------------------------------

static void test_cycle_detection() {
    std::fprintf(stderr, "\n--- compile: cycle detection ---\n");

    vivid::Graph g;
    g.add_node("a", "UnknownA");
    g.add_node("b", "UnknownB");
    g.add_node("c", "UnknownC");
    // a → b → c → a (cycle)
    g.add_connection("a", "out", "b", "in");
    g.add_connection("b", "out", "c", "in");
    g.add_connection("c", "out", "a", "in");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg == nullptr, "cycle detected → nullptr");
}

// ---------------------------------------------------------------------------
// Test 5: Diamond topology — correct edge count, topo order valid
// ---------------------------------------------------------------------------

static void test_diamond_topology() {
    std::fprintf(stderr, "\n--- compile: diamond topology ---\n");

    //   A
    //  / \
    // B   C
    //  \ /
    //   D
    vivid::Graph g;
    g.add_node("a", "UnknownA");
    g.add_node("b", "UnknownB");
    g.add_node("c", "UnknownC");
    g.add_node("d", "UnknownD");
    g.add_connection("a", "out", "b", "in");
    g.add_connection("a", "out2", "c", "in");
    g.add_connection("b", "out", "d", "in1");
    g.add_connection("c", "out", "d", "in2");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "diamond compiles");
    if (cg) {
        check(cg->nodes.size() == 4, "4 nodes");
        check(cg->edges.size() == 4, "4 edges");

        // Verify topological ordering: a before b,c; b,c before d
        uint32_t idx_a = cg->node_id_to_index["a"];
        uint32_t idx_b = cg->node_id_to_index["b"];
        uint32_t idx_c = cg->node_id_to_index["c"];
        uint32_t idx_d = cg->node_id_to_index["d"];

        // In sorted order, positions should satisfy: a < b, a < c, b < d, c < d
        // (since all are in frame_order which preserves topo sort)
        uint32_t pos_a = UINT32_MAX, pos_b = UINT32_MAX, pos_c = UINT32_MAX, pos_d = UINT32_MAX;
        for (uint32_t i = 0; i < cg->frame_order.size(); ++i) {
            if (cg->frame_order[i] == idx_a) pos_a = i;
            if (cg->frame_order[i] == idx_b) pos_b = i;
            if (cg->frame_order[i] == idx_c) pos_c = i;
            if (cg->frame_order[i] == idx_d) pos_d = i;
        }
        check(pos_a < pos_b, "a before b in topo order");
        check(pos_a < pos_c, "a before c in topo order");
        check(pos_b < pos_d, "b before d in topo order");
        check(pos_c < pos_d, "c before d in topo order");

        // d should have 2 upstream nodes
        check(cg->nodes[idx_d].upstream_nodes.size() == 2,
              "d has 2 upstream nodes");
    }
}

// ---------------------------------------------------------------------------
// Test 6: Linear chain — correct execution order
// ---------------------------------------------------------------------------

static void test_linear_chain() {
    std::fprintf(stderr, "\n--- compile: linear chain ---\n");

    vivid::Graph g;
    g.add_node("a", "Unknown");
    g.add_node("b", "Unknown");
    g.add_node("c", "Unknown");
    g.add_connection("a", "out", "b", "in");
    g.add_connection("b", "out", "c", "in");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "linear chain compiles");
    if (cg) {
        check(cg->frame_order.size() == 3, "3 nodes in frame_order");
        // Verify order: a → b → c
        auto idx_a = cg->node_id_to_index["a"];
        auto idx_b = cg->node_id_to_index["b"];
        auto idx_c = cg->node_id_to_index["c"];

        uint32_t pos_a = UINT32_MAX, pos_b = UINT32_MAX, pos_c = UINT32_MAX;
        for (uint32_t i = 0; i < cg->frame_order.size(); ++i) {
            if (cg->frame_order[i] == idx_a) pos_a = i;
            if (cg->frame_order[i] == idx_b) pos_b = i;
            if (cg->frame_order[i] == idx_c) pos_c = i;
        }
        check(pos_a < pos_b && pos_b < pos_c, "a → b → c topo order");
    }
}

// ---------------------------------------------------------------------------
// Test 7: Disconnected nodes compile fine
// ---------------------------------------------------------------------------

static void test_disconnected_nodes() {
    std::fprintf(stderr, "\n--- compile: disconnected nodes ---\n");

    vivid::Graph g;
    g.add_node("x", "UnknownX");
    g.add_node("y", "UnknownY");
    g.add_node("z", "UnknownZ");
    // No connections

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "disconnected nodes compile");
    if (cg) {
        check(cg->nodes.size() == 3, "3 nodes");
        check(cg->edges.empty(), "no edges");
        check(cg->frame_order.size() == 3, "all in frame_order");
    }
}

// ---------------------------------------------------------------------------
// Test 8: Mixed real + missing operators with real operator dylibs
// ---------------------------------------------------------------------------

static void test_mixed_real_and_missing(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- compile: mixed real + missing operators ---\n");

    const std::string staging = build_dir + "/.test_graph_compiler_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("lfo.dylib");
    stage("gain.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    vivid::Graph g;
    g.add_node("lfo1", "LFO");
    g.add_node("mystery", "NonExistent");
    g.add_node("gain1", "Gain");
    g.add_connection("lfo1", "value", "mystery", "input");
    g.add_connection("mystery", "output", "gain1", "gain");

    vivid::GraphCompiler::Options opts;
    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "mixed graph compiles");
    if (cg) {
        check(cg->nodes.size() == 3, "3 nodes");

        auto* lfo = cg->find_node("lfo1");
        auto* mystery = cg->find_node("mystery");
        auto* gain = cg->find_node("gain1");

        check(lfo && !lfo->missing_operator, "lfo is real");
        check(mystery && mystery->missing_operator, "mystery is placeholder");
        check(gain && !gain->missing_operator, "gain is real");

        // Check that lfo has a valid instance
        check(lfo && lfo->instance != nullptr, "lfo has instance");
        // mystery has no instance
        check(mystery && mystery->instance == nullptr, "mystery has no instance");
    }

    std::filesystem::remove_all(staging);
}

// ---------------------------------------------------------------------------
// Test 9: node_id_to_index is correct after topo sort reindexing
// ---------------------------------------------------------------------------

static void test_node_id_to_index() {
    std::fprintf(stderr, "\n--- compile: node_id_to_index after reindex ---\n");

    vivid::Graph g;
    g.add_node("z_last", "Unknown");
    g.add_node("a_first", "Unknown");
    g.add_connection("a_first", "out", "z_last", "in");

    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compiles");
    if (cg) {
        auto it_a = cg->node_id_to_index.find("a_first");
        auto it_z = cg->node_id_to_index.find("z_last");
        check(it_a != cg->node_id_to_index.end(), "a_first in index");
        check(it_z != cg->node_id_to_index.end(), "z_last in index");
        if (it_a != cg->node_id_to_index.end() && it_z != cg->node_id_to_index.end()) {
            check(cg->nodes[it_a->second].node_id == "a_first", "index maps to correct node");
            check(cg->nodes[it_z->second].node_id == "z_last", "index maps to correct node");
        }
    }
}

// ---------------------------------------------------------------------------
// Test: Lane behavior from real operator descriptors
// ---------------------------------------------------------------------------

static void test_lane_behavior_from_descriptor(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- compile: lane behavior from descriptor ---\n");

    const std::string staging = build_dir + "/.test_lane_behavior_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("spread_noise.dylib");
    stage("lfo.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Build a graph: SpreadNoise → LFO (via param connection or port)
    vivid::Graph g;
    g.add_node("sn", "SpreadNoise");
    g.add_node("lfo1", "LFO");

    vivid::GraphCompiler::Options opts;
    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compiles with real operators");
    if (!cg) return;

    auto* sn = cg->find_node("sn");
    auto* lfo = cg->find_node("lfo1");
    check(sn != nullptr, "SpreadNoise node found");
    check(lfo != nullptr, "LFO node found");
    if (!sn || !lfo) return;

    check(sn->lane_behavior == vivid::LaneBehavior::Structural,
          "SpreadNoise classified as Structural");
    check(lfo->lane_behavior == vivid::LaneBehavior::Pointwise,
          "LFO classified as Pointwise (default)");

    // Structural node should have gotten a fresh lane_set_id on its outputs.
    bool has_fresh_id = false;
    for (const auto& ls : sn->output_lane_sets) {
        if (ls.lane_set_id != 0)
            has_fresh_id = true;
    }
    check(has_fresh_id, "SpreadNoise outputs have fresh lane_set_id");
    check(cg->next_lane_set_id > 1, "lane_set_id counter advanced");

    std::filesystem::remove_all(staging);
}

// ---------------------------------------------------------------------------
// Test: Different-provenance non-scalar inputs fail compilation
// ---------------------------------------------------------------------------

static void test_lane_mismatch_fails(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- compile: lane-set mismatch fails ---\n");

    const std::string staging = build_dir + "/.test_lane_mismatch_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("spread_noise.dylib");
    stage("lfo.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Two different SpreadNoise nodes wired to the same LFO input.
    // Each SpreadNoise is Structural → gets a different lane_set_id.
    // LFO is Pointwise → receiving two different non-scalar lane sets is illegal.
    vivid::Graph g;
    g.add_node("sn1", "SpreadNoise");
    g.add_node("sn2", "SpreadNoise");
    g.add_node("lfo1", "LFO");
    g.add_connection("sn1", "values", "lfo1", "gate");
    g.add_connection("sn2", "values", "lfo1", "gate");

    vivid::GraphCompiler::Options opts;
    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg == nullptr,
          "compilation fails: two different-provenance non-scalar inputs to pointwise node");

    std::filesystem::remove_all(staging);
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "=== test_graph_compiler ===\n");

    test_empty_graph();
    test_missing_operator_placeholder();
    test_missing_operator_port_inference();
    test_cycle_detection();
    test_diamond_topology();
    test_linear_chain();
    test_disconnected_nodes();
    test_mixed_real_and_missing(build_dir);
    test_node_id_to_index();
    test_lane_behavior_from_descriptor(build_dir);
    test_lane_mismatch_fails(build_dir);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
