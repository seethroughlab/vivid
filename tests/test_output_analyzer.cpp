#include "runtime/output_analyzer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

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
    // AV Reactivity: basic correlation
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== AV Reactivity: basic ===\n");
        // Create correlated audio and frames: loud audio + bright frame
        uint32_t rate = 48000;
        float window = 1.0f;
        uint64_t num_samples = rate; // 1 second mono
        std::vector<float> audio(num_samples);
        // Audio ramps up over 1 second
        for (uint64_t i = 0; i < num_samples; ++i)
            audio[i] = 0.5f * static_cast<float>(i) / num_samples;

        // Frame A: dark, Frame B: bright (correlated with ramping audio)
        uint32_t w = 2, h = 2;
        std::vector<uint8_t> dark(w * h * 4, 0);
        std::vector<uint8_t> bright(w * h * 4, 255);

        auto m = vivid::analyze_av_reactivity(
            audio.data(), num_samples, rate, 1,
            dark.data(), bright.data(), w, h, window);

        check(m.energy_brightness_correlation > 0.5f,
              "correlated audio+brightness gives positive correlation");
        check_near(m.window_seconds, 1.0f, 0.001f, "window_seconds preserved");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
