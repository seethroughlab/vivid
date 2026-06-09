// Value-model graph-level performance benchmark (lane-value Phase 8d).
//
// Times the four graph shapes the clean-break plan names — scalar control, many-
// valued frame (256/512 lanes), audio lifted, bridge-heavy — and emits a single
// machine-readable JSON object of mean microseconds-per-block to stdout (warnings
// go to stderr, so stdout is pure JSON for tools/bench_regression.py to parse).
//
// The pre-removal lane-era baseline is no longer capturable (legacy merged away);
// this establishes a FORWARD reference on the current machine. Run via
// `uv run tools/bench_regression.py`, which compares to value_graphs_baseline.json.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kBlocks  = 500;   // timed iterations per repeat
constexpr int kRepeats = 8;
constexpr int kWarmup  = 16;
volatile double g_sink = 0.0;

// Mean microseconds per block over kRepeats, each repeat timing kBlocks calls.
template <typename Fn>
double measure(Fn&& block) {
    for (int i = 0; i < kWarmup; ++i) block();
    double sum_us = 0.0;
    for (int r = 0; r < kRepeats; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocks; ++i) block();
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        sum_us += ns / 1000.0 / (double)kBlocks;
    }
    return sum_us / (double)kRepeats;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.bench_value_graphs_staging";
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
    stage("lane_slew_op.dylib");
    stage("audio_lane_op.dylib");

    vivid::OperatorRegistry registry;
    if (!registry.scan(staging.c_str())) {
        std::fprintf(stderr, "bench_value_graphs: registry.scan failed\n");
        return 1;
    }

    double scalar_us = 0, many256_us = 0, many512_us = 0, audio_us = 0, bridge_us = 0;
    uint64_t frame = 0;

    // --- scalar control: 2-hop scalar chain ---
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 1.0f}});
        g.add_node("m1", "LaneSinkOp");
        g.add_node("m2", "LaneSinkOp");
        g.add_connection("src", "out", "m1", "in");
        g.add_connection("m1", "out", "m2", "in");
        vivid::RuntimeCore rt;
        if (rt.build(g, registry))
            scalar_us = measure([&]{ rt.tick(0.0, 1.0/60.0, frame++); g_sink += frame; });
    }
    // --- many-valued frame: LoopBased per-element loop ---
    auto many_case = [&](int count) -> double {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", (float)count}});
        g.add_node("op", "LaneFrameOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("src", "out", "op", "input");
        g.add_connection("op", "output", "sink", "in");
        vivid::RuntimeCore rt;
        if (!rt.build(g, registry)) return 0.0;
        return measure([&]{ rt.tick(0.0, 1.0/60.0, frame++); g_sink += frame; });
    };
    many256_us = many_case(256);
    many512_us = many_case(512);

    // --- audio lifted: LoopBased lane lift through the audio callback ---
    {
        vivid::Graph g;
        g.add_node("src", "IdentityLaneSourceOp", {{"active_mask", 15.0f}, {"base", 100.0f}});
        g.add_node("slew", "LaneSlewOp", {{"rate", 0.5f}});
        g.add_node("out", "audio_out");
        g.add_connection("src", "out", "slew", "input");
        g.set_connection_bridge("src", "out", "slew", "input", "snapshot");
        g.add_connection("src", "lane_ids", "slew", "lane_ids");
        g.set_connection_bridge("src", "lane_ids", "slew", "lane_ids", "snapshot");
        g.add_connection("slew", "output", "out", "input");
        vivid::RuntimeCore rt;
        if (rt.build(g, registry)) {
            vivid::AudioEngine eng;
            if (eng.build(rt)) {
                rt.tick(0.0, 1.0/60.0, frame++);
                rt.audio_frame_bridge().push_to_audio(*rt.compiled_graph());
                static float out[2048];
                audio_us = measure([&]{ eng.process_audio_for_test(out, 256); });
            }
            eng.shutdown();
        }
    }

    // --- bridge-heavy: 512-element Many across the cadence bridge into audio ---
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 512.0f}});
        g.add_node("al", "AudioLaneOp");
        g.add_node("out", "audio_out");
        g.add_connection("src", "out", "al", "values");
        g.set_connection_bridge("src", "out", "al", "values", "snapshot");
        g.add_connection("al", "out", "out", "input");
        vivid::RuntimeCore rt;
        if (rt.build(g, registry)) {
            vivid::AudioEngine eng;
            if (eng.build(rt)) {
                rt.tick(0.0, 1.0/60.0, frame++);
                rt.audio_frame_bridge().push_to_audio(*rt.compiled_graph());
                static float out[2048];
                bridge_us = measure([&]{ eng.process_audio_for_test(out, 256); });
            }
            eng.shutdown();
        }
    }

    std::filesystem::remove_all(staging);

    // Single-line JSON to stdout (stderr carries any warnings).
    std::printf("{\"scalar_us\": %.4f, \"many256_us\": %.4f, \"many512_us\": %.4f, "
                "\"audio_lifted_us\": %.4f, \"bridge_heavy_us\": %.4f}\n",
                scalar_us, many256_us, many512_us, audio_us, bridge_us);
    return 0;
}
