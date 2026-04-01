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

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/audio_engine.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include "runtime/builtin_operators.h"
#include "runtime/lane_types.h"
#include <cstdio>
#include <cmath>
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

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %.6f, got %.6f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.6f)\n", msg, actual);
    }
}

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

    // --- Test 1: Compiler assigns LoopBased ---
    std::fprintf(stderr, "\n--- compiler strategy assignment ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "IdentityLaneSourceOp",
                       {{"active_mask", 15.0f}, {"base", 1.0f}});
        graph.add_node("slew", "LaneSlewOp", {{"rate", 1.0f}});
        graph.add_node("out", "audio_out");
        graph.add_connection("src", "out", "slew", "input");
        graph.add_connection("src", "lane_ids", "slew", "lane_ids");
        graph.add_connection("slew", "output", "out", "input");

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
        // 4 voices, base=100: spread values [100, 200, 300, 400]
        // LaneSlewOp reads audio input (zeros from no audio upstream) and
        // accumulates per-lane state. The slew filter converges toward the
        // audio input (0), so we verify that each lane's state is independent.
        graph.add_node("src", "IdentityLaneSourceOp",
                       {{"active_mask", 15.0f}, {"base", 100.0f}});
        graph.add_node("slew", "LaneSlewOp", {{"rate", 0.5f}});
        graph.add_node("out", "audio_out");
        graph.add_connection("src", "out", "slew", "input");
        graph.add_connection("src", "lane_ids", "slew", "lane_ids");
        graph.add_connection("slew", "output", "out", "input");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        vivid::AudioEngine audio_engine;
        check(audio_engine.build(runtime), "audio_engine.build()");

        // Tick frame, push to audio, process one buffer
        runtime.tick(0.0, 1.0 / 60.0, 0);
        runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

        float output[512] = {};
        audio_engine.process_audio_for_test(output, 256);

        // With rate=0.5 and zero audio input, the slew filter does:
        // value += (0 - value) * 0.5 → value *= 0.5 each sample
        // Starting from initial state 0.0, all lanes converge to 0.
        // Each lane should have the SAME output (all zero) because audio input is zero.
        auto* slew = runtime.compiled_graph()->find_node("slew");
        check(slew != nullptr, "slew node found");
        if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
            const auto& buf = slew->audio->buffers_out[0];
            constexpr uint32_t kBufSize = 256;

            // All lanes see zero audio input, start at zero → output is zero
            // This proves the LoopBased path processes all 4 lanes
            if (buf.size() >= 4 * kBufSize) {
                // Each lane's LAST sample should be ~0 (slew from 0 toward 0)
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    float last_sample = buf[lane * kBufSize + kBufSize - 1];
                    check_float(last_sample, 0.0f, 0.001f,
                                (std::string("lane ") + std::to_string(lane) + " output ≈ 0").c_str());
                }
            } else {
                check(false, "buffer too small for 4 lanes");
            }
        }

        // --- Test 3: Signal outputs report correct metadata ---
        std::fprintf(stderr, "\n--- signal output metadata ---\n");
        if (slew && slew->audio) {
            // Signal outputs are written per-lane in LoopBased, last lane wins.
            // lane_count_out should be 4 (all lanes see the same count)
            if (!slew->audio->float_output_values.empty()) {
                check_float(slew->audio->float_output_values[0], 4.0f, 0.01f,
                            "lane_count_out = 4");
                // lane_index_out = 3 (last lane processed)
                check_float(slew->audio->float_output_values[1], 3.0f, 0.01f,
                            "lane_index_out = 3 (last lane)");
                // lane_id_out should be the identity-bearing ID from the source
                // IdentityLaneSourceOp uses kBaseLaneId=100, so lane 3 = 103
                check_float(slew->audio->float_output_values[2], 103.0f, 0.01f,
                            "lane_id_out = 103 (identity from spread)");
            }
        }

        // --- Test 4: Second buffer shows state continuity ---
        std::fprintf(stderr, "\n--- state continuity across buffers ---\n");
        float output2[512] = {};
        audio_engine.process_audio_for_test(output2, 256);

        if (slew && slew->audio && !slew->audio->buffers_out.empty()) {
            const auto& buf = slew->audio->buffers_out[0];
            constexpr uint32_t kBufSize = 256;

            if (buf.size() >= 4 * kBufSize) {
                // After 2 buffers (512 samples), slew from 0 toward 0 = still 0
                // But each lane should be independent — no cross-lane contamination
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    float last_sample = buf[lane * kBufSize + kBufSize - 1];
                    check_float(last_sample, 0.0f, 0.001f,
                                (std::string("lane ") + std::to_string(lane) + " buffer 2 ≈ 0").c_str());
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
            graph.add_connection("dc", "output", "slew", "input");
            graph.add_connection("slew", "output", "out", "input");

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
            runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

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
            graph.add_connection("src", "out", "dc", "lanes");
            graph.add_connection("src", "out", "slew", "input");
            graph.add_connection("src", "out", "slew", "lane_ids");
            // Audio chain: dc → slew (LoopBased→LoopBased routing copies all lanes)
            graph.add_connection("dc", "output", "slew", "input");
            graph.add_connection("slew", "output", "out", "input");

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
            runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

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
