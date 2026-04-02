// Integration test: fixed-cadence operator assignment.
// Verifies that _fr operators are frame-only and _au operators are audio-only,
// and that audio-frame bridge edges compile as snapshot
// transports with an explicit bridge kind.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/graph_compiler.h"
#include "runtime/compiled_graph.h"
#include "runtime/cadence_types.h"
#include <cstdio>
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

    std::fprintf(stderr, "=== test_fixed_cadence_assignment ===\n");

    // Stage required dylibs
    const std::string staging = build_dir + "/.test_fixed_cadence_assignment_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("clock_fr.dylib");
    stage("clock_au.dylib");
    stage("oscillator.dylib");
    stage("lfo_fr.dylib");
    stage("lfo_au.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Verify clock_fr is frame-only and clock_au is audio-only
    {
        auto* loader_fr = registry.find("clock_fr");
        check(loader_fr != nullptr, "clock_fr loaded");
        if (loader_fr) {
            const auto* desc = loader_fr->descriptor();
            check(desc->has_process_frame == 1 && desc->has_process_audio == 0,
                  "clock_fr is frame-only");
        }

        auto* loader_au = registry.find("clock_au");
        check(loader_au != nullptr, "clock_au loaded");
        if (loader_au) {
            const auto* desc = loader_au->descriptor();
            check(desc->has_process_audio == 1 && desc->has_process_frame == 0,
                  "clock_au is audio-only");
        }
    }

    // =====================================================================
    // Test 1: Audio-only clock_au → Oscillator (both audio, Direct edge)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Audio-only clock → audio-only osc ===\n");
        vivid::Graph g;
        g.add_node("clock", "clock_au");
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
                  "clock_au runs at Audio");
            check(cn_osc->active_cadence == vivid::Cadence::Audio,
                  "osc is Audio");

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

        runtime.shutdown();
    }

    // =====================================================================
    // Test 2: Frame-only clock_fr → Oscillator (audio-frame bridge edge)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Frame-only clock → audio-only osc via hold bridge ===\n");
        vivid::Graph g;
        g.load_from_string(R"({
            "nodes": {
                "clock": { "type": "clock_fr" },
                "osc":   { "type": "Oscillator" }
            },
            "connections": [
                { "from": "clock/beat_phase", "to": "osc/freq_cv", "bridge": "hold" }
            ]
        })");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock_fr stays at Frame");

        bool found_bridge = false;
        for (const auto& e : runtime.compiled_graph()->edges) {
            if (runtime.compiled_graph()->nodes[e.from_node].node_id == "clock" &&
                runtime.compiled_graph()->nodes[e.to_node].node_id == "osc") {
                found_bridge = (e.transport == vivid::EdgeTransport::Snapshot &&
                                e.bridge_kind == vivid::BridgeKind::Hold);
                break;
            }
        }
        check(found_bridge, "clock→osc edge compiles as hold bridge transport");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 3: Frame chain — clock_fr → lfo_fr (both frame, Direct edge)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Frame chain ===\n");
        vivid::Graph g;
        g.add_node("clock", "clock_fr");
        g.add_node("lfo", "lfo_fr");
        g.add_connection("clock", "beat_phase", "lfo", "beat_phase");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        auto* cn_lfo = runtime.compiled_graph()->find_node("lfo");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock_fr is Frame");
        check(cn_lfo && cn_lfo->active_cadence == vivid::Cadence::Frame,
              "lfo_fr is Frame");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 4: Standalone frame node stays at Frame
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Standalone frame node ===\n");
        vivid::Graph g;
        g.add_node("clock", "clock_fr");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        auto* cn_clock = runtime.compiled_graph()->find_node("clock");
        check(cn_clock && cn_clock->active_cadence == vivid::Cadence::Frame,
              "clock_fr stays at Frame (standalone)");

        runtime.shutdown();
    }

    // --- Summary ---
    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\nResults: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");
    return failures > 0 ? 1 : 0;
}
