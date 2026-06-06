// Test: the frame scalar-float value path (lane-value clean-break, Phase 5c).
//
// ScalarValueGainOp reads its scalar input ONLY via ctx->values and writes ONLY
// via ctx->value_outputs (no input_values/output_values). Between scalar neighbors
// (ScalarSourceOp → ScalarValueGainOp → ControlPassOp), it must still carry the
// value: the input view aliases the scalar input_values (no lane), and the value
// output reaches the downstream scalar consumer. This is the Phase-6 prerequisite —
// the value API fully carrying the frame scalar-float path.

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

    std::string staging = build_dir + "/.test_frame_value_scalar_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* d) {
        std::filesystem::copy_file(build_dir + "/" + d, staging + "/" + d,
            std::filesystem::copy_options::overwrite_existing);
    };
    stage("scalar_source_op.dylib");
    stage("scalar_value_gain_op.dylib");
    stage("control_pass_op.dylib");

    std::string graph_path = build_dir + "/test_frame_value_scalar.json";
    {
        std::ofstream f(graph_path);
        f << R"({
    "nodes": {
        "src":  { "type": "ScalarSourceOp",    "params": { "value": 3.0 } },
        "gain": { "type": "ScalarValueGainOp", "params": { "gain": 2.0 } },
        "sink": { "type": "ControlPassOp",     "params": { "gain": 1.0 } }
    },
    "connections": [
        { "from": "src/out",  "to": "gain/in" },
        { "from": "gain/out", "to": "sink/in" }
    ]
})";
    }

    std::fprintf(stderr, "\n=== Test: Frame Scalar-Float Value Path ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    check_graph_clean(runtime.compiled_graph(), "frame value scalar");

    runtime.tick(0.0, 1.0 / 60.0, 0);
    const auto* cg = runtime.compiled_graph();

    // ScalarValueGainOp (pure value API) read 3 via ctx->values, wrote 6 via
    // ctx->value_outputs; ControlPassOp (×1) received 6 on its scalar input.
    const auto* sink = find_node(cg, "sink");
    check(sink != nullptr, "found sink node");
    if (sink && !sink->output_values.empty()) {
        std::fprintf(stderr, "  sink output[0] = %.4f (expect 6.0)\n", sink->output_values[0]);
        check_float(sink->output_values[0], 6.0f, 0.001f,
                    "scalar value path: 3 -> x2 (pure value API) -> sink = 6");
    } else {
        check(false, "sink produced a scalar output");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
