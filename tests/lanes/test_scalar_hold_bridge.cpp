#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/core/runtime_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    const std::string staging = build_dir + "/.test_scalar_hold_bridge_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
                               staging + "/test_op_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_scalar_probe_op.dylib",
                               staging + "/audio_scalar_probe_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: SCALAR Hold Bridge Contract ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load_from_string(R"({
        "nodes": {
            "ctrl":  { "type": "TestOp", "params": { "scale": 1.0 } },
            "probe": { "type": "AudioScalarProbeOp" }
        },
        "connections": [
            { "from": "ctrl/out", "to": "probe/held", "bridge": "hold" }
        ]
    })"), "graph.load_from_string()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(runtime), "audio_engine.build()");

    const int probe_idx = audio_engine.audio_node_index("probe");
    check(probe_idx >= 0, "probe node exists");

    runtime.tick(0.0, 0.016, 0);
    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

    if (probe_idx >= 0) {
        check_float(audio_engine.analysis_read().rms[probe_idx], 0.0f, 1e-5f,
                    "publishing hold data does not change audio analysis before callback");

        float output[vivid::AudioEngine::kBufferSize * 2] = {};
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);

        check_float(audio_engine.analysis_read().rms[probe_idx], 2.0f, 0.1f,
                    "audio callback applies the latest held scalar value");

        auto* ctrl = runtime.compiled_graph()->find_node("ctrl");
        check(ctrl != nullptr, "find ctrl node");
        if (ctrl) {
            auto it = ctrl->param_indices.find("scale");
            check(it != ctrl->param_indices.end(), "scale param exists");
            if (it != ctrl->param_indices.end()) {
                ctrl->param_values[it->second] = 4.5f;
            }
        }

        runtime.tick(0.016, 0.016, 1);
        std::fill(std::begin(output), std::end(output), 0.0f);
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
        check_float(audio_engine.analysis_read().rms[probe_idx], 2.0f, 0.1f,
                    "held scalar remains active until the next push_to_audio()");

        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());
        std::fill(std::begin(output), std::end(output), 0.0f);
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
        check_float(audio_engine.analysis_read().rms[probe_idx], 9.0f, 0.1f,
                    "audio analysis updates after a new held scalar snapshot is published");
    }

    audio_engine.shutdown();
    runtime.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
