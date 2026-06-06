// Test: the frame value-view API for the many-string payload (lane-value
// clean-break, Phase 4b).
//
// StringValueEchoOp consumes its STRING_LANES input via ctx->values (strings)
// and produces via ctx->value_outputs::set_string — the value-model API — instead
// of the string-lane views. It runs through real frame execution between
// string-lane-API neighbors (StringSourceOp → StringValueEchoOp → StringSinkOp),
// proving the value API round-trips many strings and interoperates with the
// string-lane transport.

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

    std::string staging = build_dir + "/.test_frame_value_api_string_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* dylib) {
        std::filesystem::copy_file(build_dir + "/" + dylib, staging + "/" + dylib,
            std::filesystem::copy_options::overwrite_existing);
    };
    stage("string_source_op.dylib");
    stage("string_value_echo_op.dylib");
    stage("string_sink_op.dylib");

    // StringSourceOp emits the list ["alpha","beta","gamma"] on its "list" port.
    std::string graph_path = build_dir + "/test_frame_value_api_string.json";
    {
        std::ofstream f(graph_path);
        f << R"({
    "nodes": {
        "src":  { "type": "StringSourceOp" },
        "echo": { "type": "StringValueEchoOp" },
        "sink": { "type": "StringSinkOp" }
    },
    "connections": [
        { "from": "src/list",  "to": "echo/in" },
        { "from": "echo/out",  "to": "sink/in_list" }
    ]
})";
    }

    std::fprintf(stderr, "\n=== Test: Frame Value-View API (many-string) ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    check_graph_clean(runtime.compiled_graph(), "frame value api string");

    runtime.tick(0.0, 1.0 / 60.0, 0);
    const auto* cg = runtime.compiled_graph();

    const char* expected[3] = {"alpha", "beta", "gamma"};

    // The value-API echo op produced the 3 strings via VividValueOutput::set_string.
    std::fprintf(stderr, "\n--- value-API op produced the many-string output ---\n");
    const auto* echo = find_node(cg, "echo");
    check(echo != nullptr, "found echo node");
    if (echo) {
        check(echo->output_string_lanes.size() > 0 && echo->output_string_lanes[0].size() == 3,
              "echo produced 3 strings via the value API");
        if (echo->output_string_lanes.size() > 0 && echo->output_string_lanes[0].size() == 3) {
            for (int i = 0; i < 3; ++i)
                check(echo->output_string_lanes[0][i] == expected[i],
                      (std::string("echo out[") + std::to_string(i) + "] = " + expected[i]).c_str());
        }
    }

    // The string-lane-API sink received them on its in_list input — interop holds.
    std::fprintf(stderr, "\n--- string-lane-API sink received the value-API output ---\n");
    const auto* sink = find_node(cg, "sink");
    check(sink != nullptr, "found sink node");
    if (sink) {
        // in_list is the sink's input port ordinal 1.
        check(sink->input_string_lanes.size() > 1 && sink->input_string_lanes[1].size() == 3,
              "sink received 3 strings via the string-lane transport");
        if (sink->input_string_lanes.size() > 1 && sink->input_string_lanes[1].size() == 3) {
            for (int i = 0; i < 3; ++i)
                check(sink->input_string_lanes[1][i] == expected[i],
                      (std::string("sink in[") + std::to_string(i) + "] = " + expected[i]).c_str());
        }
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
