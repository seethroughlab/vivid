// Integration test: Clock → DrumSequencer → DrumKick, all audio domain.
// Verifies the new AudioFloatPortWire routing and float gate triggers.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/builtin_operators.h"
#include <cstdio>
#include <cmath>
#include <cstring>
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

int main() {
    std::fprintf(stderr, "=== test_audio_domain_sequencer ===\n");

    // --- Operator registry ---
    vivid::OperatorRegistry registry;
    // Scan an isolated directory with only the operators we need, to avoid
    // probing GPU operators that crash in headless mode (pre-existing issue).
    const char* ops_dir = std::getenv("VIVID_TEST_OPS_DIR");
    registry.scan_deferred(ops_dir ? ops_dir : ".");

    // Verify operators loaded
    check(registry.find("Clock") != nullptr, "Clock operator loaded");
    check(registry.find("DrumSequencer") != nullptr, "DrumSequencer operator loaded");
    check(registry.find("DrumKick") != nullptr, "DrumKick operator loaded");
    check(registry.find("DrumSnare") != nullptr, "DrumSnare operator loaded");
    check(registry.find("DrumHiHat") != nullptr, "DrumHiHat operator loaded");

    // Verify Clock is now audio domain
    {
        auto* clock_loader = registry.find("Clock");
        if (clock_loader) {
            const auto* desc = clock_loader->descriptor();
            check(desc->domain == VIVID_DOMAIN_AUDIO, "Clock is audio domain");
            check(desc->has_process_audio == 1, "Clock has process_audio");
        }
    }

    // Verify DrumSequencer is now audio domain
    {
        auto* seq_loader = registry.find("DrumSequencer");
        if (seq_loader) {
            const auto* desc = seq_loader->descriptor();
            check(desc->domain == VIVID_DOMAIN_AUDIO, "DrumSequencer is audio domain");
            check(desc->has_process_audio == 1, "DrumSequencer has process_audio");
        }
    }

    // Verify DrumKick has trigger float input
    {
        auto* kick_loader = registry.find("DrumKick");
        if (kick_loader) {
            const auto* desc = kick_loader->descriptor();
            bool has_trigger = false;
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                if (std::string(desc->ports[i].name) == "trigger" &&
                    desc->ports[i].type == VIVID_PORT_FLOAT &&
                    desc->ports[i].direction == VIVID_PORT_INPUT) {
                    has_trigger = true;
                    break;
                }
            }
            check(has_trigger, "DrumKick has float trigger input port");
        }
    }

    // --- Build graph: Clock → DrumSequencer → DrumKick → audio_out ---
    const char* graph_json = R"({
        "nodes": {
            "clock1": { "type": "Clock", "params": { "bpm": 120.0 } },
            "seq1": { "type": "DrumSequencer", "params": {
                "steps": 4,
                "kick_0": 1, "kick_1": 1, "kick_2": 1, "kick_3": 1
            }},
            "kick1": { "type": "DrumKick", "params": { "volume": 0.8 } },
            "aout": { "type": "audio_out", "params": {} }
        },
        "connections": [
            { "from": "clock1/bar_phase", "to": "seq1/beat_phase" },
            { "from": "seq1/kick", "to": "kick1/trigger" },
            { "from": "kick1/output", "to": "aout/input" }
        ]
    })";

    vivid::Graph graph;
    bool loaded = graph.load_from_string(graph_json);
    check(loaded, "Graph loaded from JSON string");
    if (!loaded) {
        std::fprintf(stderr, "\nResults: %d passed, %d failed\n", passes, failures);
        return failures > 0 ? 1 : 0;
    }

    registry.load_for_graph(graph);

    // Build scheduler
    vivid::Scheduler sched;
    bool sched_ok = sched.build(graph, registry);
    check(sched_ok, "Scheduler built");

    // Build audio engine
    vivid::AudioEngine audio;
    bool audio_ok = audio.build(graph, registry, sched);
    check(audio_ok, "Audio engine built");

    // Start audio with null device (no real audio output)
    bool started = audio.start(true);
    check(started, "Audio engine started (null device)");

    // Tick for ~0.5 seconds (30 frames at ~60Hz)
    for (uint64_t frame = 0; frame < 30; ++frame) {
        double time = frame * 0.016;
        sched.tick(time, 0.016, frame);
        audio.push_params(sched);
        audio.inject_analysis(sched);
    }

    // Check that audio output has non-zero RMS (the kick should be producing sound)
    const auto& analysis = audio.analysis_read();
    int kick_idx = audio.audio_node_index("kick1");
    check(kick_idx >= 0, "Kick node found in audio engine");

    if (kick_idx >= 0) {
        float rms = analysis.rms[kick_idx];
        float peak = analysis.peak[kick_idx];
        std::fprintf(stderr, "    kick1 RMS=%.6f  peak=%.6f\n", rms, peak);
        check(peak > 0.001f, "DrumKick produced non-zero audio output");
    }

    // Check that the sequencer's float outputs are being fed back to scheduler
    // (via inject_analysis → inject_external_output)
    for (size_t si = 0; si < sched.nodes().size(); ++si) {
        const auto& ns = sched.nodes()[si];
        if (ns.node_id == "seq1") {
            // Look for the "step" output
            auto step_it = ns.output_port_indices.find("step");
            if (step_it != ns.output_port_indices.end()) {
                // After 30 ticks at 120 BPM, some steps should have fired
                // The step value should be a valid step index (0-3)
                float step_val = ns.output_values[step_it->second];
                std::fprintf(stderr, "    seq1/step = %.2f\n", step_val);
                check(step_val >= 0.0f && step_val <= 3.0f,
                      "DrumSequencer step output fed back to scheduler");
            }
            break;
        }
    }

    // Check no errors on any audio node
    bool any_error = false;
    for (size_t i = 0; i < analysis.errored.size(); ++i) {
        if (analysis.errored[i]) {
            std::fprintf(stderr, "    ERROR on node %zu: %s\n", i,
                         analysis.error_msgs[i].data());
            any_error = true;
        }
    }
    check(!any_error, "No audio node errors");

    // Cleanup
    audio.shutdown();
    sched.shutdown();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Results: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
