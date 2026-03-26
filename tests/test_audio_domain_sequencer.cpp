// Integration test: Clock → Oscillator → Gain → audio_out, all audio domain.
// Verifies AudioFloatPortWire routing with core CMake-built operators.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/builtin_operators.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include <cstdio>
#include <cmath>
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

    std::fprintf(stderr, "=== test_audio_domain_sequencer ===\n");

    // --- Stage required dylibs into an isolated directory ---
    const std::string staging = build_dir + "/.test_audio_sequencer_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/clock.dylib", staging + "/clock.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/oscillator.dylib", staging + "/oscillator.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/gain.dylib", staging + "/gain.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    // --- Operator registry ---
    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Verify operators loaded
    check(registry.find("Clock") != nullptr, "Clock operator loaded");
    check(registry.find("Oscillator") != nullptr, "Oscillator operator loaded");
    check(registry.find("Gain") != nullptr, "Gain operator loaded");

    // Verify Clock is now audio domain
    {
        auto* clock_loader = registry.find("Clock");
        if (clock_loader) {
            const auto* desc = clock_loader->descriptor();
            check(desc->execution_env == VIVID_ENV_AUDIO, "Clock is audio domain");
            check(desc->has_process_audio == 1, "Clock has process_audio");
        }
    }

    // Verify Oscillator is audio domain with freq_cv input
    {
        auto* osc_loader = registry.find("Oscillator");
        if (osc_loader) {
            const auto* desc = osc_loader->descriptor();
            check(desc->execution_env == VIVID_ENV_AUDIO, "Oscillator is audio domain");
            check(desc->has_process_audio == 1, "Oscillator has process_audio");
            bool has_freq_cv = false;
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                if (std::string(desc->ports[i].name) == "freq_cv" &&
                    desc->ports[i].type == VIVID_PORT_SIGNAL &&
                    desc->ports[i].direction == VIVID_PORT_INPUT) {
                    has_freq_cv = true;
                    break;
                }
            }
            check(has_freq_cv, "Oscillator has float freq_cv input port");
        }
    }

    // --- Build graph: Clock → Oscillator → Gain → audio_out ---
    const char* graph_json = R"({
        "nodes": {
            "clock1": { "type": "Clock", "params": { "bpm": 120.0 } },
            "osc1": { "type": "Oscillator", "params": { "frequency": 440.0 } },
            "gain1": { "type": "Gain", "params": { "amplitude": 0.8 } },
            "aout": { "type": "audio_out", "params": {} }
        },
        "connections": [
            { "from": "clock1/beat_phase", "to": "osc1/freq_cv" },
            { "from": "osc1/output", "to": "gain1/input" },
            { "from": "gain1/output", "to": "aout/input" }
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
    float audio_buf[vivid::AudioEngine::kBufferSize * 2] = {};
    for (uint64_t frame = 0; frame < 30; ++frame) {
        double time = frame * 0.016;
        auto& cb = sched.cadence_bridge();
        auto* cg = sched.compiled_graph();
        cb.pull_from_audio(*cg);
        cb.update_sources(time, *cg);
        sched.tick(time, 0.016, frame);
        cb.push_to_audio(*cg);
        audio.process_audio_for_test(audio_buf, vivid::AudioEngine::kBufferSize);
    }

    // Pull final audio results into CompiledNode/NodeState for assertions below
    sched.cadence_bridge().pull_from_audio(*sched.compiled_graph());
    sched.sync_to_nodestate();

    // Check that audio output has non-zero RMS (the gain node should pass through sound)
    const auto& analysis = audio.analysis_read();
    int gain_idx = audio.audio_node_index("gain1");
    check(gain_idx >= 0, "Gain node found in audio engine");

    if (gain_idx >= 0) {
        float rms = analysis.rms[gain_idx];
        float peak = analysis.peak[gain_idx];
        std::fprintf(stderr, "    gain1 RMS=%.6f  peak=%.6f\n", rms, peak);
        check(peak > 0.001f, "Gain produced non-zero audio output");
    }

    // Check that the oscillator's audio output is being fed through the graph
    int osc_idx = audio.audio_node_index("osc1");
    check(osc_idx >= 0, "Oscillator node found in audio engine");

    if (osc_idx >= 0) {
        float osc_peak = analysis.peak[osc_idx];
        std::fprintf(stderr, "    osc1 peak=%.6f\n", osc_peak);
        check(osc_peak > 0.001f, "Oscillator produced non-zero audio output");
    }

    auto* clock_cn = sched.compiled_graph()->find_node("clock1");
    check(clock_cn != nullptr, "Clock node found in scheduler");
    if (clock_cn) {
        auto beat_phase_it = clock_cn->output_port_indices.find("beat_phase");
        auto beat_ms_it = clock_cn->output_port_indices.find("beat_ms");
        auto bar_phase_it = clock_cn->output_port_indices.find("bar_phase");
        check(beat_phase_it != clock_cn->output_port_indices.end(), "Clock beat_phase output exists");
        check(beat_ms_it != clock_cn->output_port_indices.end(), "Clock beat_ms output exists");
        check(bar_phase_it != clock_cn->output_port_indices.end(), "Clock bar_phase output exists");
        if (beat_phase_it != clock_cn->output_port_indices.end() &&
            beat_ms_it != clock_cn->output_port_indices.end() &&
            bar_phase_it != clock_cn->output_port_indices.end()) {
            float beat_phase = clock_cn->output_values[beat_phase_it->second];
            float beat_ms = clock_cn->output_values[beat_ms_it->second];
            float bar_phase = clock_cn->output_values[bar_phase_it->second];
            std::fprintf(stderr, "    clock1 beat_phase=%.6f beat_ms=%.6f bar_phase=%.6f\n",
                         beat_phase, beat_ms, bar_phase);
            check(beat_phase > 0.001f, "Clock beat_phase advances");
            check(std::fabs(beat_ms - 500.0f) < 0.5f, "Clock beat_ms remains correct");
            check(bar_phase > 0.001f, "Clock bar_phase advances");
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
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Results: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
