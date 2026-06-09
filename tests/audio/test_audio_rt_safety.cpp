// Audio real-time safety guard (lane-value Phase 8b).
//
// "Audio execution remains real-time safe" is an acceptance criterion of the
// value-model clean break — enforced by design (allow_grow=false pools,
// BridgeValueSlot::write_clamped) but never VERIFIED. This installs a
// program-global allocation counter and asserts the audio callback
// (AudioEngine::process_audio_for_test) performs ZERO heap allocations in steady
// state across the value-model execution paths: LoopBased lane lift,
// InstancePerLane, and a bridge-fed Many input that OVERFLOWS the slot capacity
// (clamped, must stay alloc-free).
//
// The override below replaces global operator new/delete for the whole linked
// executable (incl. vivid_runtime_testlib, where the audio executor lives).
// process_audio_for_test runs on the calling (test) thread, so the counter sees
// its allocations directly. Discipline: nothing allocating (fprintf/string/check)
// between arm and disarm — measure() snapshots the count, disarms, then returns.

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<size_t> g_alloc{0};
bool g_armed = false;   // set/read only on the test thread
}

void* operator new(std::size_t n) {
    if (g_armed) g_alloc.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    if (g_armed) g_alloc.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

namespace {

// Warm up (settle first-touch), then arm and run N blocks measuring allocations.
// Pushes the frame→audio snapshot once up front (frame-thread op, not the RT
// callback — excluded from the measurement). Returns the steady-state alloc count.
size_t measure_callback_allocs(vivid::RuntimeCore& rt, vivid::AudioEngine& eng,
                               int warmup, int n, uint32_t frames) {
    static float out[2048];
    rt.tick(0.0, 1.0 / 60.0, 0);
    rt.audio_frame_bridge().push_to_audio(*rt.compiled_graph());
    for (int i = 0; i < warmup; ++i) eng.process_audio_for_test(out, frames);

    g_alloc.store(0, std::memory_order_relaxed);
    g_armed = true;
    for (int i = 0; i < n; ++i) eng.process_audio_for_test(out, frames);
    g_armed = false;
    return g_alloc.load(std::memory_order_relaxed);
}

void add_conn(vivid::Graph& g, const char* fn, const char* fp,
              const char* tn, const char* tp, const char* bridge = nullptr) {
    g.add_connection(fn, fp, tn, tp);
    if (bridge) g.set_connection_bridge(fn, fp, tn, tp, bridge);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_audio_rt_safety_staging";
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
    stage("lane_source_op.dylib");
    stage("audio_lane_op.dylib");

    std::fprintf(stderr, "\n=== test_audio_rt_safety ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    constexpr int kWarmup = 8;
    constexpr int kBlocks = 64;
    constexpr uint32_t kFrames = 256;

    // --- Case 1: LoopBased lane lift ---
    std::fprintf(stderr, "\n--- LoopBased lane lift ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "IdentityLaneSourceOp", {{"active_mask", 15.0f}, {"base", 100.0f}});
        g.add_node("slew", "LaneSlewOp", {{"rate", 0.5f}});
        g.add_node("out", "audio_out");
        add_conn(g, "src", "out", "slew", "input", "snapshot");
        add_conn(g, "src", "lane_ids", "slew", "lane_ids", "snapshot");
        add_conn(g, "slew", "output", "out", "input");

        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "LoopBased: builds");
        vivid::AudioEngine eng;
        check(eng.build(rt), "LoopBased: engine builds");
        auto* slew = rt.compiled_graph()->find_node("slew");
        check(slew && slew->audio &&
              slew->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
              "LoopBased: strategy assigned");

        size_t allocs = measure_callback_allocs(rt, eng, kWarmup, kBlocks, kFrames);
        check(allocs == 0, "LoopBased: ZERO heap allocations in the audio callback");
        if (allocs != 0)
            std::fprintf(stderr, "  [!] LoopBased steady-state allocs over %d blocks: %zu\n", kBlocks, allocs);
        eng.shutdown();
    }

    // --- Case 2: InstancePerLane ---
    std::fprintf(stderr, "\n--- InstancePerLane ---\n");
    {
        vivid::Graph g;
        g.add_node("dc", "MultiChannelDcSourceOp");
        g.add_node("slew", "LaneSlewOp", {{"rate", 0.5f}});
        g.add_node("out", "audio_out");
        add_conn(g, "dc", "output", "slew", "input");
        add_conn(g, "slew", "output", "out", "input");

        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "IPL: builds");
        vivid::AudioEngine eng;
        check(eng.build(rt), "IPL: engine builds");
        auto* slew = rt.compiled_graph()->find_node("slew");
        check(slew && slew->audio &&
              slew->audio->execution_strategy == vivid::LaneExecutionStrategy::InstancePerLane,
              "IPL: strategy assigned");

        size_t allocs = measure_callback_allocs(rt, eng, kWarmup, kBlocks, kFrames);
        check(allocs == 0, "IPL: ZERO heap allocations in the audio callback");
        if (allocs != 0)
            std::fprintf(stderr, "  [!] IPL steady-state allocs over %d blocks: %zu\n", kBlocks, allocs);
        eng.shutdown();
    }

    // --- Case 3: bridge-fed Many input that OVERFLOWS the slot (clamped) ---
    std::fprintf(stderr, "\n--- bridge-fed Many overflow (count=2048 > 1024) ---\n");
    {
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 2048.0f}});
        g.add_node("al", "AudioLaneOp");
        g.add_node("out", "audio_out");
        add_conn(g, "src", "out", "al", "values", "snapshot");
        add_conn(g, "al", "out", "out", "input");

        vivid::RuntimeCore rt;
        check(rt.build(g, registry), "Overflow: builds");
        vivid::AudioEngine eng;
        check(eng.build(rt), "Overflow: engine builds");

        size_t allocs = measure_callback_allocs(rt, eng, kWarmup, kBlocks, kFrames);
        check(allocs == 0, "Overflow: ZERO heap allocations (clamp is RT-safe)");
        if (allocs != 0)
            std::fprintf(stderr, "  [!] Overflow steady-state allocs over %d blocks: %zu\n", kBlocks, allocs);
        // The oversized Many was clamped — the overflow is observable.
        check(rt.audio_frame_bridge().lane_overflow_count() > 0,
              "Overflow: bridge overflow counter is observable (>0)");
        eng.shutdown();
    }

    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
