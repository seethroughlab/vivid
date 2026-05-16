// Phase 4 end-to-end demo: Tracker authors PITCH_BEND/PRESSURE/TIMBRE events,
// WavetableLayer consumes them via slot.pressure (→ amplitude) and slot.timbre
// (→ wavetable position). The demo graph walks C4→E4→G4→C5 with a pressure
// swell that peaks on row 4 and a timbre sweep that climbs through row 7.
//
// This test:
//  1. Bootstraps the operator registry (loads vivid-wavetable from the user's
//     installed packages). Skips with exit 0 if the package isn't available.
//  2. Loads graphs/audio/tracker_expression_demo.json from the source tree.
//  3. Renders ~1.6 seconds of audio at the project's default settings.
//  4. Asserts the master output is non-silent.
//  5. Asserts the segment around peak pressure/timbre (middle of the pattern)
//     has a measurably different brightness/loudness profile than the segment
//     at the start (pre-pressure) — proving expression actually shaped the
//     sound rather than passing through inert.

#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/runtime_bootstrap.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

// First-order high-pass difference: y[n] = x[n] - x[n-1]. RMS of the resulting
// signal divided by RMS of the original is a coarse "brightness" proxy —
// enough to detect timbre/wavetable-position shifts that move spectral energy.
float rough_brightness(const float* samples, size_t n) {
    if (n < 2) return 0.0f;
    double total_energy = 0.0;
    double hp_energy = 0.0;
    for (size_t i = 1; i < n; ++i) {
        const double s = samples[i];
        const double d = s - samples[i - 1];
        total_energy += s * s;
        hp_energy    += d * d;
    }
    if (total_energy <= 1e-12) return 0.0f;
    return static_cast<float>(std::sqrt(hp_energy / total_energy));
}

float rms(const float* samples, size_t n) {
    if (n == 0) return 0.0f;
    double e = 0.0;
    for (size_t i = 0; i < n; ++i) e += static_cast<double>(samples[i]) * samples[i];
    return static_cast<float>(std::sqrt(e / static_cast<double>(n)));
}

}  // namespace

int main(int argc, char* argv[]) {
    std::fprintf(stderr, "=== test_tracker_expression_demo ===\n");

    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_tracker_expression_demo <build_dir>\n");
        return 1;
    }
    const std::string build_dir = argv[1];

    // Resolve runtime paths so bootstrap_operator_registry can find the user's
    // installed packages (~/Library/Application Support/vivid/packages on macOS).
    auto runtime_paths = vivid::resolve_runtime_bootstrap_paths(argv[0]);

    vivid::OperatorRegistry registry;
    vivid::PackageCompiler pkg_compiler(runtime_paths.source_dir, runtime_paths.build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    vivid::RegistryBootstrapOptions bootstrap_opts;
    auto bootstrap = vivid::bootstrap_operator_registry(registry, &pkg_manager,
                                                       runtime_paths, bootstrap_opts);

    // Locate the wavetable package. If it's not installed, skip — the rest of
    // Phase 4's coverage (data model, emission, editor UX, breakouts) is
    // exercised by unit tests; this is the end-to-end audible-impact gate.
    bool have_wavetable = false;
    for (const auto& info : bootstrap.package_discovery.loaded_packages) {
        if (info.name == "vivid-wavetable") { have_wavetable = true; break; }
    }
    if (!registry.find("WavetableLayer") || !have_wavetable) {
        std::fprintf(stderr, "  SKIP: vivid-wavetable package not loaded — install it to run this end-to-end test\n");
        return 0;
    }

    // Resolve the source-tree graph file. Tests run from build/, so walk up to
    // the source root via the resolved bootstrap path.
    std::filesystem::path graph_path = std::filesystem::path(runtime_paths.source_dir) /
                                       "graphs" / "audio" / "tracker_expression_demo.json";
    if (!std::filesystem::exists(graph_path)) {
        // Fallback: build-bundled copy (configure_file in app.cmake).
        graph_path = std::filesystem::path(build_dir) / "graphs" / "audio" /
                     "tracker_expression_demo.json";
    }
    check(std::filesystem::exists(graph_path), "demo graph file present");

    vivid::Graph graph;
    bool loaded = graph.load(graph_path.string().c_str());
    check(loaded, "demo graph loads");
    if (!loaded) {
        std::fprintf(stderr, "\nResults: %d failed\n", failures);
        return 1;
    }
    registry.load_for_graph(graph);

    vivid::RuntimeCore runtime;
    runtime.set_audio_buffer_size(512);
    bool built = runtime.build(graph, registry);
    check(built, "runtime builds");
    if (!built) {
        std::fprintf(stderr, "\nResults: %d failed\n", failures);
        return 1;
    }
    check_graph_clean(runtime.compiled_graph(), "tracker expression demo");
    runtime.reset_live_metronome(graph.metronome(), 0.0);

    // No missing-operator placeholders — the wavetable package must resolve.
    if (const auto* cg = runtime.compiled_graph()) {
        for (const auto& node : cg->nodes) {
            if (node.missing_operator) {
                std::fprintf(stderr, "  FAIL: node '%s' (%s) unresolved: %s\n",
                             node.node_id.c_str(), node.type_name.c_str(),
                             node.missing_operator_reason.c_str());
                ++failures;
            }
        }
    }

    vivid::AudioEngine audio;
    bool audio_ok = audio.build(runtime);
    check(audio_ok, "audio engine builds");
    if (!audio_ok) {
        std::fprintf(stderr, "\nResults: %d failed\n", failures);
        return 1;
    }
    bool started = audio.start(true);  // null device
    check(started, "audio engine starts (null device)");

    // Render ~1.6 seconds. At 96 BPM with rate=4 (1/16) and 6 ticks/row, one
    // row is 60/96/4 = 0.156 s. The 8-row pattern spans 1.25 s. We render a
    // bit longer to capture the post-row-7 ringout.
    const uint32_t buf_frames = audio.buffer_size();        // 512
    const uint32_t sample_rate = 48000;                     // null-device default
    const double seconds = 1.6;
    const size_t total_buffers = static_cast<size_t>(
        std::ceil(seconds * sample_rate / static_cast<double>(buf_frames)));

    // Capture the master stereo signal across all buffers so we can slice it
    // into "pre-pressure" and "peak-pressure" segments for analysis.
    std::vector<float> master_left;
    master_left.reserve(total_buffers * buf_frames);

    std::vector<float> audio_buf(buf_frames * 2, 0.0f);
    for (uint64_t frame = 0; frame < total_buffers; ++frame) {
        const double t = frame * (static_cast<double>(buf_frames) / sample_rate);
        runtime.pre_tick_audio_sync(t);
        runtime.tick(t, static_cast<double>(buf_frames) / sample_rate, frame);
        runtime.post_tick_audio_sync();
        std::fill(audio_buf.begin(), audio_buf.end(), 0.0f);
        audio.process_audio_for_test(audio_buf.data(), buf_frames);
        for (uint32_t i = 0; i < buf_frames; ++i) {
            master_left.push_back(audio_buf[i * 2 + 0]);
        }
    }

    // Pull engine analysis snapshots back across the bridge so we can read
    // the per-node peaks below.
    runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());

    const auto& analysis = audio.analysis_read();
    int synth_idx = audio.audio_node_index("synth");
    check(synth_idx >= 0, "WavetableLayer node found in audio engine");
    if (synth_idx >= 0) {
        const float synth_peak = analysis.peak[synth_idx][0];
        std::fprintf(stderr, "    synth peak=%.6f\n", synth_peak);
        check(synth_peak > 0.001f, "WavetableLayer produces audible audio");
    }

    // Master-side non-silent check.
    const float master_peak = *std::max_element(master_left.begin(), master_left.end(),
        [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    std::fprintf(stderr, "    master peak (left) = %.6f\n", std::fabs(master_peak));
    check(std::fabs(master_peak) > 0.001f, "master output is non-silent");

    // Slice the captured signal:
    //   "early" = first 0.30 s — note 1 (C4) at row 0, before the pressure ramp.
    //   "mid"   = 0.55..0.85 s — around row 4 (G4 + pressure peak + timbre rising).
    auto slice = [&](double start_s, double end_s) -> std::pair<size_t, size_t> {
        size_t s = static_cast<size_t>(start_s * sample_rate);
        size_t e = static_cast<size_t>(end_s * sample_rate);
        if (e > master_left.size()) e = master_left.size();
        if (s > e) s = e;
        return {s, e};
    };
    auto [e_s, e_e] = slice(0.05, 0.30);
    auto [m_s, m_e] = slice(0.55, 0.85);

    const float early_rms        = rms(master_left.data() + e_s, e_e - e_s);
    const float mid_rms          = rms(master_left.data() + m_s, m_e - m_s);
    const float early_brightness = rough_brightness(master_left.data() + e_s, e_e - e_s);
    const float mid_brightness   = rough_brightness(master_left.data() + m_s, m_e - m_s);

    std::fprintf(stderr, "    early window  RMS=%.6f  brightness=%.6f  (samples=%zu)\n",
                 early_rms, early_brightness, e_e - e_s);
    std::fprintf(stderr, "    mid window    RMS=%.6f  brightness=%.6f  (samples=%zu)\n",
                 mid_rms,   mid_brightness,   m_e - m_s);

    // Pressure → amplitude: the mid window (peak pressure) should be
    // measurably louder than the early window (pre-pressure). pressure_to_amp
    // is 0.6 in the demo graph and pressure peaks at ~0.498 (raw 16319/32767),
    // so the boost is roughly (1 + 0.6 * 0.498) / (1 + 0.6 * 0) ≈ 1.30x.
    // Allow a wide margin for envelope shape / note-to-note RMS variation.
    check(mid_rms > early_rms * 1.05f,
          "pressure swell measurably increases output level mid-pattern");

    // Timbre → wavetable position: the mid window (timbre rising) should have
    // a different spectral profile from the early window. The position offset
    // changes which wavetable slice is read, shifting harmonic content.
    // Direction depends on the wavetable's per-slice spectrum, so we just
    // require a meaningful change in either direction.
    const float brightness_delta = std::fabs(mid_brightness - early_brightness);
    std::fprintf(stderr, "    |Δ brightness| = %.6f\n", brightness_delta);
    check(brightness_delta > 0.005f,
          "expression shifts spectral profile mid-pattern (timbre/pressure modulation)");

    // No audio-side errors.
    bool any_error = false;
    for (size_t i = 0; i < analysis.errored.size(); ++i) {
        if (analysis.errored[i]) {
            std::fprintf(stderr, "  audio node %zu errored: %s\n", i,
                         analysis.error_msgs[i].data());
            any_error = true;
        }
    }
    check(!any_error, "no audio node errors during render");

    audio.shutdown();
    runtime.shutdown();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Results: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures > 0 ? 1 : 0;
}
