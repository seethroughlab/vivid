#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

enum class AnalysisMode { Frame, Audio, AV };

struct AudioMetrics {
    float rms = 0.0f;
    float peak = 0.0f;
    float spectral_centroid_hz = 0.0f;
    float spectral_brightness = 0.0f;  // energy ratio above 4kHz
    float spectral_flatness = 0.0f;    // 0=tonal, 1=noise-like
};

struct VisualMetrics {
    float mean_brightness = 0.0f;  // luminance average, 0-1
    float contrast = 0.0f;        // luminance std deviation
    float motion_magnitude = 0.0f; // mean abs pixel diff, 0-1
};

struct AVReactivityMetrics {
    float energy_brightness_correlation = 0.0f;  // Pearson r, -1 to +1
    float window_seconds = 0.0f;
};

struct AnalysisResult {
    AnalysisMode mode = AnalysisMode::Frame;
    AudioMetrics audio;
    VisualMetrics visual;
    AVReactivityMetrics av_reactivity;
    std::vector<std::string> notes;
};

struct ComparisonDelta {
    std::string label;
    float magnitude = 0.0f;
};

struct ComparisonResult {
    AnalysisMode mode = AnalysisMode::Frame;
    std::vector<ComparisonDelta> deltas;
    std::vector<std::string> notes;
};

// Pure analysis functions (no runtime deps)
AudioMetrics analyze_audio(const float* samples, uint64_t count,
                           uint32_t rate, uint16_t channels);

VisualMetrics analyze_frame(const uint8_t* rgba, uint32_t w, uint32_t h);

float compute_motion(const uint8_t* rgba_a, const uint8_t* rgba_b,
                     uint32_t w, uint32_t h);

AVReactivityMetrics analyze_av_reactivity(const float* audio, uint64_t count,
                                          uint32_t rate, uint16_t channels,
                                          const uint8_t* frame_a,
                                          const uint8_t* frame_b,
                                          uint32_t w, uint32_t h,
                                          float window_seconds);

ComparisonResult compare_analyses(const AnalysisResult& a,
                                  const AnalysisResult& b);

} // namespace vivid
