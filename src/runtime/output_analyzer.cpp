#include "runtime/output_analyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid {

// ---------------------------------------------------------------------------
// Radix-2 Cooley-Tukey FFT (1024-point, in-place)
// Adapted from operators/control/fft_analysis/fft_analysis.cpp
// ---------------------------------------------------------------------------

static void fft_radix2(float* re, float* im, uint32_t N) {
    // Compute log2(N)
    uint32_t log2N = 0;
    for (uint32_t tmp = N; tmp > 1; tmp >>= 1) ++log2N;

    // Bit-reversal permutation
    for (uint32_t i = 0; i < N; ++i) {
        uint32_t j = 0;
        for (uint32_t b = 0; b < log2N; ++b)
            j |= ((i >> b) & 1) << (log2N - 1 - b);
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    // Butterfly stages
    for (uint32_t s = 1; s <= log2N; ++s) {
        uint32_t m = 1u << s;
        float wm_re = std::cos(-2.0f * static_cast<float>(M_PI) / m);
        float wm_im = std::sin(-2.0f * static_cast<float>(M_PI) / m);
        for (uint32_t k = 0; k < N; k += m) {
            float w_re = 1.0f, w_im = 0.0f;
            for (uint32_t j = 0; j < m / 2; ++j) {
                uint32_t t_idx = k + j + m / 2;
                uint32_t u_idx = k + j;
                float t_re = w_re * re[t_idx] - w_im * im[t_idx];
                float t_im = w_re * im[t_idx] + w_im * re[t_idx];
                re[t_idx] = re[u_idx] - t_re;
                im[t_idx] = im[u_idx] - t_im;
                re[u_idx] = re[u_idx] + t_re;
                im[u_idx] = im[u_idx] + t_im;
                float new_w_re = w_re * wm_re - w_im * wm_im;
                float new_w_im = w_re * wm_im + w_im * wm_re;
                w_re = new_w_re;
                w_im = new_w_im;
            }
        }
    }
}

static constexpr uint32_t kFFTSize = 1024;

// ---------------------------------------------------------------------------
// analyze_audio
// ---------------------------------------------------------------------------

AudioMetrics analyze_audio(const float* samples, uint64_t count,
                           uint32_t rate, uint16_t channels) {
    AudioMetrics m{};
    if (!samples || count == 0) return m;

    // Mix to mono
    uint64_t mono_count = count;
    std::vector<float> mono;
    if (channels >= 2) {
        mono_count = count / channels;
        mono.resize(mono_count);
        for (uint64_t i = 0; i < mono_count; ++i) {
            float sum = 0.0f;
            for (uint16_t c = 0; c < channels; ++c)
                sum += samples[i * channels + c];
            mono[i] = sum / channels;
        }
    } else {
        mono.assign(samples, samples + count);
    }

    // RMS and peak
    double sum_sq = 0.0;
    float peak = 0.0f;
    for (uint64_t i = 0; i < mono_count; ++i) {
        float v = mono[i];
        sum_sq += static_cast<double>(v) * v;
        float av = std::fabs(v);
        if (av > peak) peak = av;
    }
    m.rms = static_cast<float>(std::sqrt(sum_sq / mono_count));
    m.peak = peak;

    // FFT for spectral metrics
    float re[kFFTSize] = {};
    float im[kFFTSize] = {};

    // Copy up to kFFTSize samples, apply Hann window
    uint32_t copy_len = static_cast<uint32_t>(std::min(
        static_cast<uint64_t>(kFFTSize), mono_count));
    for (uint32_t i = 0; i < copy_len; ++i) {
        float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (kFFTSize - 1)));
        re[i] = mono[i] * w;
    }

    fft_radix2(re, im, kFFTSize);

    // Magnitude spectrum (first N/2 bins)
    uint32_t num_bins = kFFTSize / 2;
    std::vector<float> mag(num_bins);
    for (uint32_t i = 0; i < num_bins; ++i)
        mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);

    // Spectral centroid: weighted mean of bin frequencies
    float bin_hz = static_cast<float>(rate) / kFFTSize;
    double weighted_sum = 0.0, total_energy = 0.0;
    for (uint32_t i = 1; i < num_bins; ++i) {
        float freq = i * bin_hz;
        double energy = static_cast<double>(mag[i]) * mag[i];
        weighted_sum += freq * energy;
        total_energy += energy;
    }
    m.spectral_centroid_hz = (total_energy > 1e-12)
        ? static_cast<float>(weighted_sum / total_energy) : 0.0f;

    // Spectral brightness: energy ratio above 4kHz
    // Bin index for 4kHz = 4000 / bin_hz
    uint32_t brightness_bin = static_cast<uint32_t>(4000.0f / bin_hz);
    if (brightness_bin < num_bins) {
        double high_energy = 0.0;
        for (uint32_t i = brightness_bin; i < num_bins; ++i) {
            double e = static_cast<double>(mag[i]) * mag[i];
            high_energy += e;
        }
        m.spectral_brightness = (total_energy > 1e-12)
            ? static_cast<float>(high_energy / total_energy) : 0.0f;
    }

    // Spectral flatness: geometric mean / arithmetic mean of magnitudes
    // Use log-domain for numerical stability
    double log_sum = 0.0;
    double arith_sum = 0.0;
    uint32_t valid_bins = 0;
    for (uint32_t i = 1; i < num_bins; ++i) {
        if (mag[i] > 1e-12f) {
            log_sum += std::log(static_cast<double>(mag[i]));
            valid_bins++;
        }
        arith_sum += mag[i];
    }
    if (valid_bins > 0 && arith_sum > 1e-12) {
        double geo_mean = std::exp(log_sum / valid_bins);
        double ari_mean = arith_sum / (num_bins - 1);
        m.spectral_flatness = static_cast<float>(
            std::min(1.0, geo_mean / ari_mean));
    }

    return m;
}

// ---------------------------------------------------------------------------
// analyze_frame
// ---------------------------------------------------------------------------

static float rgba_to_luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
}

VisualMetrics analyze_frame(const uint8_t* rgba, uint32_t w, uint32_t h) {
    VisualMetrics m{};
    if (!rgba || w == 0 || h == 0) return m;

    uint64_t pixel_count = static_cast<uint64_t>(w) * h;

    // Compute mean brightness (luminance)
    double sum = 0.0;
    for (uint64_t i = 0; i < pixel_count; ++i) {
        const uint8_t* p = rgba + i * 4;
        sum += rgba_to_luminance(p[0], p[1], p[2]);
    }
    m.mean_brightness = static_cast<float>(sum / pixel_count);

    // Compute contrast (stddev of luminance)
    double sum_sq_diff = 0.0;
    for (uint64_t i = 0; i < pixel_count; ++i) {
        const uint8_t* p = rgba + i * 4;
        float lum = rgba_to_luminance(p[0], p[1], p[2]);
        double diff = lum - m.mean_brightness;
        sum_sq_diff += diff * diff;
    }
    m.contrast = static_cast<float>(std::sqrt(sum_sq_diff / pixel_count));

    return m;
}

// ---------------------------------------------------------------------------
// compute_motion
// ---------------------------------------------------------------------------

float compute_motion(const uint8_t* rgba_a, const uint8_t* rgba_b,
                     uint32_t w, uint32_t h) {
    if (!rgba_a || !rgba_b || w == 0 || h == 0) return 0.0f;

    uint64_t pixel_count = static_cast<uint64_t>(w) * h;
    double sum_diff = 0.0;
    for (uint64_t i = 0; i < pixel_count; ++i) {
        float lum_a = rgba_to_luminance(rgba_a[i * 4], rgba_a[i * 4 + 1], rgba_a[i * 4 + 2]);
        float lum_b = rgba_to_luminance(rgba_b[i * 4], rgba_b[i * 4 + 1], rgba_b[i * 4 + 2]);
        sum_diff += std::fabs(lum_a - lum_b);
    }
    return static_cast<float>(sum_diff / pixel_count);
}

// ---------------------------------------------------------------------------
// analyze_av_reactivity
// ---------------------------------------------------------------------------

AVReactivityMetrics analyze_av_reactivity(const float* audio, uint64_t count,
                                          uint32_t rate, uint16_t channels,
                                          const uint8_t* frame_a,
                                          const uint8_t* frame_b,
                                          uint32_t w, uint32_t h,
                                          float window_seconds) {
    AVReactivityMetrics m{};
    m.window_seconds = window_seconds;

    if (!audio || count == 0 || !frame_a || !frame_b || w == 0 || h == 0)
        return m;

    // Mix audio to mono
    uint64_t mono_count = (channels >= 2) ? count / channels : count;
    std::vector<float> mono(mono_count);
    if (channels >= 2) {
        for (uint64_t i = 0; i < mono_count; ++i) {
            float sum = 0.0f;
            for (uint16_t c = 0; c < channels; ++c)
                sum += audio[i * channels + c];
            mono[i] = sum / channels;
        }
    } else {
        for (uint64_t i = 0; i < mono_count; ++i)
            mono[i] = audio[i];
    }

    // Chunk audio into ~50ms windows, compute per-window RMS
    uint32_t chunk_samples = rate / 20;  // 50ms
    if (chunk_samples == 0) chunk_samples = 1;
    uint32_t num_chunks = static_cast<uint32_t>(mono_count / chunk_samples);
    if (num_chunks < 2) {
        // Not enough data for correlation
        return m;
    }

    std::vector<float> audio_rms(num_chunks);
    for (uint32_t c = 0; c < num_chunks; ++c) {
        double sq = 0.0;
        for (uint32_t s = 0; s < chunk_samples; ++s) {
            float v = mono[c * chunk_samples + s];
            sq += static_cast<double>(v) * v;
        }
        audio_rms[c] = static_cast<float>(std::sqrt(sq / chunk_samples));
    }

    // Compute brightness of frame A and frame B
    uint64_t pixel_count = static_cast<uint64_t>(w) * h;
    double sum_a = 0.0, sum_b = 0.0;
    for (uint64_t i = 0; i < pixel_count; ++i) {
        sum_a += rgba_to_luminance(frame_a[i * 4], frame_a[i * 4 + 1], frame_a[i * 4 + 2]);
        sum_b += rgba_to_luminance(frame_b[i * 4], frame_b[i * 4 + 1], frame_b[i * 4 + 2]);
    }
    float brightness_a = static_cast<float>(sum_a / pixel_count);
    float brightness_b = static_cast<float>(sum_b / pixel_count);

    // Interpolate brightness at each chunk timestamp
    std::vector<float> brightness(num_chunks);
    for (uint32_t c = 0; c < num_chunks; ++c) {
        float t = static_cast<float>(c) / (num_chunks - 1);
        brightness[c] = brightness_a + t * (brightness_b - brightness_a);
    }

    // Pearson correlation between audio RMS and brightness
    float mean_rms = 0.0f, mean_br = 0.0f;
    for (uint32_t c = 0; c < num_chunks; ++c) {
        mean_rms += audio_rms[c];
        mean_br += brightness[c];
    }
    mean_rms /= num_chunks;
    mean_br /= num_chunks;

    double cov = 0.0, var_rms = 0.0, var_br = 0.0;
    for (uint32_t c = 0; c < num_chunks; ++c) {
        double dr = audio_rms[c] - mean_rms;
        double db = brightness[c] - mean_br;
        cov += dr * db;
        var_rms += dr * dr;
        var_br += db * db;
    }

    double denom = std::sqrt(var_rms * var_br);
    m.energy_brightness_correlation = (denom > 1e-12)
        ? static_cast<float>(cov / denom) : 0.0f;

    return m;
}

// ---------------------------------------------------------------------------
// compare_analyses
// ---------------------------------------------------------------------------

static void add_delta(std::vector<ComparisonDelta>& deltas,
                      const std::string& higher_label,
                      const std::string& lower_label,
                      float a_val, float b_val) {
    float diff = b_val - a_val;
    ComparisonDelta d;
    d.label = (diff > 0) ? higher_label : lower_label;
    d.magnitude = std::fabs(diff);
    deltas.push_back(std::move(d));
}

ComparisonResult compare_analyses(const AnalysisResult& a,
                                  const AnalysisResult& b) {
    ComparisonResult r;
    r.mode = a.mode;

    if (a.mode == AnalysisMode::Audio || a.mode == AnalysisMode::AV) {
        add_delta(r.deltas, "louder", "quieter", a.audio.rms, b.audio.rms);
        add_delta(r.deltas, "brighter_spectral", "darker_spectral",
                  a.audio.spectral_brightness, b.audio.spectral_brightness);
    }

    if (a.mode == AnalysisMode::Frame || a.mode == AnalysisMode::AV) {
        add_delta(r.deltas, "brighter", "darker",
                  a.visual.mean_brightness, b.visual.mean_brightness);
        add_delta(r.deltas, "more_contrast", "less_contrast",
                  a.visual.contrast, b.visual.contrast);
        add_delta(r.deltas, "more_motion", "less_motion",
                  a.visual.motion_magnitude, b.visual.motion_magnitude);
    }

    if (a.mode == AnalysisMode::AV) {
        add_delta(r.deltas, "more_reactive", "less_reactive",
                  a.av_reactivity.energy_brightness_correlation,
                  b.av_reactivity.energy_brightness_correlation);
    }

    return r;
}

} // namespace vivid
