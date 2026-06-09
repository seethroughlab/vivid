// Value-model stress suite (lane-value Phase 8d→8e).
//
// Sustained-load + churn coverage for the value substrate:
//   (a) identity churn      — thousands of lane_id allocate/get/retire/sweep
//                             cycles; LaneStateService frees retired state, no
//                             unbounded growth, allocate_lane_id stays monotonic.
//   (b) recompile stress    — rebuild a many-graph 50× with fresh RuntimeCores;
//                             provenance_group_id stays well-formed; no crash/leak.
//   (c) sustained overflow  — 100 push+process blocks of an oversized Many across
//                             the cadence bridge; lane_overflow_count is monotonic
//                             and observable; the clamp stays stable (no crash).
//
// (d) multiplicity_behavior hot-reload recompute is intentionally NOT a case here:
// driving an in-place descriptor change (same node, MAP→GENERATE) needs the
// versioned-dylib reload harness (test_hot_reload_stress). Its two halves are
// already covered — the change→recompile TRIGGER by test_hot_reload_classify
// (multiplicity_behavior change ⇒ RecompileRequired) and recompute-on-rebuild by
// test_value_flow_runtime + case (b) below.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_state.h"
#include "runtime/graph/lane_types.h"
#include "operator_api/value_model.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_value_stress_staging";
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
    stage("audio_lane_op.dylib");

    std::fprintf(stderr, "\n=== test_value_stress ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- (a) identity churn ---
    std::fprintf(stderr, "\n--- identity churn (5000 allocate/get/retire/sweep) ---\n");
    {
        vivid::LaneStateService svc;
        svc.set_node_capacity(1);
        constexpr int kChurn = 5000;
        constexpr int kSweepEvery = 64;
        uint32_t prev_id = 0;
        bool monotonic = true;
        uint32_t peak_live = 0;
        for (int i = 0; i < kChurn; ++i) {
            uint32_t id = svc.allocate_lane_id();
            if (i > 0 && id <= prev_id) monotonic = false;
            prev_id = id;
            svc.get(0, id, 16);                 // identity-stable storage (inserts entry)
            svc.retire(0, id);                  // mark for deferred cleanup
            if ((i + 1) % kSweepEvery == 0) svc.sweep_retired();
            uint32_t live = svc.live_entry_count(0);
            if (live > peak_live) peak_live = live;
        }
        svc.sweep_retired();
        check(monotonic, "churn: allocate_lane_id strictly monotonic");
        // Peak live entries is bounded by the sweep interval — never the churn count.
        check(peak_live <= 2 * kSweepEvery, "churn: live entries stay bounded (≤ 2× sweep interval)");
        check(svc.live_entry_count(0) == 0, "churn: all entries freed after final sweep");
        std::fprintf(stderr, "  (peak live entries: %u over %d cycles)\n", peak_live, kChurn);
    }

    // --- (b) recompile stress ---
    std::fprintf(stderr, "\n--- recompile stress (50 rebuilds of a many-graph) ---\n");
    {
        constexpr int kRebuilds = 50;
        int ok_builds = 0, many_ok = 0;
        for (int i = 0; i < kRebuilds; ++i) {
            vivid::Graph g;
            g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 256.0f}});
            g.add_node("op", "LaneFrameOp");
            g.add_node("sink", "LaneSinkOp");
            g.add_connection("src", "out", "op", "input");
            g.add_connection("op", "output", "sink", "in");
            vivid::RuntimeCore rt;
            if (!rt.build(g, registry)) continue;
            ok_builds++;
            auto* op = rt.compiled_graph()->find_node("op");
            if (op) {
                auto it = op->output_port_indices.find("output");
                if (it != op->output_port_indices.end() &&
                    it->second < op->output_value_envelopes.size()) {
                    const auto& e = op->output_value_envelopes[it->second];
                    if (e.multiplicity == VIVID_MULTIPLICITY_MANY && e.provenance_group_id > 1)
                        many_ok++;
                }
            }
        }
        check(ok_builds == kRebuilds, "recompile: all 50 rebuilds succeeded");
        check(many_ok == kRebuilds, "recompile: provenance well-formed (Many, group>1) every rebuild");
    }

    // --- (c) sustained bridge overflow ---
    std::fprintf(stderr, "\n--- sustained bridge overflow (100 blocks, count=2048 > 1024) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 2048.0f}});
        g.add_node("al", "AudioLaneOp");
        g.add_node("out", "audio_out");
        g.add_connection("src", "out", "al", "values");
        g.set_connection_bridge("src", "out", "al", "values", "snapshot");
        g.add_connection("al", "out", "out", "input");
        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "overflow: builds");
        vivid::AudioEngine eng;
        check(eng.build(rt), "overflow: engine builds");

        rt.tick(0.0, 1.0 / 60.0, 0);
        static float out[2048];
        bool monotonic = true;
        uint32_t prev = 0;
        for (int i = 0; i < 100; ++i) {
            rt.audio_frame_bridge().push_to_audio(*rt.compiled_graph());  // overflows → count++
            eng.process_audio_for_test(out, 256);
            uint32_t c = rt.audio_frame_bridge().lane_overflow_count();
            if (c < prev) monotonic = false;
            prev = c;
        }
        eng.shutdown();
        check(monotonic, "overflow: lane_overflow_count is monotonic non-decreasing");
        check(prev >= 100, "overflow: counter accumulated (≥1 per overflowing push)");
        std::fprintf(stderr, "  (final overflow count: %u)\n", prev);
    }

    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
