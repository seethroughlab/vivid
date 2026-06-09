// Value-flow runtime correctness (lane-value Phase 8a).
//
// The successor to the deleted test_value_flow.cpp (which proved the value-flow
// pass equivalent to the now-removed lane sets). This proves the value model
// directly: for each multiplicity behavior, the compiler's Pass 2.7 infers the
// right output ValueEnvelope (multiplicity + provenance_group_id) AND the runtime
// produces the right values. Covers MAP / GENERATE / REDUCE / KERNEL via existing
// fixtures + COLLECT / PRESERVE via mock fixtures (no seed op declares those).

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_types.h"
#include "operator_api/value_model.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

namespace {

// Output port index of a named output on a compiled node.
int out_port(const vivid::CompiledNode* cn, const char* name) {
    auto it = cn->output_port_indices.find(name);
    return it == cn->output_port_indices.end() ? -1 : static_cast<int>(it->second);
}

const vivid::ValueEnvelope* out_env(const vivid::CompiledNode* cn, const char* name) {
    int p = out_port(cn, name);
    if (p < 0 || p >= static_cast<int>(cn->output_value_envelopes.size())) return nullptr;
    return &cn->output_value_envelopes[p];
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_value_flow_runtime_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");   // GENERATE
    stage("lane_sink_op.dylib");     // MAP (passthrough sink)
    stage("lane_frame_op.dylib");    // MAP (strategy-independent, LoopBased)
    stage("lane_smooth_op.dylib");   // KERNEL
    stage("reduce_op.dylib");        // REDUCE (mock)
    stage("collect_op.dylib");       // COLLECT (mock)
    stage("preserve_op.dylib");      // PRESERVE (mock)

    std::fprintf(stderr, "\n=== test_value_flow_runtime ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- GENERATE: scalar params → Many output ---
    std::fprintf(stderr, "\n--- GENERATE (LaneSourceOp) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 2.0f}, {"count", 4.0f}});
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "GENERATE: builds");
        check_graph_clean(rt.compiled_graph(), "GENERATE");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* src  = rt.compiled_graph()->find_node("src");
        auto* sink = rt.compiled_graph()->find_node("sink");
        if (src) {
            const auto* e = out_env(src, "out");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_MANY, "GENERATE: src output is Many");
            check(e && e->provenance_group_id > 1, "GENERATE: src minted a provenance group");
        }
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "GENERATE: 4 values flow to sink");
            if (sp.size() == 4) {
                check_float(sp[0], 2.0f, 0.01f, "GENERATE v0 = 2");
                check_float(sp[3], 8.0f, 0.01f, "GENERATE v3 = 8");
            }
        }
    }

    // --- MAP: Many in → Many out, count preserved, provenance forwarded ---
    std::fprintf(stderr, "\n--- MAP (LaneFrameOp, LoopBased) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 3.0f}, {"count", 4.0f}});
        g.add_node("map", "LaneFrameOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "map", "input");
        g.add_connection("map", "output", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "MAP: builds");
        check_graph_clean(rt.compiled_graph(), "MAP");
        rt.tick(0.0, 1.0 / 60.0, 0);   // 1 tick: LaneFrameOp accumulates input once
        auto* src = rt.compiled_graph()->find_node("src");
        auto* mp  = rt.compiled_graph()->find_node("map");
        auto* sink = rt.compiled_graph()->find_node("sink");
        if (mp) {
            const auto* e = out_env(mp, "output");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_MANY, "MAP: output stays Many");
            const auto* se = src ? out_env(src, "out") : nullptr;
            check(e && se && e->provenance_group_id == se->provenance_group_id,
                  "MAP: forwards source provenance group");
        }
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "MAP: 4 values preserved");
            if (sp.size() == 4) {
                check_float(sp[0], 3.0f, 0.01f, "MAP lane0 accum = 3 (1 tick)");
                check_float(sp[3], 12.0f, 0.01f, "MAP lane3 accum = 12");
            }
        }
    }

    // --- KERNEL: whole-collection access, Many out ---
    std::fprintf(stderr, "\n--- KERNEL (LaneSmoothOp) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});  // [1,2,3,4]
        g.add_node("k", "LaneSmoothOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "k", "in");
        g.add_connection("k", "out", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "KERNEL: builds");
        check_graph_clean(rt.compiled_graph(), "KERNEL");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* k = rt.compiled_graph()->find_node("k");
        auto* sink = rt.compiled_graph()->find_node("sink");
        if (k) {
            const auto* e = out_env(k, "out");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_MANY, "KERNEL: output is Many");
        }
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "KERNEL: 4 smoothed values");
            if (sp.size() == 4) {
                // 3-element moving avg of [1,2,3,4] (edges clamp): [(1+1+2)/3, (1+2+3)/3, (2+3+4)/3, (3+4+4)/3]
                check_float(sp[1], 2.0f, 0.01f, "KERNEL v1 = 2.0");
                check_float(sp[2], 3.0f, 0.01f, "KERNEL v2 = 3.0");
            }
        }
    }

    // --- REDUCE: Many in → scalar out ---
    std::fprintf(stderr, "\n--- REDUCE (ReduceOp mock) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 2.0f}, {"count", 4.0f}});  // [2,4,6,8] sum=20
        g.add_node("r", "ReduceOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "r", "in");
        g.add_connection("r", "out", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "REDUCE: builds");
        check_graph_clean(rt.compiled_graph(), "REDUCE");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* r = rt.compiled_graph()->find_node("r");
        if (r) {
            const auto* e = out_env(r, "out");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_SCALAR, "REDUCE: output is Scalar");
            check(e && e->provenance_group_id == 0, "REDUCE: scalar output has no provenance group");
            check_float(r->output_values[out_port(r, "out")], 20.0f, 0.01f, "REDUCE sum = 20");
        }
    }

    // --- COLLECT: output Many regardless of input (mock) ---
    std::fprintf(stderr, "\n--- COLLECT (CollectOp mock) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 5.0f}, {"count", 1.0f}});  // scalar 5
        g.add_node("c", "CollectOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "c", "in");
        g.add_connection("c", "out", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "COLLECT: builds");
        check_graph_clean(rt.compiled_graph(), "COLLECT");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* c = rt.compiled_graph()->find_node("c");
        auto* sink = rt.compiled_graph()->find_node("sink");
        if (c) {
            const auto* e = out_env(c, "out");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_MANY, "COLLECT: output is Many");
            check(e && e->provenance_group_id > 1, "COLLECT: minted a provenance group");
        }
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 3, "COLLECT: 3 collected values");
            if (sp.size() == 3) check_float(sp[0], 5.0f, 0.01f, "COLLECT v0 = 5");
        }
    }

    // --- PRESERVE: output multiplicity follows input (mock) ---
    std::fprintf(stderr, "\n--- PRESERVE (PreserveOp mock) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 3.0f}});  // [1,2,3]
        g.add_node("p", "PreserveOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "p", "in");
        g.add_connection("p", "out", "sink", "in");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "PRESERVE: builds");
        check_graph_clean(rt.compiled_graph(), "PRESERVE");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* p = rt.compiled_graph()->find_node("p");
        auto* sink = rt.compiled_graph()->find_node("sink");
        if (p) {
            const auto* e = out_env(p, "out");
            check(e && e->multiplicity == VIVID_MULTIPLICITY_MANY, "PRESERVE: Many in → Many out");
            const auto* se = rt.compiled_graph()->find_node("src");
            const auto* sev = se ? out_env(se, "out") : nullptr;
            check(e && sev && e->provenance_group_id == sev->provenance_group_id,
                  "PRESERVE: forwards source provenance group");
        }
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 3, "PRESERVE: 3 values passed through");
            if (sp.size() == 3) {
                check_float(sp[0], 1.0f, 0.01f, "PRESERVE v0 = 1");
                check_float(sp[2], 3.0f, 0.01f, "PRESERVE v2 = 3");
            }
        }
    }

    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
