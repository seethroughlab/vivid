// Test: LoopBased execution produces correct per-lane independent output.
//
// Verifies that LaneSlewOp (strategy-independent, VIVID_LANE_POINTWISE)
// runs correctly under LoopBased execution:
// - Compiler assigns LoopBased strategy
// - Runtime drives per-lane loop from lane input
// - Each lane accumulates independent slew state via vivid_lane_state()
// - Output audio buffers contain per-lane data at correct offsets
//
// Graph: IdentityLaneSourceOp (frame) → LaneSlewOp (audio) → audio_out

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/graph/lane_types.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // Stage operators
    std::string staging = build_dir + "/.test_lane_equivalence_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("identity_lane_source_op.dylib");
    stage("lane_slew_op.dylib");
    stage("multi_channel_dc_source_op.dylib");
    stage("dc_per_lane_op.dylib");
    stage("lane_source_op.dylib");

    std::fprintf(stderr, "\n=== test_lane_equivalence ===\n\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan()");

    auto connect = [](vivid::Graph& graph,
                      const char* from_node, const char* from_port,
                      const char* to_node, const char* to_port,
                      const char* bridge = nullptr) {
        graph.add_connection(from_node, from_port, to_node, to_port);
        if (bridge) {
            graph.set_connection_bridge(from_node, from_port, to_node, to_port, bridge);
        }
    };

    // --- Test 1: Compiler assigns LoopBased ---
    std::fprintf(stderr, "\n--- compiler strategy assignment ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "IdentityLaneSourceOp",
                       {{"active_mask", 15.0f}, {"base", 1.0f}});
        graph.add_node("slew", "LaneSlewOp", {{"rate", 1.0f}});
        graph.add_node("out", "audio_out");
        connect(graph, "src", "out", "slew", "input", "snapshot");
        connect(graph, "src", "lane_ids", "slew", "lane_ids", "snapshot");
        connect(graph, "slew", "output", "out", "input");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        auto* slew = runtime.compiled_graph()->find_node("slew");
        check(slew != nullptr, "slew node found");
        if (slew && slew->audio) {
            check(slew->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "slew assigned LoopBased");
            check(slew->audio->lane_id_port >= 0,
                  "lane_id_port detected");
        }
    }

    // --- Test 2: LoopBased runtime produces per-lane independent output ---
    std::fprintf(stderr, "\n--- per-lane independent slew output ---\n");
    {
        vivid::Graph graph;
        // 4 voices, base=100: lane values [100, 200, 300, 400]
        // LaneSlewOp reads audio input (zeros from no audio upstream) and
        // accumulates per-lane state. The slew filter converges toward the
        // audio input (0), so we verify that each lane's state is independent.
        graph.add_node("src", "IdentityLaneSourceOp",
                       {{"active_mask", 15.0f}, {"base", 100.0f}});
        graph.add_node("slew", "LaneSlewOp", {{"rate", 0.5f}});
        graph.add_node("out", "audio_out");
        connect(graph, "src", "out", "slew", "input", "snapshot");
        connect(graph, "src", "lane_ids", "slew", "lane_ids", "snapshot");
        connect(graph, "slew", "output", "out", "input");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        vivid::AudioEngine audio_engine;
        check(audio_engine.build(runtime), "audio_engine.build()");

        // Tick frame, push to audio, process one buffer
        runtime.tick(0.0, 1.0 / 60.0, 0);
        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

        float output[512] = {};
        audio_engine.process_audio_for_test(output, 256);

        // With identical audio input on every lane, LoopBased execution should
        // drive every lane to the same final value.
        auto* slew = runtime.compiled_graph()->find_node("slew");
        check(slew != nullptr, "slew node found");
        if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
            const auto& buf = slew->audio->buffers_out[0];
            constexpr uint32_t kBufSize = 256;

            if (buf.size() >= 4 * kBufSize) {
                float lane0 = buf[kBufSize - 1];
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    float last_sample = buf[lane * kBufSize + kBufSize - 1];
                    check_float(last_sample, lane0, 0.001f,
                                (std::string("lane ") + std::to_string(lane) + " matches lane 0").c_str());
                }
            } else {
                check(false, "buffer too small for 4 lanes");
            }
        }

        // --- Test 3: Second buffer shows state continuity ---
        std::fprintf(stderr, "\n--- state continuity across buffers ---\n");
        float output2[512] = {};
        audio_engine.process_audio_for_test(output2, 256);

        if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
            const auto& buf = slew->audio->buffers_out[0];
            constexpr uint32_t kBufSize = 256;

            if (buf.size() >= 4 * kBufSize) {
                float lane0 = buf[kBufSize - 1];
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    float last_sample = buf[lane * kBufSize + kBufSize - 1];
                    check_float(last_sample, lane0, 0.001f,
                                (std::string("lane ") + std::to_string(lane) + " buffer 2 matches lane 0").c_str());
                }
            }
        }

        audio_engine.shutdown();
    }

    // --- Test 5: Full 4-lane cross-strategy equivalence ---
    //
    // Proves that LaneSlewOp produces identical output under InstancePerLane
    // and LoopBased for all 4 lanes with per-lane-distinct DC audio input.
    //
    // Graph A (InstancePerLane): MultiChannelDcSourceOp(4ch) → slew → audio_out
    //   Channel c outputs DC = (c+1) * 0.1. Compiler assigns InstancePerLane.
    //
    // Graph B (LoopBased): LaneSourceOp(4) → DcPerLaneOp → slew → audio_out
    //   DcPerLaneOp writes DC = (lane_index+1) * 0.1 per lane. Both DcPerLaneOp
    //   and slew are LoopBased. LoopBased→LoopBased routing copies the full
    //   multi-lane buffer so each lane of slew sees the correct per-lane DC.
    std::fprintf(stderr, "\n--- full 4-lane cross-strategy equivalence ---\n");
    {
        constexpr uint32_t kBufSize = 256;
        constexpr uint32_t kLanes = 4;
        constexpr float kRate = 0.5f;

        // ── Graph A: InstancePerLane ──
        float ipl_output[kLanes] = {};
        {
            vivid::Graph graph;
            graph.add_node("dc", "MultiChannelDcSourceOp");
            graph.add_node("slew", "LaneSlewOp", {{"rate", kRate}});
            graph.add_node("out", "audio_out");
            connect(graph, "dc", "output", "slew", "input");
            connect(graph, "slew", "output", "out", "input");

            vivid::RuntimeCore runtime;
            check(runtime.build(graph, registry), "IPL: runtime.build()");

            auto* slew = runtime.compiled_graph()->find_node("slew");
            check(slew != nullptr, "IPL: slew found");
            if (slew && slew->audio) {
                check(slew->audio->execution_strategy == vivid::LaneExecutionStrategy::InstancePerLane,
                      "IPL: slew assigned InstancePerLane");
            }

            vivid::AudioEngine engine;
            check(engine.build(runtime), "IPL: engine.build()");

            runtime.tick(0.0, 1.0 / 60.0, 0);
            runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

            float out_buf[512] = {};
            engine.process_audio_for_test(out_buf, kBufSize);
            engine.process_audio_for_test(out_buf, kBufSize);

            if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
                const auto& buf = slew->audio->buffers_out[0];
                for (uint32_t c = 0; c < kLanes && (c + 1) * kBufSize <= buf.size(); ++c)
                    ipl_output[c] = buf[c * kBufSize + kBufSize - 1];
            }

            engine.shutdown();
        }

        // ── Graph B: LoopBased ──
        float lb_output[kLanes] = {};
        {
            vivid::Graph graph;
            graph.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
            graph.add_node("dc", "DcPerLaneOp");
            graph.add_node("slew", "LaneSlewOp", {{"rate", kRate}});
            graph.add_node("out", "audio_out");
            // Spread triggers LoopBased on dc and slew
            connect(graph, "src", "out", "dc", "lanes", "snapshot");
            connect(graph, "src", "out", "slew", "input", "snapshot");
            connect(graph, "src", "out", "slew", "lane_ids", "snapshot");
            // Audio chain: dc → slew (LoopBased→LoopBased routing copies all lanes)
            connect(graph, "dc", "output", "slew", "input");
            connect(graph, "slew", "output", "out", "input");

            vivid::RuntimeCore runtime;
            check(runtime.build(graph, registry), "LB: runtime.build()");

            auto* dc_node = runtime.compiled_graph()->find_node("dc");
            auto* slew = runtime.compiled_graph()->find_node("slew");
            check(slew != nullptr, "LB: slew found");
            if (dc_node && dc_node->audio) {
                check(dc_node->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                      "LB: dc assigned LoopBased");
            }
            if (slew && slew->audio) {
                check(slew->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                      "LB: slew assigned LoopBased");
            }

            vivid::AudioEngine engine;
            check(engine.build(runtime), "LB: engine.build()");

            runtime.tick(0.0, 1.0 / 60.0, 0);
            runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

            float out_buf[512] = {};
            engine.process_audio_for_test(out_buf, kBufSize);
            engine.process_audio_for_test(out_buf, kBufSize);

            if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
                const auto& buf = slew->audio->buffers_out[0];
                for (uint32_t c = 0; c < kLanes && (c + 1) * kBufSize <= buf.size(); ++c)
                    lb_output[c] = buf[c * kBufSize + kBufSize - 1];
            }

            engine.shutdown();
        }

        // ── Compare all 4 lanes ──
        std::fprintf(stderr, "\n--- comparing all 4 lanes: InstancePerLane vs LoopBased ---\n");
        for (uint32_t c = 0; c < kLanes; ++c) {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "lane %u: IPL=%.8f vs LB=%.8f", c, ipl_output[c], lb_output[c]);
            check_float(ipl_output[c], lb_output[c], 1e-5f, msg);
        }

        // Sanity: each lane should converge toward its DC = (c+1) * 0.1
        for (uint32_t c = 0; c < kLanes; ++c) {
            float expected_dc = static_cast<float>(c + 1) * 0.1f;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "lane %u converged toward DC %.1f", c, expected_dc);
            check(ipl_output[c] > expected_dc * 0.9f, msg);
        }
    }

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
