// Test: frame-domain per-lane lifting (Phase C).
//
// Verifies that frame-rate operators with kStrategyIndependent are
// evaluated per-lane when receiving structural lane input:
// 1. Compiler assigns LoopBased frame execution strategy
// 2. Frame executor drives per-lane loop, extracting per-lane input from lane arrays
// 3. Per-lane output is written back to the output lane array
// 4. Per-lane state via vivid_lane_state() persists across ticks

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_types.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

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
    stage("envelope.dylib");
    stage("lfo.dylib");

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

    // Tests 5–8 (Envelope and LFO frame-rate LoopBased parity / passthrough)
    // were retired in the operator-naming consolidation that moved Envelope
    // and LFO to audio-rate. The frame_execution_strategy assertions no
    // longer apply — these operators run on the audio thread and their
    // multi-lane handling is exercised by the audio executor. Cross-cadence
    // lane behavior is covered by the integration tests in tests/lanes/
    // (test_compute_lane_equivalence, test_lane_propagation, etc.) and by
    // operator-specific suites. The frame-rate generic lane-lifting logic
    // remains covered by Tests 1–4 above using test_op_v1 / test_op_v2.

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
