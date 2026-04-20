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

// One sampled visual frame within an analysis window. `motion` is the
// inter-sample frame delta (mean abs luminance diff vs the previous sample);
// it is 0 for the first sample.
struct VisualSample {
    float timestamp_seconds = 0.0f;
    float brightness = 0.0f;
    float contrast = 0.0f;
    float motion = 0.0f;
};

// Per-band (bass / mid / treble) correlations against a single visual axis.
// Bands: bass < 250 Hz, mid 250–2000 Hz, treble > 2000 Hz.
struct BandCorrelations {
    float bass = 0.0f;     // Pearson r, -1 to +1
    float mid = 0.0f;
    float treble = 0.0f;
};

struct AVReactivityMetrics {
    float energy_brightness_correlation = 0.0f;  // Pearson r, -1 to +1
    float energy_motion_correlation = 0.0f;      // Pearson r, -1 to +1
    float energy_contrast_correlation = 0.0f;    // Pearson r, -1 to +1
    float window_seconds = 0.0f;
    uint32_t visual_samples = 0;                 // number of frames in series

    // Onset-aligned reactivity (works where Pearson correlation breaks down —
    // smoothed/feedback-rich/percussive graphs). For each detected audio onset,
    // measures whether the visual changed within `max_latency_seconds` after
    // the onset.
    uint32_t detected_onsets = 0;
    float onset_response_rate = 0.0f;            // 0-1: responsive_onsets / detected_onsets
    float reactivity_latency_ms = 0.0f;          // median onset→peak latency, ms

    // Per-band correlations — lets callers see which frequency range drives
    // which visual axis. E.g., "bass drives brightness but treble doesn't"
    // is a useful design observation that the overall correlation hides.
    BandCorrelations band_brightness_correlations;
    BandCorrelations band_motion_correlations;
    BandCorrelations band_contrast_correlations;
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

// Correlates audio energy (chunked RMS) against a time series of visual
// samples on three axes: brightness, motion, contrast. The visual samples
// are interpolated to the audio chunk timestamps before correlation.
//
// Also runs spectral-flux onset detection on the audio and measures, for
// each detected onset, whether the visual sample series shows a change
// within `max_latency_seconds`. Together with the per-axis correlations
// these handle both continuous reactivity (correlation) and event-driven
// reactivity (onset response rate).
//
// Returns zeros if either time series is too short to analyze.
AVReactivityMetrics analyze_av_reactivity(const float* audio, uint64_t count,
                                          uint32_t rate, uint16_t channels,
                                          const std::vector<VisualSample>& visual,
                                          float window_seconds);

// Detect audio onsets via spectral flux. Returns timestamps (seconds from the
// start of the audio buffer) at which a percussive/transient event begins.
// Exposed for testing and so callers can run onset analysis independently
// of the full reactivity pipeline.
std::vector<float> detect_audio_onsets(const float* audio, uint64_t count,
                                       uint32_t rate, uint16_t channels);

ComparisonResult compare_analyses(const AnalysisResult& a,
                                  const AnalysisResult& b);

} // namespace vivid
