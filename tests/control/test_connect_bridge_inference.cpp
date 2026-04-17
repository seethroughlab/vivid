// Verify RuntimeAPI::connect auto-infers a cross-cadence bridge kind when
// the caller doesn't supply one, so UI drags (which pass no bridge) between
// audio and frame nodes no longer silently drop at compile time.
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

static const vivid::ConnectionDef* find_conn(const vivid::Graph& g,
                                             const std::string& fn, const std::string& fp,
                                             const std::string& tn, const std::string& tp) {
    for (const auto& c : g.connections()) {
        if (c.from_node == fn && c.from_port == fp &&
            c.to_node == tn && c.to_port == tp) return &c;
    }
    return nullptr;
}

static void seed_nodes(vivid::Graph& g) {
    g.add_node("audio1", "AudioTestOp", {{"level", 0.5f}});
    g.add_node("ctrl1",  "TestOp",      {{"scale", 1.0f}});
    g.add_node("pass1",  "ControlPassOp", {{"gain", 1.0f}});
    g.add_node("lanes1", "IdentityLaneSourceOp", {{"active_mask", 3.0f}, {"base", 1.0f}});
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_bridge_infer_staging";
    std::filesystem::create_directories(staging);
    for (const char* name : {
        "test_op_v1.dylib", "control_pass_op.dylib",
        "audio_test_op.dylib", "identity_lane_source_op.dylib"}) {
        std::filesystem::copy_file(build_dir + "/" + name, staging + "/" + name,
            std::filesystem::copy_options::overwrite_existing);
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");
    check(registry.find("AudioTestOp") != nullptr,           "AudioTestOp registered");
    check(registry.find("TestOp") != nullptr,                "TestOp registered");
    check(registry.find("ControlPassOp") != nullptr,         "ControlPassOp registered");
    check(registry.find("IdentityLaneSourceOp") != nullptr,  "IdentityLaneSourceOp registered");

    // --- Test 1: audio -> frame ports (rms, peak, waveform) ---
    {
        std::fprintf(stderr, "\n--- audio -> frame: analysis ports ---\n");
        vivid::Graph g; vivid::RuntimeCore rt; vivid::AudioEngine ae;
        seed_nodes(g);
        vivid::RuntimeAPI api(g, rt, ae, registry);

        auto r1 = api.connect("audio1/rms", "ctrl1/scale");
        check(r1.ok, "connect audio1/rms -> ctrl1/scale");
        const auto* c1 = find_conn(g, "audio1", "rms", "ctrl1", "scale");
        check(c1 && c1->bridge == "rms", "inferred bridge = 'rms'");
        check(r1.message.find("(bridge: rms)") != std::string::npos,
              "success message reports inferred bridge");

        auto r2 = api.connect("audio1/peak", "pass1/gain");
        check(r2.ok, "connect audio1/peak -> pass1/gain");
        const auto* c2 = find_conn(g, "audio1", "peak", "pass1", "gain");
        check(c2 && c2->bridge == "peak", "inferred bridge = 'peak'");

        auto r3 = api.connect("audio1/waveform", "pass1/in");
        check(r3.ok, "connect audio1/waveform -> pass1/in");
        const auto* c3 = find_conn(g, "audio1", "waveform", "pass1", "in");
        check(c3 && c3->bridge == "waveform", "inferred bridge = 'waveform'");
    }

    // --- Test 2: frame -> audio scalar defaults to 'hold' ---
    {
        std::fprintf(stderr, "\n--- frame -> audio scalar ---\n");
        vivid::Graph g; vivid::RuntimeCore rt; vivid::AudioEngine ae;
        seed_nodes(g);
        vivid::RuntimeAPI api(g, rt, ae, registry);

        auto r = api.connect("ctrl1/out", "audio1/level");
        check(r.ok, "connect ctrl1/out -> audio1/level");
        const auto* c = find_conn(g, "ctrl1", "out", "audio1", "level");
        check(c && c->bridge == "hold", "inferred bridge = 'hold'");
    }

    // --- Test 3: frame lane_array -> audio defaults to 'snapshot' ---
    {
        std::fprintf(stderr, "\n--- frame lane_array -> audio ---\n");
        vivid::Graph g; vivid::RuntimeCore rt; vivid::AudioEngine ae;
        seed_nodes(g);
        vivid::RuntimeAPI api(g, rt, ae, registry);

        auto r = api.connect("lanes1/lane_ids", "audio1/level");
        check(r.ok, "connect lanes1/lane_ids -> audio1/level");
        const auto* c = find_conn(g, "lanes1", "lane_ids", "audio1", "level");
        check(c && c->bridge == "snapshot", "inferred bridge = 'snapshot'");
    }

    // --- Test 4: same-cadence connections are never annotated ---
    {
        std::fprintf(stderr, "\n--- same-cadence untouched ---\n");
        vivid::Graph g; vivid::RuntimeCore rt; vivid::AudioEngine ae;
        seed_nodes(g);
        vivid::RuntimeAPI api(g, rt, ae, registry);

        auto r = api.connect("ctrl1/out", "pass1/in");
        check(r.ok, "connect ctrl1/out -> pass1/in (frame -> frame)");
        const auto* c = find_conn(g, "ctrl1", "out", "pass1", "in");
        check(c && c->bridge.empty(), "same-cadence edge has no bridge");
        check(r.message.find("bridge:") == std::string::npos,
              "success message reports no inferred bridge");
    }

    // --- Test 5: explicit bridge string wins over inference ---
    {
        std::fprintf(stderr, "\n--- explicit bridge wins ---\n");
        vivid::Graph g; vivid::RuntimeCore rt; vivid::AudioEngine ae;
        seed_nodes(g);
        vivid::RuntimeAPI api(g, rt, ae, registry);

        auto r = api.connect("audio1/rms", "ctrl1/scale", false, "peak");
        check(r.ok, "connect audio1/rms -> ctrl1/scale with explicit bridge='peak'");
        const auto* c = find_conn(g, "audio1", "rms", "ctrl1", "scale");
        check(c && c->bridge == "peak", "explicit 'peak' overrides inferred 'rms'");
        check(r.message.find("bridge:") == std::string::npos,
              "explicit bridge does not emit inferred-bridge message");
    }

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
