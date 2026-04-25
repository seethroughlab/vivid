// Integration test: Clock → Oscillator → Gain → audio_out, all on the audio
// execution world. Verifies direct audio-edge routing with core operators.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/control/runtime_command_sink.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/settings.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

static int passes = 0;

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "=== test_audio_sequencer_graph ===\n");

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
    std::filesystem::copy_file(build_dir + "/drum_sequencer.dylib", staging + "/drum_sequencer.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/drum_kick.dylib", staging + "/drum_kick.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    // --- Operator registry ---
    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    // Verify operators loaded
    check(registry.find("Clock") != nullptr, "Clock operator loaded");
    check(registry.find("Oscillator") != nullptr, "Oscillator operator loaded");
    check(registry.find("Gain") != nullptr, "Gain operator loaded");

    // Verify clock is audio-only
    {
        auto* clock_loader = registry.find("Clock");
        if (clock_loader) {
            const auto* desc = clock_loader->descriptor();
            check(desc->has_process_audio == 1, "clock has process_audio");
            check(desc->has_process_frame == 0, "clock has no process_frame");
        }
    }

    // Verify Oscillator is audio cadence with freq_cv input
    {
        auto* osc_loader = registry.find("Oscillator");
        if (osc_loader) {
            const auto* desc = osc_loader->descriptor();
            check(vivid_operator_kind(desc) == VIVID_OP_AUDIO, "Oscillator is audio cadence");
            check(desc->has_process_audio == 1, "Oscillator has process_audio");
            bool has_freq_cv = false;
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                if (std::string(desc->ports[i].name) == "freq_cv" &&
                    desc->ports[i].type == VIVID_PORT_SCALAR &&
                    desc->ports[i].direction == VIVID_PORT_INPUT) {
                    has_freq_cv = true;
                    break;
                }
            }
            check(has_freq_cv, "Oscillator has SCALAR freq_cv input port");
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

    // Build runtime
    vivid::RuntimeCore runtime;
    runtime.set_audio_buffer_size(512);
    bool build_ok = runtime.build(graph, registry);
    check(build_ok, "Runtime built");

    // Build audio engine
    vivid::AudioEngine audio;
    bool audio_ok = audio.build(runtime);
    check(audio_ok, "Audio engine built");
    check(audio.buffer_size() == 512, "audio engine uses configured buffer size");

    // Start audio with null device (no real audio output)
    bool started = audio.start(true);
    check(started, "Audio engine started (null device)");

    // Tick for ~0.5 seconds (30 frames at ~60Hz)
    const uint32_t audio_frames = audio.buffer_size();
    std::vector<float> audio_buf(audio_frames * 2, 0.0f);
    for (uint64_t frame = 0; frame < 30; ++frame) {
        double time = frame * 0.016;
        runtime.pre_tick_audio_sync(time);
        runtime.tick(time, 0.016, frame);
        runtime.post_tick_audio_sync();
        audio.process_audio_for_test(audio_buf.data(), audio_frames);
    }

    // Pull final audio results into CompiledNode for assertions below
    runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());

    // Check that audio output has non-zero RMS (the gain node should pass through sound)
    const auto& analysis = audio.analysis_read();
    int gain_idx = audio.audio_node_index("gain1");
    check(gain_idx >= 0, "Gain node found in audio engine");

    if (gain_idx >= 0) {
        float rms = analysis.rms[gain_idx][0];
        float peak = analysis.peak[gain_idx][0];
        std::fprintf(stderr, "    gain1 RMS=%.6f  peak=%.6f\n", rms, peak);
        check(peak > 0.001f, "Gain produced non-zero audio output");
    }

    // Check that the oscillator's audio output is being fed through the graph
    int osc_idx = audio.audio_node_index("osc1");
    check(osc_idx >= 0, "Oscillator node found in audio engine");

    if (osc_idx >= 0) {
        float osc_peak = analysis.peak[osc_idx][0];
        std::fprintf(stderr, "    osc1 peak=%.6f\n", osc_peak);
        check(osc_peak > 0.001f, "Oscillator produced non-zero audio output");
    }

    auto* clock_cn = runtime.compiled_graph()->find_node("clock1");
    check(clock_cn != nullptr, "Clock node found in runtime");
    if (clock_cn) {
        check(clock_cn->active_cadence == vivid::Cadence::Audio,
              "Clock remains on the audio execution world");
    }

    // Change the buffer size through the same preference command path used by the UI.
    vivid::RuntimeAPI api(graph, runtime, audio, registry);
    RuntimeCommandSink sink(api);
    vivid::Settings settings;
    settings.audio_buffer_size = 512;
    bool has_gpu_ops = false;
    bool has_audio = true;
    sink.set_settings(&settings);
    sink.set_runtime_flags(&has_gpu_ops, &has_audio);
    sink.set_audio_buffer_preference_callback(
        [&](uint32_t old_size, uint32_t new_size, std::string& error) {
            runtime.set_audio_buffer_size(new_size);
            auto rebuild_result = api.rebuild_current_graph(has_gpu_ops, has_audio);
            if (rebuild_result.ok) {
                error.clear();
                return true;
            }

            runtime.set_audio_buffer_size(old_size);
            auto restore_result = api.rebuild_current_graph(has_gpu_ops, has_audio);
            error = rebuild_result.message;
            if (!restore_result.ok)
                error += " (restore failed: " + restore_result.message + ")";
            return false;
        });

    auto set_freq_result = api.set_param("osc1", "frequency", 330.0f);
    check(set_freq_result.ok, "set_param(osc1/frequency)");
    std::string pref_error;
    check(sink.try_set_audio_buffer_preference(1024, &pref_error), "audio buffer preference applied");
    check(settings.audio_buffer_size == 1024, "settings updated to new audio buffer size");
    check(runtime.audio_buffer_size() == 1024, "runtime updated to new audio buffer size");
    check(audio.buffer_size() == 1024, "audio engine rebuilt with new audio buffer size");
    std::vector<float> rebuilt_audio_buf(audio.buffer_size() * 2, 0.0f);
    audio.process_audio_for_test(rebuilt_audio_buf.data(), audio.buffer_size());

    auto* osc_cn = runtime.compiled_graph()->find_node("osc1");
    check(osc_cn != nullptr, "Oscillator node still present after audio buffer rebuild");
    if (osc_cn) {
        auto freq_it = osc_cn->param_indices.find("frequency");
        check(freq_it != osc_cn->param_indices.end(), "Oscillator frequency param exists after rebuild");
        if (freq_it != osc_cn->param_indices.end()) {
            check_float(osc_cn->param_values[freq_it->second], 330.0f, 1e-4f,
                        "Oscillator frequency preserved across buffer-size rebuild");
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

    audio.shutdown();
    runtime.shutdown();

    // --- Regression: DrumSequencer per-drum MIDI output routes to a matching
    //                 Drum* voice without a DrumKit hub. This is the pattern
    //                 seed graphs use post-per-drum-outputs refactor. ---
    const char* drum_graph_json = R"({
        "metronome": { "enabled": true, "bpm": 120.0, "beats_per_bar": 4 },
        "nodes": {
            "seq": {
                "type": "DrumSequencer",
                "params": {
                    "clock_source": 1,
                    "kick_0": 1.0,
                    "kick_4": 1.0,
                    "kick_8": 1.0,
                    "kick_12": 1.0,
                    "kick_ma_0": 1.0,
                    "kick_ma_4": 1.0,
                    "kick_ma_8": 1.0,
                    "kick_ma_12": 1.0
                }
            },
            "kick": { "type": "DrumKick", "params": { "volume": 0.8 } },
            "master": { "type": "Gain", "params": { "gain": 1.0 } },
            "aout": { "type": "audio_out", "params": {} }
        },
        "connections": [
            { "from": "seq/kick_out", "to": "kick/midi_in" },
            { "from": "kick/output", "to": "master/input" },
            { "from": "master/output", "to": "aout/input" }
        ]
    })";

    vivid::Graph drum_graph;
    bool drum_loaded = drum_graph.load_from_string(drum_graph_json);
    check(drum_loaded, "Drum graph loaded from JSON string");
    if (drum_loaded) {
        registry.load_for_graph(drum_graph);

        vivid::RuntimeCore drum_runtime;
        drum_runtime.set_audio_buffer_size(512);
        bool drum_build_ok = drum_runtime.build(drum_graph, registry);
        check(drum_build_ok, "Drum runtime built");

        vivid::AudioEngine drum_audio;
        bool drum_audio_ok = drum_audio.build(drum_runtime);
        check(drum_audio_ok, "Drum audio engine built");

        if (drum_build_ok && drum_audio_ok) {
            const uint32_t drum_frames = drum_audio.buffer_size();
            std::vector<float> drum_audio_buf(drum_frames * 2, 0.0f);
            float max_master_peak = 0.0f;
            float max_kick_peak = 0.0f;

            for (uint64_t frame = 0; frame < 24; ++frame) {
                double time = frame * 0.016;
                drum_runtime.pre_tick_audio_sync(time);
                drum_runtime.tick(time, 0.016, frame);
                drum_runtime.post_tick_audio_sync();
                drum_audio.process_audio_for_test(drum_audio_buf.data(), drum_frames);
                drum_runtime.audio_frame_bridge().pull_from_audio(*drum_runtime.compiled_graph());

                const auto& drum_analysis = drum_audio.analysis_read();
                int master_idx = drum_audio.audio_node_index("master");
                int kick_idx = drum_audio.audio_node_index("kick");
                if (master_idx >= 0)
                    max_master_peak = std::max(max_master_peak, drum_analysis.peak[master_idx][0]);
                if (kick_idx >= 0)
                    max_kick_peak = std::max(max_kick_peak, drum_analysis.peak[kick_idx][0]);
            }

            std::fprintf(stderr, "    drum master peak=%.6f  kick peak=%.6f\n",
                         max_master_peak, max_kick_peak);
            check(max_kick_peak > 0.001f,
                  "DrumKick receives sequencer MIDI from seq/kick_out and produces audio");
            check(max_master_peak > 0.001f,
                  "Drum graph produces non-zero master audio");
        }

        drum_audio.shutdown();
        drum_runtime.shutdown();
    }

    // Cleanup
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Results: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
