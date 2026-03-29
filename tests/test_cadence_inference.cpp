// Integration test: CadenceOverride::Auto inference from downstream connections.
// Verifies that audio-capable nodes with Auto override get promoted to audio
// cadence when they feed downstream audio consumers, and that promotion is
// ephemeral — re-derived each compile, not persisted to the graph.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/graph_compiler.h"
#include "runtime/compiled_graph.h"
#include "runtime/cadence_types.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static int passes = 0;
static int failures = 0;

static void check(bool ok, const char* label) {
    if (ok) {
        std::fprintf(stderr, "  PASS: %s\n", label);
        passes++;
    } else {
        std::fprintf(stderr, "  FAIL: %s\n", label);
        failures++;
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "=== test_cadence_inference ===\n");

    // Stage required dylibs
    const std::string staging = build_dir + "/.test_cadence_inference_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("clock.dylib");
    stage("oscillator.dylib");
    stage("lfo.dylib");
    stage("gain.dylib");
    stage("audio_test_op.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Verify Clock is audio-capable (frame + audio)
    {
        auto* loader = registry.find("Clock");
        check(loader != nullptr, "Clock loaded");
        if (loader) {
            const auto* desc = loader->descriptor();
            check(desc->cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE,
                  "Clock is AUDIO_CAPABLE");
        }
    }

    // =====================================================================
    // Test 1: Basic inference — Auto node → Audio-only node
    // Clock (Auto, audio-capable) → Oscillator (audio-only)
    // Clock should be promoted to Audio, edge should be Direct.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Basic inference ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        g.add_node("osc", "Oscillator");
        g.add_connection("clock", "beat_phase", "osc", "freq_cv");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        auto* cn_osc = runtime.compiled_graph()->find_node("osc");
        check(cn_clock != nullptr, "clock node found");
        check(cn_osc != nullptr, "osc node found");
        if (cn_clock && cn_osc) {
            check(cn_clock->active_cadence == vivid::Cadence::Audio,
                  "clock promoted to Audio");
            check(cn_osc->active_cadence == vivid::Cadence::Audio,
                  "osc is Audio");

            // Find the edge between them
            bool found_direct = false;
            for (const auto& e : runtime.compiled_graph()->edges) {
                if (runtime.compiled_graph()->nodes[e.from_node].node_id == "clock" &&
                    runtime.compiled_graph()->nodes[e.to_node].node_id == "osc") {
                    found_direct = (e.transport == vivid::EdgeTransport::Direct);
                    break;
                }
            }
            check(found_direct, "clock→osc edge is Direct (same cadence)");
        }

        // NodeDef should remain Auto (promotion is ephemeral)
        auto* ndef = g.find_node("clock");
        check(ndef && ndef->cadence_override == vivid::CadenceOverride::Auto,
              "clock NodeDef stays Auto (not persisted)");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 2: Cascade — Auto_A → Auto_B → Audio-only
    // Both Auto nodes should be promoted.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Cascade inference ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        g.add_node("lfo", "LFO");
        g.add_node("osc", "Oscillator");
        g.add_connection("clock", "beat_phase", "lfo", "beat_phase");
        g.add_connection("lfo", "value", "osc", "freq_cv");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        auto* cn_lfo = runtime.compiled_graph()->find_node("lfo");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Audio,
              "clock promoted to Audio");
        check(cn_lfo && cn_lfo->active_cadence == vivid::Cadence::Audio,
              "lfo promoted to Audio");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 3: Frame override blocks promotion
    // Clock (Frame override) → Oscillator
    // Clock should stay at Frame, edge should be Snapshot.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Frame override blocks promotion ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        g.add_node("osc", "Oscillator");
        g.add_connection("clock", "beat_phase", "osc", "freq_cv");

        auto* ndef = g.find_node("clock");
        if (ndef) ndef->cadence_override = vivid::CadenceOverride::Frame;

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock stays at Frame");

        // Edge should be Snapshot (cross-cadence)
        bool found_snapshot = false;
        for (const auto& e : runtime.compiled_graph()->edges) {
            if (runtime.compiled_graph()->nodes[e.from_node].node_id == "clock" &&
                runtime.compiled_graph()->nodes[e.to_node].node_id == "osc") {
                found_snapshot = (e.transport == vivid::EdgeTransport::Snapshot);
                break;
            }
        }
        check(found_snapshot, "clock→osc edge is Snapshot");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 4: No audio consumer — no promotion
    // Clock (Auto, audio-capable) with no audio downstream stays Frame.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: No audio consumer ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        // No audio consumer connected

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock stays at Frame (no audio consumer)");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 5: Promotion re-derived on rebuild
    // After inference promotes a node, rebuild with consumer still connected.
    // Node should still be promoted (re-inferred, not persisted).
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Promotion re-derived on rebuild ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        g.add_node("osc", "Oscillator");
        g.add_connection("clock", "beat_phase", "osc", "freq_cv");

        // First build — inference promotes clock
        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "first build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Audio,
              "clock promoted on first build");

        // NodeDef should still be Auto
        auto* ndef = g.find_node("clock");
        check(ndef && ndef->cadence_override == vivid::CadenceOverride::Auto,
              "clock NodeDef is Auto (ephemeral)");

        // Second build — clock should be re-promoted from Auto
        runtime.shutdown();
        check(runtime.build(g, registry), "second build succeeds");

        cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Audio,
              "clock still Audio after rebuild (re-inferred)");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 6: Disconnect demotes to Frame
    // After inference promotes a node, remove the audio consumer. On rebuild
    // the node should revert to Frame (promotion is ephemeral).
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Disconnect demotes to Frame ===\n");
        vivid::Graph g;
        g.add_node("clock", "Clock");
        g.add_node("osc", "Oscillator");
        g.add_connection("clock", "beat_phase", "osc", "freq_cv");

        // First build — inference promotes clock
        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "first build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Audio,
              "clock promoted to Audio");

        // Disconnect the audio consumer
        g.remove_connection("clock", "beat_phase", "osc", "freq_cv");

        // Rebuild — clock should demote to Frame (no audio consumer)
        runtime.shutdown();
        check(runtime.build(g, registry), "rebuild after disconnect succeeds");

        cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock demoted to Frame after disconnect");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 7: Legacy InferredAudio (value 3) migrates to Auto on load
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Legacy InferredAudio migration ===\n");
        // Build a graph JSON with "cadence": 3 (legacy InferredAudio)
        const char* json = R"({
            "nodes": {
                "clock": { "type": "Clock", "cadence": 3 }
            },
            "connections": []
        })";

        vivid::Graph g;
        check(g.load_from_string(json, std::strlen(json)), "load legacy JSON");

        auto* ndef = g.find_node("clock");
        check(ndef != nullptr, "clock node found");
        if (ndef) {
            check(ndef->cadence_override == vivid::CadenceOverride::Auto,
                  "legacy InferredAudio (3) migrated to Auto");
        }
    }

    // --- Summary ---
    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\nResults: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");
    return failures > 0 ? 1 : 0;
}
