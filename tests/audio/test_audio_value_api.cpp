// Test: the AUDIO value-view API (lane-value clean-break, Phase 5a).
//
// AudioGainValueOp reads its audio input via ctx->values (VIVID_VALUE_AUDIO) and
// writes its output via ctx->value_outputs[0].resize — the value-model API — instead
// of input_buffers/output_buffers. It runs through the real (Scalar-path) audio
// executor, between buffer-API neighbors (MonoDcSourceOp → AudioGainValueOp →
// audio_out), proving the audio value API produces the same result as the buffer
// API and the executor populates the value views RT-safely. Offline (no device).

#include "runtime/operators/operator_registry.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include <cstdio>
#include <filesystem>
#include "test_helpers.h"

static const vivid::CompiledNode* find_node(const vivid::CompiledGraph* cg, const char* id) {
    for (const auto& n : cg->nodes) if (n.node_id == id) return &n;
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::string build_dir = (argc > 1) ? argv[1] : ".";

    std::string staging = build_dir + "/.test_audio_value_api_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name, dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("mono_dc_source_op.dylib");
    stage("audio_gain_value_op.dylib");
    stage("multi_channel_dc_source_op.dylib");

    std::fprintf(stderr, "\n=== Test: Audio Value-View API ===\n\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);            // audio_out
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- Scalar path: mono DC (0.5) → value-API gain (×2) → audio_out ---
    std::fprintf(stderr, "\n--- Scalar audio path (5a) ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src",  "MonoDcSourceOp",   {{"level", 0.5f}});
        graph.add_node("gain", "AudioGainValueOp", {{"gain", 2.0f}});
        graph.add_node("out",  "audio_out");
        graph.add_connection("src", "output", "gain", "input");
        graph.add_connection("gain", "output", "out", "input");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "scalar: runtime.build()");
        check_graph_clean(runtime.compiled_graph(), "audio value api scalar");

        vivid::AudioEngine audio_engine;
        check(audio_engine.build(runtime), "scalar: audio_engine.build()");
        runtime.tick(0.0, 1.0 / 60.0, 0);
        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());
        float output[512] = {};
        audio_engine.process_audio_for_test(output, 256);

        const auto* gain = find_node(runtime.compiled_graph(), "gain");
        check(gain != nullptr && gain->audio != nullptr, "gain node has audio state");
        if (gain && gain->audio && !gain->audio->buffers_out.empty()) {
            check(gain->audio->execution_strategy == vivid::LaneExecutionStrategy::Scalar,
                  "gain is on the Scalar audio path");
            const auto& buf = gain->audio->buffers_out[0];
            check(buf.size() >= 256, "gain output buffer has a block");
            if (buf.size() >= 256) {
                std::fprintf(stderr, "  gain out[0]=%.4f out[255]=%.4f (expect 1.0)\n", buf[0], buf[255]);
                check_float(buf[0],   1.0f, 0.001f, "value-API gain output[0] = 0.5*2 = 1.0");
                check_float(buf[255], 1.0f, 0.001f, "value-API gain output[255] = 1.0");
            }
        } else {
            check(false, "gain produced an audio output buffer");
        }
        audio_engine.shutdown();
    }

    // --- InstancePerLane (lifted) path: 4ch DC → value-API gain (×2, lifted) ---
    // MultiChannelDcSourceOp emits ch c = (c+1)*0.1; a mono gain is lifted to 4
    // instances, each reading its lane's block via ctx->values + writing via
    // ctx->value_outputs. Output buffer is laid out [lane0 block | lane1 block | ...].
    std::fprintf(stderr, "\n--- InstancePerLane audio path (5b) ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src",  "MultiChannelDcSourceOp");
        graph.add_node("gain", "AudioGainValueOp", {{"gain", 2.0f}});
        graph.add_node("out",  "audio_out");
        graph.add_connection("src", "output", "gain", "input");
        graph.add_connection("gain", "output", "out", "input");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "lifted: runtime.build()");
        check_graph_clean(runtime.compiled_graph(), "audio value api lifted");

        vivid::AudioEngine audio_engine;
        check(audio_engine.build(runtime), "lifted: audio_engine.build()");
        runtime.tick(0.0, 1.0 / 60.0, 0);
        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());
        float output[512] = {};
        audio_engine.process_audio_for_test(output, 256);

        const auto* gain = find_node(runtime.compiled_graph(), "gain");
        check(gain != nullptr && gain->audio != nullptr, "lifted gain has audio state");
        if (gain && gain->audio && !gain->audio->buffers_out.empty()) {
            check(gain->audio->execution_strategy == vivid::LaneExecutionStrategy::InstancePerLane,
                  "gain is on the InstancePerLane audio path");
            const auto& buf = gain->audio->buffers_out[0];
            constexpr uint32_t kBuf = 256;
            check(buf.size() >= 4 * kBuf, "lifted output holds 4 lanes");
            if (buf.size() >= 4 * kBuf) {
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    float expect = static_cast<float>(lane + 1) * 0.1f * 2.0f;  // (c+1)*0.1 × 2
                    float got = buf[lane * kBuf + kBuf - 1];
                    std::fprintf(stderr, "  lane %u: got %.4f expect %.4f\n", lane, got, expect);
                    check_float(got, expect,  0.001f,
                                (std::string("lane ") + std::to_string(lane) +
                                 " value-API output = (c+1)*0.1*2").c_str());
                }
            }
        } else {
            check(false, "lifted gain produced per-lane output");
        }
        audio_engine.shutdown();
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
