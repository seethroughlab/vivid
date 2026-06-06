// Test: the frame value-view API (lane-value clean-break, Phase 4a).
//
// ValueGainOp consumes its input via ctx->values (VividValueView) and produces
// via ctx->value_outputs (VividValueOutput) — the value-model API — instead of
// the lane views. It runs through real frame execution between lane-API neighbors
// (LaneSourceOp → ValueGainOp → LaneSinkOp), proving the value API produces the
// same result as the lane equivalent and interoperates with the lane transport.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "test_helpers.h"

static const vivid::CompiledNode* find_node(const vivid::CompiledGraph* cg, const char* id) {
    for (const auto& n : cg->nodes) if (n.node_id == id) return &n;
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::string build_dir = (argc > 1) ? argv[1] : ".";

    // Stage the operators this test needs.
    std::string staging = build_dir + "/.test_frame_value_api_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* dylib) {
        std::filesystem::copy_file(build_dir + "/" + dylib, staging + "/" + dylib,
            std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("value_gain_op.dylib");
    stage("lane_sink_op.dylib");

    // Write the graph: a many chain and a single-element chain, each routed
    // through the value-API ValueGainOp between lane-API source/sink.
    std::string graph_path = build_dir + "/test_frame_value_api.json";
    {
        std::ofstream f(graph_path);
        f << R"({
    "nodes": {
        "src3":   { "type": "LaneSourceOp", "params": { "base": 1.0, "count": 3 } },
        "gain_m": { "type": "ValueGainOp",  "params": { "gain": 2.0 } },
        "sink_m": { "type": "LaneSinkOp" },
        "src1":   { "type": "LaneSourceOp", "params": { "base": 5.0, "count": 1 } },
        "gain_s": { "type": "ValueGainOp",  "params": { "gain": 3.0 } },
        "sink_s": { "type": "LaneSinkOp" }
    },
    "connections": [
        { "from": "src3/out",   "to": "gain_m/input" },
        { "from": "gain_m/output", "to": "sink_m/in" },
        { "from": "src1/out",   "to": "gain_s/input" },
        { "from": "gain_s/output", "to": "sink_s/in" }
    ]
})";
    }

    std::fprintf(stderr, "\n=== Test: Frame Value-View API ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    check_graph_clean(runtime.compiled_graph(), "frame value api");

    runtime.tick(0.0, 1.0 / 60.0, 0);
    const auto* cg = runtime.compiled_graph();

    // ---- Many: src3 [1,2,3] → ×2 via value API → [2,4,6] ----
    std::fprintf(stderr, "\n--- many value (Map via the value API) ---\n");
    const auto* gain_m = find_node(cg, "gain_m");
    const auto* sink_m = find_node(cg, "sink_m");
    check(gain_m && sink_m, "found many-chain nodes");
    if (gain_m) {
        check(gain_m->output_lanes.size() > 0 && gain_m->output_lanes[0].size() == 3,
              "value-API op produced 3 values");
        if (gain_m->output_lanes.size() > 0 && gain_m->output_lanes[0].size() == 3) {
            check_float(gain_m->output_lanes[0][0], 2.0f, 0.01f, "values[0]*2 = 2");
            check_float(gain_m->output_lanes[0][1], 4.0f, 0.01f, "values[1]*2 = 4");
            check_float(gain_m->output_lanes[0][2], 6.0f, 0.01f, "values[2]*2 = 6");
        }
    }
    if (sink_m && sink_m->output_lanes.size() > 0 && sink_m->output_lanes[0].size() == 3) {
        // The lane-API sink read the value-API op's output — interop holds.
        check_float(sink_m->output_lanes[0][2], 6.0f, 0.01f, "lane-API sink saw value-API output [2]=6");
    } else {
        check(false, "lane-API sink received the value-API output (3 elems)");
    }

    // ---- Single element: src1 [5] → ×3 via value API → [15] ----
    std::fprintf(stderr, "\n--- single value through the value API ---\n");
    const auto* gain_s = find_node(cg, "gain_s");
    check(gain_s != nullptr, "found single-chain gain");
    if (gain_s && gain_s->output_lanes.size() > 0 && gain_s->output_lanes[0].size() >= 1) {
        check_float(gain_s->output_lanes[0][0], 15.0f, 0.01f, "5*3 = 15 via value API");
    } else {
        check(false, "single-chain value-API op produced output");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
