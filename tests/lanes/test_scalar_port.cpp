#include "operator_api/types.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/core/runtime_core.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

static void test_port_type_compat() {
    std::fprintf(stderr, "\n--- SCALAR port compatibility ---\n");

    check(vivid_is_control_type(VIVID_PORT_SCALAR), "SCALAR is a control-domain type");
    check(vivid_port_type_compatible(VIVID_PORT_SCALAR, VIVID_PORT_SCALAR),
          "SCALAR ↔ SCALAR compatible");
    check(!vivid_port_type_compatible(VIVID_PORT_SCALAR, VIVID_PORT_AUDIO_BUFFER),
          "SCALAR ↔ AUDIO_BUFFER incompatible");
    check(!vivid_port_type_compatible(VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_SCALAR),
          "AUDIO_BUFFER ↔ SCALAR incompatible");
    check(!vivid_port_type_compatible(VIVID_PORT_SCALAR, VIVID_PORT_TEXTURE),
          "SCALAR ↔ TEXTURE incompatible");
    check(!vivid_port_type_compatible(VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_LANE_ARRAY),
          "AUDIO_BUFFER ↔ LANE_ARRAY incompatible");
}

static void test_audio_scalar_routing(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- audio-rate SCALAR routing ---\n");

    const std::string staging = build_dir + "/.test_scalar_port_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) -> bool {
        const std::string src = build_dir + "/" + name;
        if (!std::filesystem::exists(src)) {
            std::fprintf(stderr, "  SKIP: %s not found\n", name);
            return false;
        }
        std::filesystem::copy_file(src, staging + "/" + name,
                                   std::filesystem::copy_options::overwrite_existing);
        return true;
    };

    if (!stage("lfo_au.dylib") || !stage("audio_scalar_probe_op.dylib")) {
        std::filesystem::remove_all(staging);
        return;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load_from_string(R"({
        "nodes": {
            "lfo":   { "type": "Lfo", "params": { "frequency": 10.0, "amplitude": 1.0, "waveform": 0.0 } },
            "probe": { "type": "AudioScalarProbeOp" }
        },
        "connections": [
            { "from": "lfo/value", "to": "probe/value" }
        ]
    })"), "graph.load_from_string()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    bool found_direct = false;
    for (const auto& edge : runtime.compiled_graph()->edges) {
        if (runtime.compiled_graph()->nodes[edge.from_node].node_id == "lfo" &&
            runtime.compiled_graph()->nodes[edge.to_node].node_id == "probe") {
            found_direct = (edge.transport == vivid::EdgeTransport::Direct);
            break;
        }
    }
    check(found_direct, "audio-rate SCALAR connection stays a direct edge");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(runtime), "audio_engine.build()");

    const int probe_idx = audio_engine.audio_node_index("probe");
    check(probe_idx >= 0, "probe found in audio engine");

    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());
    const uint32_t audio_frames = audio_engine.buffer_size();
    std::vector<float> output(audio_frames * 2, 0.0f);
    for (int i = 0; i < 4; ++i) {
        audio_engine.process_audio_for_test(output.data(), audio_frames);
    }

    if (probe_idx >= 0) {
        const auto& analysis = audio_engine.analysis_read();
        check(analysis.peak[probe_idx][0] > 0.001f,
              "audio probe emits non-zero output from routed SCALAR input");
        check(analysis.rms[probe_idx][0] > 0.001f,
              "analysis reports non-zero output for routed SCALAR input");
    }

    audio_engine.shutdown();
    runtime.shutdown();
    std::filesystem::remove_all(staging);
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "\n=== Test: SCALAR Port ===\n");

    test_port_type_compat();
    test_audio_scalar_routing(build_dir);

    std::fprintf(stderr, "\n%s (%d failure%s)\n\n",
                 failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
