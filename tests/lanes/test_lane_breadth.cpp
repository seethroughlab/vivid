// Test: lane model breadth proof cases (Phase D).
//
// Proves:
// 1. Large-count frame LoopBased lifting (256 and 512 lanes)
// 2. FFT-derived structural provenance with per-bin lane processing
//
// These are composition and scale tests — no new runtime machinery.

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

    std::string staging = build_dir + "/.test_lane_breadth_staging";
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
    stage("fft_analysis.dylib");
    stage("repeat.dylib");

    std::fprintf(stderr, "\n=== test_lane_breadth ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- Test 1: 256-lane frame LoopBased lifting ---
    std::fprintf(stderr, "\n--- large-count frame lifting: 256 lanes ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 256.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "op", "input");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        auto* op = runtime.compiled_graph()->find_node("op");
        if (op) {
            check(op->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "op assigned LoopBased");
        }

        // 2 ticks: each lane accumulates its lane value twice
        runtime.tick(0.0, 1.0 / 60.0, 0);
        runtime.tick(1.0 / 60.0, 1.0 / 60.0, 1);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 256, "output lane array has 256 elements");
            if (sp.size() == 256) {
                // Lane 0: base*1 = 1, accumulated over 2 ticks → 2
                check_float(sp[0], 2.0f, 0.01f, "lane 0: 1*2 ticks = 2");
                // Lane 127: base*128 = 128, accumulated over 2 ticks → 256
                check_float(sp[127], 256.0f, 0.01f, "lane 127: 128*2 = 256");
                // Lane 255: base*256 = 256, accumulated over 2 ticks → 512
                check_float(sp[255], 512.0f, 0.01f, "lane 255: 256*2 = 512");
            }
        }
    }

    // --- Test 2: 512-lane frame LoopBased lifting ---
    std::fprintf(stderr, "\n--- large-count frame lifting: 512 lanes ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 0.1f}, {"count", 512.0f}});
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
            check(sp.size() == 512, "output lane array has 512 elements");
            if (sp.size() >= 512) {
                // Lane 0: 0.1*1 = 0.1
                check_float(sp[0], 0.1f, 0.001f, "lane 0 = 0.1");
                // Lane 511: 0.1*512 = 51.2
                check_float(sp[511], 51.2f, 0.01f, "lane 511 = 51.2");
            }
        }
    }

    // --- Test 3: FFT-derived structural provenance with per-bin lifting ---
    // Chain: Repeat(1.0, count=1024) → FFTAnalysis(fft_size=1024, window=none) → LaneFrameOp → sink
    // Constant waveform of 1.0 across 1024 samples.
    // FFT of DC=1.0 with N=1024: bin 0 magnitude = 1.0 * 1024 * (2/1024) = 2.0, all others ≈ 0.
    // FFTAnalysis is STRUCTURAL → its spectrum lane array gets a fresh lane_set_id.
    // LaneFrameOp is kStrategyIndependent → compiler assigns LoopBased from FFT's lane set.
    // Per-bin accumulation proves each of 512 bins is lifted independently.
    std::fprintf(stderr, "\n--- FFT-derived per-bin lane processing ---\n");
    {
        vivid::Graph graph;
        // Produce constant waveform: LaneSourceOp(base=1, count=1) outputs scalar 1.0
        // Repeat(count=1024) broadcasts to a 1024-element lane array
        graph.add_node("scalar", "LaneSourceOp", {{"base", 1.0f}, {"count", 1.0f}});
        graph.add_node("wave", "Repeat", {{"count", 1024.0f}});
        graph.add_node("fft", "FFTAnalysis", {{"fft_size", 1024.0f}, {"window", 0.0f}});
        graph.add_node("op", "LaneFrameOp");
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("scalar", "out", "wave", "input");
        graph.add_connection("wave", "output", "fft", "waveform");
        graph.add_connection("fft", "spectrum", "op", "input");
        graph.add_connection("op", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");

        // Verify FFTAnalysis is STRUCTURAL
        auto* fft_node = runtime.compiled_graph()->find_node("fft");
        check(fft_node != nullptr, "fft node found");
        if (fft_node) {
            check(fft_node->lane_behavior == vivid::LaneBehavior::Structural,
                  "FFTAnalysis is Structural");
            if (!fft_node->output_lane_sets.empty()) {
                check(fft_node->output_lane_sets[0].lane_set_id > 0,
                      "FFT output has non-scalar lane_set_id");
            }
        }

        // Verify LaneFrameOp gets LoopBased from FFT provenance
        auto* op = runtime.compiled_graph()->find_node("op");
        if (op) {
            check(op->frame_execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
                  "LaneFrameOp assigned LoopBased from FFT provenance");
        }

        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 512, "output lane array has 512 bins (N/2)");
            if (sp.size() >= 2) {
                // Bin 0 (DC): magnitude ≈ 2.0 (constant 1.0 * N * 2/N)
                // LaneFrameOp accumulates, so after 1 tick: output = 2.0
                check_float(sp[0], 2.0f, 0.05f, "bin 0 (DC) ≈ 2.0");
                // Bin 1: near zero (no spectral leakage with rectangular window on DC)
                check(sp[1] < 0.01f, "bin 1 ≈ 0 (no leakage)");
                // Last bin: also near zero
                if (sp.size() == 512)
                    check(sp[511] < 0.01f, "bin 511 ≈ 0");
            }
        }

        // Run a second tick to prove per-bin state accumulation
        runtime.tick(1.0 / 60.0, 1.0 / 60.0, 1);
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            if (sp.size() >= 1) {
                // Bin 0 accumulated twice: 2.0 + 2.0 = 4.0
                check_float(sp[0], 4.0f, 0.1f, "bin 0 after 2 ticks ≈ 4.0");
            }
        }
    }

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
