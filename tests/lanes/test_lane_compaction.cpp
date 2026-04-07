// Test: end-to-end identity compaction.
//
// Proves that vivid_lane_state() keyed by lane_id survives compaction
// correctly. When a structural operator removes a voice and compacts
// its lane array, downstream operators must retain state for surviving
// lane_ids — state must NOT follow positional index.
//
// Graph: IdentityLaneSourceOp (frame) → LaneStateTrackerOp (audio)
//
// Phase 1: 4 voices with distinct lane values. Each lane accumulates
//          its value into per-lane state keyed by lane_id.
// Phase 2: Voice 1 (second voice) is removed; the lane array compacts to 3.
//          Verify that surviving lanes retained correct accumulated state.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/operators/builtin_operators.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // Stage operators
    std::string staging = build_dir + "/.test_lane_compaction_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("identity_lane_source_op.dylib");
    stage("lane_state_tracker_op.dylib");

    std::fprintf(stderr, "\n=== test_lane_compaction ===\n\n");

    // --- Build graph programmatically ---
    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    // active_mask=0xF: voices 0,1,2,3 active. base=10.0: values=[10,20,30,40]
    graph.add_node("src", "IdentityLaneSourceOp",
                   {{"active_mask", 15.0f}, {"base", 10.0f}});
    graph.add_node("tracker", "LaneStateTrackerOp");
    graph.add_node("out", "audio_out");

    // Wire lane arrays: src.out → tracker.values, src.lane_ids → tracker.lane_ids
    // Explicit bridge required for frame→audio cross-cadence connections.
    graph.add_connection("src", "out", "tracker", "values");
    graph.set_connection_bridge("src", "out", "tracker", "values", "snapshot");
    graph.add_connection("src", "lane_ids", "tracker", "lane_ids");
    graph.set_connection_bridge("src", "lane_ids", "tracker", "lane_ids", "snapshot");
    // Audio output (so audio executor has a sink)
    graph.add_connection("tracker", "output", "out", "input");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    // Verify tracker is LoopBased
    auto* src_node = runtime.compiled_graph()->find_node("src");
    auto* tracker_node = runtime.compiled_graph()->find_node("tracker");
    check(tracker_node != nullptr, "tracker node found");
    if (tracker_node && tracker_node->audio) {
        check(tracker_node->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
              "tracker assigned LoopBased");
        check(tracker_node->audio->lane_id_port >= 0,
              "tracker has lane_id_port set");
    }

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(runtime), "audio_engine.build()");

    // --- Phase 1: 4 voices, accumulate state ---
    std::fprintf(stderr, "\n--- Phase 1: 4 voices ---\n");

    // Tick frame to produce lane arrays, push to audio snapshot
    runtime.tick(0.0, 1.0 / 60.0, 0);
    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

    // Run 1 audio buffer (256 samples) via test path
    float output[512] = {};  // stereo output
    audio_engine.process_audio_for_test(output, 256);

    // Read back per-lane audio output from tracker's buffers_out
    // LoopBased layout: lane c is at offset c * kBufferSize in buffers_out[0]
    if (tracker_node && tracker_node->audio && !tracker_node->audio->buffers_out.empty()) {
        const auto& buf = tracker_node->audio->buffers_out[0];
        constexpr uint32_t kBufSize = 256;

        // After 1 buffer, each lane accumulated its lane value once:
        // Voice 0: value=10 → accumulated=10
        // Voice 1: value=20 → accumulated=20
        // Voice 2: value=30 → accumulated=30
        // Voice 3: value=40 → accumulated=40
        // The audio output for each lane is the accumulated value (DC)
        if (buf.size() >= 4 * kBufSize) {
            float lane0_val = buf[0 * kBufSize];  // first sample of lane 0
            float lane1_val = buf[1 * kBufSize];  // first sample of lane 1
            float lane2_val = buf[2 * kBufSize];
            float lane3_val = buf[3 * kBufSize];

            check_float(lane0_val, 10.0f, 0.1f, "lane 0 accumulated = 10");
            check_float(lane1_val, 20.0f, 0.1f, "lane 1 accumulated = 20");
            check_float(lane2_val, 30.0f, 0.1f, "lane 2 accumulated = 30");
            check_float(lane3_val, 40.0f, 0.1f, "lane 3 accumulated = 40");
        } else {
            check(false, "buffer too small for 4 lanes");
        }
    }

    // --- Phase 2: Remove voice 1 (second voice), compact to 3 ---
    std::fprintf(stderr, "\n--- Phase 2: compact to 3 voices (remove voice 1) ---\n");

    // Change active_mask: 0xF=1111 → 0xD=1101 (voice 1 removed)
    if (src_node) {
        auto mask_it = src_node->param_indices.find("active_mask");
        if (mask_it != src_node->param_indices.end()) {
            src_node->param_values[mask_it->second] = 13.0f;  // 0xD = 1101
        }
    }

    // Tick frame to produce compacted lane arrays
    runtime.tick(1.0 / 60.0, 1.0 / 60.0, 1);
    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

    // Run another audio buffer
    float output2[512] = {};
    audio_engine.process_audio_for_test(output2, 256);

    // Now the lane arrays are: values=[10, 30, 40], lane_ids=[L1, L3, L4]
    // (voice 1 removed, voice 2 shifted to position 1)
    //
    // Expected per-lane state after 2 buffers:
    //   Position 0 (L1): was 10, now adds 10 → accumulated=20
    //   Position 1 (L3): was 30, now adds 30 → accumulated=60
    //   Position 2 (L4): was 40, now adds 40 → accumulated=80
    //
    // If lane_ids were NOT propagated (positional fallback):
    //   Position 0 (id=1): state from L1=10, adds 10 → 20 ✓ (happens to match)
    //   Position 1 (id=2): state from L2=20, adds 30 → 50 ✗ (should be 60)
    //   Position 2 (id=3): state from L3=30, adds 40 → 70 ✗ (should be 80)
    if (tracker_node && tracker_node->audio && !tracker_node->audio->buffers_out.empty()) {
        const auto& buf = tracker_node->audio->buffers_out[0];
        constexpr uint32_t kBufSize = 256;

        if (buf.size() >= 3 * kBufSize) {
            float pos0_val = buf[0 * kBufSize];
            float pos1_val = buf[1 * kBufSize];
            float pos2_val = buf[2 * kBufSize];

            check_float(pos0_val, 20.0f, 0.1f,
                        "position 0 (L1): accumulated=20 (10+10)");
            check_float(pos1_val, 60.0f, 0.1f,
                        "position 1 (L3): accumulated=60 (30+30), NOT 50 (positional would give 20+30)");
            check_float(pos2_val, 80.0f, 0.1f,
                        "position 2 (L4): accumulated=80 (40+40), NOT 70 (positional would give 30+40)");
        } else {
            check(false, "buffer too small for 3 lanes after compaction");
        }
    }

    // Cleanup
    audio_engine.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
