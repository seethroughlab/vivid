#include "runtime/debug/output_analyzer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "test_helpers.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void check_near(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (got %.6f, expected %.6f, tol %.6f)\n",
                     msg, actual, expected, tol);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.6f)\n", msg, actual);
    }
}

int main() {
    // =====================================================================
    // Audio: silence
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Audio: silence ===\n");
        std::vector<float> silence(48000 * 2, 0.0f); // 1s stereo @ 48kHz
        auto m = vivid::analyze_audio(silence.data(), silence.size(), 48000, 2);
        check_near(m.rms, 0.0f, 0.001f, "silence RMS ~ 0");
        check_near(m.peak, 0.0f, 0.001f, "silence peak ~ 0");
    }

    // =====================================================================
    // Audio: sine wave
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Audio: 440Hz sine wave ===\n");
        uint32_t rate = 48000;
        uint64_t num_samples = rate; // 1 second mono
        std::vector<float> sine(num_samples);
        for (uint64_t i = 0; i < num_samples; ++i)
            sine[i] = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / rate);

        auto m = vivid::analyze_audio(sine.data(), num_samples, rate, 1);
        // RMS of 0.5 * sin = 0.5 / sqrt(2) ~ 0.3536
        check_near(m.rms, 0.3536f, 0.01f, "sine RMS ~ 0.354");
        check_near(m.peak, 0.5f, 0.01f, "sine peak ~ 0.5");
        check(m.spectral_centroid_hz > 400.0f && m.spectral_centroid_hz < 500.0f,
              "sine centroid near 440Hz");
        check(m.spectral_flatness < 0.2f, "sine spectral flatness low (tonal)");
    }

    // =====================================================================
    // Audio: white noise (pseudo)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Audio: pseudo white noise ===\n");
        uint32_t rate = 48000;
        uint64_t num_samples = rate;
        std::vector<float> noise(num_samples);
        // Simple LCG for reproducible noise
        uint32_t seed = 12345;
        for (uint64_t i = 0; i < num_samples; ++i) {
            seed = seed * 1103515245u + 12345u;
            noise[i] = (static_cast<float>(seed & 0xFFFF) / 32768.0f) - 1.0f;
        }

        auto m = vivid::analyze_audio(noise.data(), num_samples, rate, 1);
        check(m.rms > 0.3f, "noise has significant RMS");
        check(m.spectral_flatness > 0.3f, "noise spectral flatness higher than tonal");
    }

    // =====================================================================
    // Audio: zero-length
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Audio: zero-length ===\n");
        auto m = vivid::analyze_audio(nullptr, 0, 48000, 2);
        check_near(m.rms, 0.0f, 0.001f, "zero-length RMS = 0");
        check_near(m.peak, 0.0f, 0.001f, "zero-length peak = 0");
    }

    // =====================================================================
    // Visual: solid white
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Visual: solid white ===\n");
        uint32_t w = 4, h = 4;
        std::vector<uint8_t> pixels(w * h * 4, 255);
        auto m = vivid::analyze_frame(pixels.data(), w, h);
        check_near(m.mean_brightness, 1.0f, 0.01f, "white brightness ~ 1.0");
        check_near(m.contrast, 0.0f, 0.01f, "white contrast ~ 0.0");
    }

    // =====================================================================
    // Visual: solid black
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Visual: solid black ===\n");
        uint32_t w = 4, h = 4;
        std::vector<uint8_t> pixels(w * h * 4, 0);
        auto m = vivid::analyze_frame(pixels.data(), w, h);
        check_near(m.mean_brightness, 0.0f, 0.01f, "black brightness ~ 0.0");
        check_near(m.contrast, 0.0f, 0.01f, "black contrast ~ 0.0");
    }

    // =====================================================================
    // Visual: 50% gray
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Visual: 50%% gray ===\n");
        uint32_t w = 4, h = 4;
        std::vector<uint8_t> pixels(w * h * 4);
        for (uint32_t i = 0; i < w * h; ++i) {
            pixels[i * 4 + 0] = 128;
            pixels[i * 4 + 1] = 128;
            pixels[i * 4 + 2] = 128;
            pixels[i * 4 + 3] = 255;
        }
        auto m = vivid::analyze_frame(pixels.data(), w, h);
        // luminance = (0.2126 + 0.7152 + 0.0722) * 128/255 = 128/255 ~ 0.502
        check_near(m.mean_brightness, 0.502f, 0.02f, "gray brightness ~ 0.5");
        check_near(m.contrast, 0.0f, 0.01f, "uniform gray contrast ~ 0.0");
    }

    // =====================================================================
    // Visual: single pixel
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Visual: single pixel ===\n");
        uint8_t pixel[4] = {255, 0, 0, 255}; // red
        auto m = vivid::analyze_frame(pixel, 1, 1);
        // luminance of pure red = 0.2126
        check_near(m.mean_brightness, 0.2126f, 0.01f, "red pixel brightness ~ 0.213");
    }

    // =====================================================================
    // Visual: zero-size
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Visual: zero-size ===\n");
        auto m = vivid::analyze_frame(nullptr, 0, 0);
        check_near(m.mean_brightness, 0.0f, 0.001f, "zero-size brightness = 0");
    }

    // =====================================================================
    // Motion: identical frames
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Motion: identical frames ===\n");
        uint32_t w = 4, h = 4;
        std::vector<uint8_t> pixels(w * h * 4, 128);
        float motion = vivid::compute_motion(pixels.data(), pixels.data(), w, h);
        check_near(motion, 0.0f, 0.001f, "identical frames motion ~ 0");
    }

    // =====================================================================
    // Motion: black vs white
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Motion: black vs white ===\n");
        uint32_t w = 4, h = 4;
        std::vector<uint8_t> black(w * h * 4, 0);
        std::vector<uint8_t> white(w * h * 4, 255);
        float motion = vivid::compute_motion(black.data(), white.data(), w, h);
        check_near(motion, 1.0f, 0.01f, "black vs white motion ~ 1.0");
    }

    // =====================================================================
    // Comparison: directional deltas
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Comparison: directional deltas ===\n");
        vivid::AnalysisResult a, b;
        a.mode = vivid::AnalysisMode::AV;
        b.mode = vivid::AnalysisMode::AV;

        a.audio.rms = 0.1f;
        b.audio.rms = 0.5f;
        a.visual.mean_brightness = 0.8f;
        b.visual.mean_brightness = 0.2f;
        a.visual.contrast = 0.1f;
        b.visual.contrast = 0.3f;
        a.visual.motion_magnitude = 0.0f;
        b.visual.motion_magnitude = 0.5f;
        a.av_reactivity.energy_brightness_correlation = 0.1f;
        b.av_reactivity.energy_brightness_correlation = 0.9f;

        auto r = vivid::compare_analyses(a, b);
        check(r.deltas.size() >= 5, "at least 5 deltas for AV mode");

        // Find specific deltas by label
        bool found_louder = false, found_darker = false, found_more_motion = false;
        for (const auto& d : r.deltas) {
            if (d.label == "louder") found_louder = true;
            if (d.label == "darker") found_darker = true;
            if (d.label == "more_motion") found_more_motion = true;
        }
        check(found_louder, "b is louder than a");
        check(found_darker, "b is darker than a");
        check(found_more_motion, "b has more motion than a");
    }

    // =====================================================================
    // AV Reactivity: brightness correlation (audio energy ramps up,
    // brightness ramps up — should give strong positive correlation)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: brightness correlation ===\n");
        uint32_t rate = 48000;
        float window = 1.0f;
        uint64_t num_samples = rate;
        std::vector<float> audio(num_samples);
        for (uint64_t i = 0; i < num_samples; ++i)
            audio[i] = 0.5f * static_cast<float>(i) / num_samples;

        // Visual time series: brightness ramps up linearly across the window
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 10; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 9.0f;
            s.brightness = static_cast<float>(i) / 9.0f;
            s.contrast = 0.0f;
            s.motion = 0.0f;
            visual.push_back(s);
        }

        auto m = vivid::analyze_av_reactivity(
            audio.data(), num_samples, rate, 1, visual, window);

        check(m.energy_brightness_correlation > 0.9f,
              "energy + brightness ramp give strong positive correlation");
        check_near(m.window_seconds, 1.0f, 0.001f, "window_seconds preserved");
        check(m.visual_samples == 10, "visual_samples count reflects time series");
    }

    // =====================================================================
    // AV Reactivity: motion-only correlation (constant brightness, only
    // motion tracks audio energy — exercises the case Phase 0 surfaced
    // where displacement-driven reactivity was invisible to the brightness
    // metric)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: motion-only correlation ===\n");
        uint32_t rate = 48000;
        float window = 1.0f;
        uint64_t num_samples = rate;
        std::vector<float> audio(num_samples);
        for (uint64_t i = 0; i < num_samples; ++i)
            audio[i] = 0.5f * static_cast<float>(i) / num_samples;

        // Brightness flat; motion ramps with audio energy
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 10; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 9.0f;
            s.brightness = 0.5f;  // constant
            s.contrast = 0.5f;    // constant
            s.motion = static_cast<float>(i) / 9.0f;  // ramps
            visual.push_back(s);
        }

        auto m = vivid::analyze_av_reactivity(
            audio.data(), num_samples, rate, 1, visual, window);

        check(std::fabs(m.energy_brightness_correlation) < 0.1f,
              "flat brightness gives near-zero brightness correlation");
        check(m.energy_motion_correlation > 0.9f,
              "energy + motion ramp give strong positive motion correlation");
    }

    // =====================================================================
    // Onset detection: pulses at known intervals
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Onset detection: 4 pulses ===\n");
        uint32_t rate = 48000;
        // 2 seconds; pulses at 0.3, 0.8, 1.3, 1.8 sec
        std::vector<float> audio(rate * 2, 0.0f);
        const float pulse_times[] = {0.3f, 0.8f, 1.3f, 1.8f};
        for (float t : pulse_times) {
            uint64_t i0 = static_cast<uint64_t>(t * rate);
            // 50ms decaying sine burst at 1kHz
            for (uint32_t i = 0; i < rate / 20; ++i) {
                if (i0 + i >= audio.size()) break;
                float env = std::exp(-static_cast<float>(i) / (rate / 100.0f));
                audio[i0 + i] = 0.7f * env * std::sin(2.0f * M_PI * 1000.0f * i / rate);
            }
        }
        auto onsets = vivid::detect_audio_onsets(audio.data(), audio.size(), rate, 1);
        std::fprintf(stderr, "  detected %zu onsets at:", onsets.size());
        for (float o : onsets) std::fprintf(stderr, " %.3f", o);
        std::fprintf(stderr, "\n");
        check(onsets.size() == 4, "detected exactly 4 onsets");
        if (onsets.size() == 4) {
            for (int i = 0; i < 4; ++i)
                check_near(onsets[i], pulse_times[i], 0.05f,
                           "onset i within 50ms of expected");
        }
    }

    // =====================================================================
    // Onset detection: silence → no onsets
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Onset detection: silence ===\n");
        std::vector<float> silence(48000, 0.0f);
        auto onsets = vivid::detect_audio_onsets(silence.data(), silence.size(), 48000, 1);
        check(onsets.empty(), "silence produces zero onsets");
    }

    // =====================================================================
    // AV Reactivity: onset response rate (onsets + correlated visual peaks)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: onset response rate ===\n");
        uint32_t rate = 48000;
        float window = 2.0f;
        std::vector<float> audio(static_cast<uint64_t>(rate * window), 0.0f);
        const float pulse_times[] = {0.3f, 0.8f, 1.3f, 1.8f};
        for (float t : pulse_times) {
            uint64_t i0 = static_cast<uint64_t>(t * rate);
            for (uint32_t i = 0; i < rate / 20; ++i) {
                if (i0 + i >= audio.size()) break;
                float env = std::exp(-static_cast<float>(i) / (rate / 100.0f));
                audio[i0 + i] = 0.7f * env * std::sin(2.0f * M_PI * 1000.0f * i / rate);
            }
        }

        // Visual: brightness ramps up after each onset (decays back to baseline
        // by next onset, so brightness is highly responsive to each pulse).
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 40; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 39.0f;
            s.brightness = 0.05f;
            for (float p : pulse_times) {
                float dt = s.timestamp_seconds - p;
                if (dt >= 0.0f && dt < 0.3f)
                    s.brightness = std::max(s.brightness, 0.05f + 0.5f * std::exp(-dt / 0.1f));
            }
            s.contrast = 0.0f;
            s.motion = (i > 0 ? std::fabs(s.brightness - visual.back().brightness) : 0.0f);
            visual.push_back(s);
        }

        auto m = vivid::analyze_av_reactivity(audio.data(), audio.size(), rate, 1, visual, window);
        std::fprintf(stderr, "  detected_onsets=%u response_rate=%.3f latency_ms=%.1f\n",
                     m.detected_onsets, m.onset_response_rate, m.reactivity_latency_ms);
        check(m.detected_onsets >= 3, "detected at least 3 of 4 onsets");
        check(m.onset_response_rate > 0.7f,
              "responsive visual gives onset_response_rate > 0.7");
        check(m.reactivity_latency_ms > 0.0f && m.reactivity_latency_ms < 200.0f,
              "median latency in 0-200ms range");
    }

    // =====================================================================
    // AV Reactivity: onset response rate — silent visual after onsets
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: onsets + flat visual ===\n");
        uint32_t rate = 48000;
        float window = 2.0f;
        std::vector<float> audio(static_cast<uint64_t>(rate * window), 0.0f);
        const float pulse_times[] = {0.3f, 0.8f, 1.3f, 1.8f};
        for (float t : pulse_times) {
            uint64_t i0 = static_cast<uint64_t>(t * rate);
            for (uint32_t i = 0; i < rate / 20; ++i) {
                if (i0 + i >= audio.size()) break;
                float env = std::exp(-static_cast<float>(i) / (rate / 100.0f));
                audio[i0 + i] = 0.7f * env * std::sin(2.0f * M_PI * 1000.0f * i / rate);
            }
        }
        // Flat visual — no response
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 40; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 39.0f;
            s.brightness = 0.5f;
            s.contrast = 0.5f;
            s.motion = 0.0f;
            visual.push_back(s);
        }
        auto m = vivid::analyze_av_reactivity(audio.data(), audio.size(), rate, 1, visual, window);
        std::fprintf(stderr, "  detected_onsets=%u response_rate=%.3f\n",
                     m.detected_onsets, m.onset_response_rate);
        check(m.detected_onsets >= 3, "still detects onsets");
        check_near(m.onset_response_rate, 0.0f, 0.01f,
                   "flat visual → zero onset response rate");
    }

    // =====================================================================
    // AV Reactivity: per-band — bass and treble with OPPOSING envelopes.
    // Brightness tracks the bass envelope. We expect bass→brightness strongly
    // positive, treble→brightness strongly negative (anti-correlated) since
    // treble fades as brightness rises.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: per-band (opposing bass/treble envelopes) ===\n");
        uint32_t rate = 48000;
        float window = 2.0f;
        uint64_t num_samples = static_cast<uint64_t>(rate * window);
        // Bass ramps UP, treble ramps DOWN, simultaneously in the same buffer.
        std::vector<float> audio(num_samples);
        for (uint64_t i = 0; i < num_samples; ++i) {
            float env_bass   = static_cast<float>(i) / num_samples;        // 0 → 1
            float env_treble = 1.0f - static_cast<float>(i) / num_samples; // 1 → 0
            audio[i] = 0.5f * env_bass   * std::sin(2.0f * M_PI *   80.0f * i / rate)
                     + 0.5f * env_treble * std::sin(2.0f * M_PI * 6000.0f * i / rate);
        }
        // Brightness ramps UP, following the bass envelope.
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 20; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 19.0f;
            s.brightness = static_cast<float>(i) / 19.0f;
            s.contrast = 0.5f;
            s.motion = 0.0f;
            visual.push_back(s);
        }
        auto m = vivid::analyze_av_reactivity(audio.data(), audio.size(),
                                              rate, 1, visual, window);
        std::fprintf(stderr, "  bass→brightness=%.2f mid→brightness=%.2f treble→brightness=%.2f\n",
                     m.band_brightness_correlations.bass,
                     m.band_brightness_correlations.mid,
                     m.band_brightness_correlations.treble);
        check(m.band_brightness_correlations.bass > 0.7f,
              "bass envelope rises with brightness → positive bass→brightness");
        check(m.band_brightness_correlations.treble < -0.7f,
              "treble envelope falls as brightness rises → negative treble→brightness");
    }

    // =====================================================================
    // AV Reactivity: per-band — band-selective motion coupling.
    // Audio has bass all the time but a treble burst in the second half.
    // Visual motion only rises during the treble burst. Expect treble→motion
    // strongly positive while bass→motion stays flat.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: per-band (selective treble→motion) ===\n");
        uint32_t rate = 48000;
        float window = 2.0f;
        uint64_t num_samples = static_cast<uint64_t>(rate * window);
        std::vector<float> audio(num_samples);
        for (uint64_t i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / num_samples;
            float bass = 0.4f * std::sin(2.0f * M_PI * 80.0f * i / rate);
            // Treble is only present in the second half (t >= 0.5) and ramps.
            float treble_env = (t < 0.5f) ? 0.0f : 2.0f * (t - 0.5f);
            float treble = 0.4f * treble_env * std::sin(2.0f * M_PI * 6000.0f * i / rate);
            audio[i] = bass + treble;
        }
        // Motion is zero in the first half, ramps in the second half.
        std::vector<vivid::VisualSample> visual;
        for (int i = 0; i < 20; ++i) {
            vivid::VisualSample s;
            s.timestamp_seconds = window * i / 19.0f;
            s.brightness = 0.5f;
            s.contrast = 0.5f;
            float t = i / 19.0f;
            s.motion = (t < 0.5f) ? 0.0f : 2.0f * (t - 0.5f);
            visual.push_back(s);
        }
        auto m = vivid::analyze_av_reactivity(audio.data(), audio.size(),
                                              rate, 1, visual, window);
        std::fprintf(stderr, "  bass→motion=%.2f mid→motion=%.2f treble→motion=%.2f\n",
                     m.band_motion_correlations.bass,
                     m.band_motion_correlations.mid,
                     m.band_motion_correlations.treble);
        check(m.band_motion_correlations.treble > 0.7f,
              "treble burst + motion burst → strong treble→motion");
        // Bass→motion should be weaker than treble→motion (bass is constant-ish)
        check(m.band_motion_correlations.treble > m.band_motion_correlations.bass + 0.2f,
              "treble→motion should exceed bass→motion when motion aligns with treble burst");
    }

    // =====================================================================
    // AV Reactivity: empty / degenerate inputs
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: degenerate inputs ===\n");
        std::vector<vivid::VisualSample> empty;
        std::vector<float> audio(48000, 0.5f);
        auto m = vivid::analyze_av_reactivity(audio.data(), audio.size(), 48000, 1, empty, 1.0f);
        check_near(m.energy_brightness_correlation, 0.0f, 0.001f,
                   "empty visual series → zero brightness correlation");
        check_near(m.energy_motion_correlation, 0.0f, 0.001f,
                   "empty visual series → zero motion correlation");
        check(m.visual_samples == 0, "visual_samples = 0 for empty input");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
