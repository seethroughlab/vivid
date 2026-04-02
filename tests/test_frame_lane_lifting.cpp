// Test: frame-domain per-lane lifting (Phase C).
//
// Verifies that frame-rate operators with kStrategyIndependent are
// evaluated per-lane when receiving structural lane input:
// 1. Compiler assigns LoopBased frame execution strategy
// 2. Frame executor drives per-lane loop, extracting per-lane input from lane arrays
// 3. Per-lane output is written back to the output lane array
// 4. Per-lane state via vivid_lane_state() persists across ticks

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/compiled_graph.h"
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
        std::fprintf(stderr, "  FAIL: %s (expected %.2f, got %.2f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.2f)\n", msg, actual);
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_frame_lane_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("lane_sink_op.dylib");
    stage("lane_frame_op.dylib");
    stage("identity_lane_source_op.dylib");
    stage("repeat.dylib");
    stage("envelope_fr.dylib");
    stage("lfo_fr.dylib");

    std::fprintf(stderr, "\n=== test_frame_lane_lifting ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- Test 1: Compiler assigns LoopBased ---
    std::fprintf(stderr, "\n--- compiler assigns LoopBased for frame node ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 10.0f}, {"count", 4.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "op", "input");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        auto* op = runtime.compiled_graph()->find_node("op");
        check(op != nullptr, "op node found");
        if (op) {
            check(op->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "op assigned LoopBased frame strategy");
        }
    }

    // --- Test 2: Per-lane output ---
    // LaneSourceOp(base=10, count=4) → lane array [10, 20, 30, 40]
    // LaneFrameOp accumulates: after tick 1, each lane's accumulated = input value
    // Output lane array should be [10, 20, 30, 40]
    std::fprintf(stderr, "\n--- per-lane output from LoopBased frame ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 10.0f}, {"count", 4.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "op", "input");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "output lane array has 4 elements");
            if (sp.size() == 4) {
                check_float(sp[0], 10.0f, 0.01f, "lane 0 output = 10");
                check_float(sp[1], 20.0f, 0.01f, "lane 1 output = 20");
                check_float(sp[2], 30.0f, 0.01f, "lane 2 output = 30");
                check_float(sp[3], 40.0f, 0.01f, "lane 3 output = 40");
            }
        }
    }

    // --- Test 3: Per-lane state persistence across ticks ---
    // LaneFrameOp accumulates input_values each tick.
    // After 3 ticks with [10, 20, 30, 40], each lane accumulates:
    //   lane 0: 10 + 10 + 10 = 30
    //   lane 1: 20 + 20 + 20 = 60
    //   lane 2: 30 + 30 + 30 = 90
    //   lane 3: 40 + 40 + 40 = 120
    std::fprintf(stderr, "\n--- per-lane state persists across ticks ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 10.0f}, {"count", 4.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "op", "input");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        // Run 3 ticks
        for (int t = 0; t < 3; ++t)
            runtime.tick(t * (1.0 / 60.0), 1.0 / 60.0, t);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "output lane array has 4 elements");
            if (sp.size() == 4) {
                check_float(sp[0],  30.0f, 0.01f, "lane 0 accumulated = 30 (10*3)");
                check_float(sp[1],  60.0f, 0.01f, "lane 1 accumulated = 60 (20*3)");
                check_float(sp[2],  90.0f, 0.01f, "lane 2 accumulated = 90 (30*3)");
                check_float(sp[3], 120.0f, 0.01f, "lane 3 accumulated = 120 (40*3)");
            }
        }
    }

    // --- Test 4: Identity-bearing lane_ids on frame path ---
    // IdentityLaneSourceOp emits explicit lane_ids (100, 101, 102, 103).
    // LaneFrameOp should see these via ctx->lane_id, not positional (1,2,3,4).
    // Verify by accumulating across 2 ticks then compacting (remove voice 1).
    // State must follow lane_id, not position — same proof as audio compaction test.
    std::fprintf(stderr, "\n--- identity-bearing lane_ids on frame path ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "IdentityLaneSourceOp",
                       {{"active_mask", 15.0f}, {"base", 10.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "op", "input");
        graph.add_connection("src", "lane_ids", "op", "lane_ids");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        auto* op_node = runtime.compiled_graph()->find_node("op");
        check(op_node != nullptr, "op node found");
        if (op_node) {
            check(op_node->frame_lane_id_port >= 0,
                  "frame_lane_id_port detected");
        }

        // Tick 1: 4 voices [10, 20, 30, 40] with lane_ids [100, 101, 102, 103]
        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "tick 1: 4-element output");
            if (sp.size() == 4) {
                check_float(sp[0], 10.0f, 0.01f, "tick 1 lane 0 = 10");
                check_float(sp[1], 20.0f, 0.01f, "tick 1 lane 1 = 20");
                check_float(sp[2], 30.0f, 0.01f, "tick 1 lane 2 = 30");
                check_float(sp[3], 40.0f, 0.01f, "tick 1 lane 3 = 40");
            }
        }

        // Change active_mask: 0xF → 0xD (remove voice 1, keep 0,2,3)
        // Compacted: values=[10, 30, 40], lane_ids=[100, 102, 103]
        auto* src_node = runtime.compiled_graph()->find_node("src");
        if (src_node) {
            auto it = src_node->param_indices.find("active_mask");
            if (it != src_node->param_indices.end())
                src_node->param_values[it->second] = 13.0f;  // 0xD
        }

        // Tick 2: 3 voices, state should follow lane_id
        runtime.tick(1.0 / 60.0, 1.0 / 60.0, 1);

        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 3, "tick 2: 3-element output (compacted)");
            if (sp.size() == 3) {
                // Position 0 (lane_id=100): was 10, now adds 10 → 20
                check_float(sp[0], 20.0f, 0.01f,
                            "pos 0 (id=100): 20 (10+10)");
                // Position 1 (lane_id=102): was 30, now adds 30 → 60
                // If positional IDs were used, this would be 20+30=50 (wrong)
                check_float(sp[1], 60.0f, 0.01f,
                            "pos 1 (id=102): 60 (30+30), NOT 50");
                // Position 2 (lane_id=103): was 40, now adds 40 → 80
                check_float(sp[2], 80.0f, 0.01f,
                            "pos 2 (id=103): 80 (40+40), NOT 70");
            }
        }
    }

    // --- Test 5: Envelope frame LoopBased parity across identical lanes ---
    // Repeat(input=1.0, count=4) → Envelope(gate) → LaneSinkOp
    // All 4 lanes receive the same gate=1.0 input. Under correct LoopBased
    // lifting with per-lane state, all lanes must produce identical envelope values.
    // The old shared frame_state_ bug caused lane N to advance the ADSR N extra
    // times per tick (cross-lane contamination).
    std::fprintf(stderr, "\n--- Envelope frame LoopBased parity across identical lanes ---\n");
    {
        vivid::Graph graph;
        graph.add_node("rep", "Repeat", {{"count", 4.0f}});
        graph.add_node("env", "EnvelopeFr", {{"attack", 0.01f}, {"decay", 0.1f},
                                            {"sustain", 0.7f}, {"release", 0.3f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("rep", "output", "env", "gate");
        graph.add_connection("env", "value", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build() [Envelope parity]");

        auto* env_node = runtime.compiled_graph()->find_node("env");
        check(env_node != nullptr, "env node found");
        if (env_node) {
            check(env_node->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "Envelope assigned LoopBased frame strategy");
        }

        auto* rep_node = runtime.compiled_graph()->find_node("rep");

        // Tick 10 frames and verify all lanes are equal on each tick
        bool parity_ok = true;
        for (int t = 0; t < 10; ++t) {
            // Drive Repeat's input port to 1.0 (gate high) via bridge values.
            // bridge_input_dirty is consumed each frame, so re-set every tick.
            if (rep_node && !rep_node->bridge_input_values.empty()) {
                rep_node->bridge_input_values[0] = 1.0f;
                rep_node->bridge_input_dirty[0] = 1;
            }
            runtime.tick(t * (1.0 / 60.0), 1.0 / 60.0, t);

            auto* sink = runtime.compiled_graph()->find_node("sink");
            if (sink && !sink->output_lanes.empty()) {
                const auto& sp = sink->output_lanes[0];
                if (sp.size() == 4) {
                    for (int i = 1; i < 4; ++i) {
                        if (std::fabs(sp[i] - sp[0]) > 0.001f) {
                            std::fprintf(stderr, "  FAIL: tick %d lane %d (%.4f) != lane 0 (%.4f)\n",
                                         t, i, sp[i], sp[0]);
                            parity_ok = false;
                        }
                    }
                }
            }
        }
        check(parity_ok, "all Envelope lanes equal across 10 ticks");

        // Verify envelope is actually doing something (not stuck at 0)
        auto* sink_final = runtime.compiled_graph()->find_node("sink");
        if (sink_final && !sink_final->output_lanes.empty() && !sink_final->output_lanes[0].empty()) {
            check(sink_final->output_lanes[0][0] > 0.001f,
                  "Envelope output is non-zero (envelope is active)");
        }
    }

    // --- Test 6: LFO frame LoopBased parity across identical lanes ---
    // Repeat(input=0.0, count=4) → LFO(gate) → LaneSinkOp
    // All 4 lanes receive the same input. Under correct LoopBased lifting,
    // all lanes must produce identical LFO values (free-running sine).
    // The old shared frame_state_ bug caused free_phase to advance 4x per tick.
    std::fprintf(stderr, "\n--- LFO frame LoopBased parity across identical lanes ---\n");
    {
        vivid::Graph graph;
        graph.add_node("rep", "Repeat", {{"count", 4.0f}});
        graph.add_node("lfo", "LfoFr", {{"frequency", 1.0f}, {"amplitude", 1.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("rep", "output", "lfo", "gate");
        graph.add_connection("lfo", "value", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build() [LFO parity]");

        auto* lfo_node = runtime.compiled_graph()->find_node("lfo");
        check(lfo_node != nullptr, "lfo node found");
        if (lfo_node) {
            check(lfo_node->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "LFO assigned LoopBased frame strategy");
        }

        bool parity_ok = true;
        for (int t = 0; t < 10; ++t) {
            runtime.tick(t * (1.0 / 60.0), 1.0 / 60.0, t);

            auto* sink = runtime.compiled_graph()->find_node("sink");
            if (sink && !sink->output_lanes.empty()) {
                const auto& sp = sink->output_lanes[0];
                if (sp.size() == 4) {
                    for (int i = 1; i < 4; ++i) {
                        if (std::fabs(sp[i] - sp[0]) > 0.001f) {
                            std::fprintf(stderr, "  FAIL: tick %d lane %d (%.4f) != lane 0 (%.4f)\n",
                                         t, i, sp[i], sp[0]);
                            parity_ok = false;
                        }
                    }
                }
            }
        }
        check(parity_ok, "all LFO lanes equal across 10 ticks");
    }

    // --- Test 7: Scalar fallback unchanged ---
    // Envelope and LFO with no lane-array inputs should NOT get LoopBased
    // and should still produce output normally.
    std::fprintf(stderr, "\n--- scalar fallback unchanged ---\n");
    {
        // Envelope scalar
        {
            vivid::Graph graph;
            graph.add_node("env", "EnvelopeFr", {{"attack", 0.01f}});
            graph.add_node("sink", "LaneSinkOp");
            graph.add_connection("env", "value", "sink", "in");

            vivid::RuntimeCore runtime;
            check(runtime.build(graph, registry), "runtime.build() [Envelope scalar]");

            auto* env_node = runtime.compiled_graph()->find_node("env");
            if (env_node) {
                check(env_node->frame_execution_strategy != vivid::LaneExecutionStrategy::LoopBased,
                      "scalar Envelope is NOT LoopBased");
            }

            runtime.tick(0.0, 1.0 / 60.0, 0);
            // Just verify no crash — scalar envelope with no gate stays at 0 (IDLE)
            check(true, "scalar Envelope ticked without crash");
        }

        // LFO scalar
        {
            vivid::Graph graph;
            graph.add_node("lfo", "LfoFr", {{"frequency", 1.0f}, {"amplitude", 1.0f}});
            graph.add_node("sink", "LaneSinkOp");
            graph.add_connection("lfo", "value", "sink", "in");

            vivid::RuntimeCore runtime;
            check(runtime.build(graph, registry), "runtime.build() [LFO scalar]");

            auto* lfo_node = runtime.compiled_graph()->find_node("lfo");
            if (lfo_node) {
                check(lfo_node->frame_execution_strategy != vivid::LaneExecutionStrategy::LoopBased,
                      "scalar LFO is NOT LoopBased");
            }

            runtime.tick(0.0, 1.0 / 60.0, 0);
            runtime.tick(1.0 / 60.0, 1.0 / 60.0, 1);
            check(true, "scalar LFO ticked without crash");
        }
    }

    // --- Test 8: Envelope output contains ADSR values, not input passthrough ---
    // LaneSourceOp(base=60, count=4) → Envelope(gate) → LaneSinkOp
    // Input lanes are [60, 120, 180, 240] (mimicking MIDI notes).
    // All are > 0.5 so treated as "gate on". The Envelope output should be
    // ADSR values in the 0-to-amplitude range (default amplitude=1.0),
    // NOT the input values (60, 120, ...). This catches the passthrough bug
    // where output_lanes gets input data instead of computed output.
    std::fprintf(stderr, "\n--- Envelope output is ADSR, not input passthrough ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 60.0f}, {"count", 4.0f}});
        graph.add_node("env", "EnvelopeFr", {{"attack", 0.01f}, {"decay", 0.1f},
                                            {"sustain", 0.7f}, {"release", 0.3f},
                                            {"amplitude", 1.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "env", "gate");
        graph.add_connection("env", "value", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build() [Envelope passthrough]");

        auto* env_node = runtime.compiled_graph()->find_node("env");
        check(env_node != nullptr, "env node found");
        if (env_node) {
            check(env_node->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "Envelope assigned LoopBased");
        }

        // Tick several frames for ADSR to advance
        for (int t = 0; t < 10; ++t)
            runtime.tick(t * (1.0 / 60.0), 1.0 / 60.0, t);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 4, "output has 4 lanes");
            if (sp.size() == 4) {
                // ADSR output must be in [0, amplitude]. With amplitude=1.0,
                // values must be <= 1.0. Input values are 60+ so any value > 2.0
                // proves passthrough rather than computed ADSR.
                bool passthrough_detected = false;
                for (int i = 0; i < 4; ++i) {
                    std::fprintf(stderr, "    lane %d = %.4f\n", i, sp[i]);
                    if (sp[i] > 2.0f) {
                        std::fprintf(stderr, "  FAIL: lane %d value %.2f >> amplitude — input passthrough!\n", i, sp[i]);
                        passthrough_detected = true;
                    }
                }
                check(!passthrough_detected, "no input passthrough in Envelope output");
                // Also verify envelope is non-zero (gate is on, ADSR should be active)
                check(sp[0] > 0.001f, "Envelope output is non-zero (ADSR active)");
            }
        }
    }

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
