#include "runtime/debug/output_analyzer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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

// ---------------------------------------------------------------------------
// detect_audio_onsets — spectral-flux onset detection
// ---------------------------------------------------------------------------
//
// Algorithm (standard textbook spectral flux):
// 1. Mix audio to mono, chunk into 23ms frames with 50% overlap (so the
//    detection function has roughly 2x the resolution of the chunk rate).
// 2. Compute log-magnitude spectrum of each frame via Hann-windowed FFT.
// 3. Spectral flux SF[t] = sum_k max(0, log_mag[k,t] - log_mag[k,t-1]).
// 4. Smooth SF with a 3-tap moving average to reduce noise.
// 5. Adaptive threshold = max(min_floor, λ * local_median(SF in ±N taps)).
// 6. Peak-pick: SF[t] is an onset if SF[t] > threshold AND SF[t] is a local
//    maximum AND at least min_gap_taps since the last accepted onset.
// 7. Convert tap index → seconds.
//
// Tuned for percussive content; works on both drum hits and pitched onsets.

static constexpr uint32_t kOnsetFrameSize = 1024;
static constexpr float    kOnsetHopSec   = 0.0115f;  // 11.5ms hop ≈ 23ms / 2
static constexpr float    kOnsetMinGapSec = 0.05f;    // refractory period
static constexpr float    kOnsetThresholdLambda = 1.7f; // multiplier on local median
static constexpr float    kOnsetThresholdFloor  = 0.04f; // absolute minimum
static constexpr int      kOnsetMedianRadius = 6;     // ±6 taps ≈ ±70ms

std::vector<float> detect_audio_onsets(const float* audio, uint64_t count,
                                       uint32_t rate, uint16_t channels) {
    std::vector<float> onsets;
    if (!audio || count == 0 || rate == 0) return onsets;

    // Mix to mono
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
        for (uint64_t i = 0; i < mono_count; ++i) mono[i] = audio[i];
    }

    const uint32_t hop = static_cast<uint32_t>(kOnsetHopSec * rate);
    if (hop == 0 || mono_count < kOnsetFrameSize) return onsets;
    const uint32_t num_frames = static_cast<uint32_t>((mono_count - kOnsetFrameSize) / hop) + 1;
    if (num_frames < 3) return onsets;

    const uint32_t num_bins = kOnsetFrameSize / 2;
    std::vector<float> prev_log_mag(num_bins, 0.0f);
    std::vector<float> sf(num_frames, 0.0f);

    float re[kOnsetFrameSize];
    float im[kOnsetFrameSize];

    for (uint32_t f = 0; f < num_frames; ++f) {
        std::memset(re, 0, sizeof(re));
        std::memset(im, 0, sizeof(im));
        uint64_t base = static_cast<uint64_t>(f) * hop;
        for (uint32_t i = 0; i < kOnsetFrameSize; ++i) {
            float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (kOnsetFrameSize - 1)));
            re[i] = mono[base + i] * w;
        }
        fft_radix2(re, im, kOnsetFrameSize);

        float frame_sf = 0.0f;
        for (uint32_t k = 0; k < num_bins; ++k) {
            float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
            float log_mag = std::log(1.0f + mag);
            float diff = log_mag - prev_log_mag[k];
            if (diff > 0.0f) frame_sf += diff;
            prev_log_mag[k] = log_mag;
        }
        sf[f] = frame_sf;
    }

    if (num_frames < 5) return onsets;

    // 3-tap moving average smoothing
    std::vector<float> sf_smooth(num_frames, 0.0f);
    sf_smooth[0] = sf[0];
    sf_smooth[num_frames - 1] = sf[num_frames - 1];
    for (uint32_t f = 1; f < num_frames - 1; ++f)
        sf_smooth[f] = (sf[f - 1] + sf[f] + sf[f + 1]) / 3.0f;

    // Skip the first two frames (cold-start spectral flux is artificially high
    // because prev_log_mag is zero on the first frame).
    const uint32_t kColdStartFrames = 2;
    if (num_frames <= kColdStartFrames + 1) return onsets;

    // Peak-pick with adaptive threshold
    int last_accepted = -100000;
    int min_gap_taps = std::max(1, static_cast<int>(kOnsetMinGapSec / kOnsetHopSec));
    std::vector<float> window;
    window.reserve(2 * kOnsetMedianRadius + 1);

    for (uint32_t f = kColdStartFrames + 1; f + 1 < num_frames; ++f) {
        // Local median for adaptive threshold
        window.clear();
        int lo = std::max(static_cast<int>(kColdStartFrames),
                          static_cast<int>(f) - kOnsetMedianRadius);
        int hi = std::min(static_cast<int>(num_frames) - 1,
                          static_cast<int>(f) + kOnsetMedianRadius);
        for (int k = lo; k <= hi; ++k) window.push_back(sf_smooth[k]);
        std::nth_element(window.begin(),
                         window.begin() + window.size() / 2,
                         window.end());
        float local_median = window[window.size() / 2];

        float threshold = std::max(kOnsetThresholdFloor,
                                   kOnsetThresholdLambda * local_median);
        if (sf_smooth[f] <= threshold) continue;
        if (sf_smooth[f] <= sf_smooth[f - 1]) continue;
        if (sf_smooth[f] < sf_smooth[f + 1]) continue;
        if (static_cast<int>(f) - last_accepted < min_gap_taps) continue;

        onsets.push_back(static_cast<float>(f) * kOnsetHopSec);
        last_accepted = static_cast<int>(f);
    }

    return onsets;
}

// Pearson correlation between two equal-length series.
static float pearson_corr(const std::vector<float>& x, const std::vector<float>& y) {
    if (x.size() != y.size() || x.size() < 2) return 0.0f;
    double mean_x = 0.0, mean_y = 0.0;
    for (size_t i = 0; i < x.size(); ++i) { mean_x += x[i]; mean_y += y[i]; }
    mean_x /= x.size();
    mean_y /= y.size();
    double cov = 0.0, var_x = 0.0, var_y = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double dx = x[i] - mean_x;
        double dy = y[i] - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }
    double denom = std::sqrt(var_x * var_y);
    return (denom > 1e-12) ? static_cast<float>(cov / denom) : 0.0f;
}

// Linear interpolation of a sorted-by-timestamp series at an arbitrary t.
// Clamps to endpoints. Caller must guarantee samples is non-empty.
static float interp_at(const std::vector<VisualSample>& samples,
                       float t, float VisualSample::*field) {
    if (samples.size() == 1) return samples[0].*field;
    if (t <= samples.front().timestamp_seconds) return samples.front().*field;
    if (t >= samples.back().timestamp_seconds) return samples.back().*field;
    // Binary search for the interval
    size_t lo = 0, hi = samples.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (samples[mid].timestamp_seconds <= t) lo = mid;
        else hi = mid;
    }
    float t0 = samples[lo].timestamp_seconds;
    float t1 = samples[hi].timestamp_seconds;
    float v0 = samples[lo].*field;
    float v1 = samples[hi].*field;
    if (t1 - t0 < 1e-9f) return v0;
    float a = (t - t0) / (t1 - t0);
    return v0 + a * (v1 - v0);
}

AVReactivityMetrics analyze_av_reactivity(const float* audio, uint64_t count,
                                          uint32_t rate, uint16_t channels,
                                          const std::vector<VisualSample>& visual,
                                          float window_seconds) {
    AVReactivityMetrics m{};
    m.window_seconds = window_seconds;
    m.visual_samples = static_cast<uint32_t>(visual.size());

    if (!audio || count == 0 || visual.size() < 2 || rate == 0)
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
    if (num_chunks < 2) return m;

    std::vector<float> audio_rms(num_chunks);
    std::vector<float> audio_bass(num_chunks);
    std::vector<float> audio_mid(num_chunks);
    std::vector<float> audio_treble(num_chunks);
    std::vector<float> v_brightness(num_chunks);
    std::vector<float> v_motion(num_chunks);
    std::vector<float> v_contrast(num_chunks);

    float chunk_dur = static_cast<float>(chunk_samples) / static_cast<float>(rate);

    // Per-chunk FFT for band decomposition — 1024-point Hann-windowed transform.
    // Bass < 250 Hz, mid 250-2000 Hz, treble > 2000 Hz.
    constexpr uint32_t kBandFFTSize = 1024;
    constexpr uint32_t kBandNumBins = kBandFFTSize / 2;
    const float bin_hz = static_cast<float>(rate) / static_cast<float>(kBandFFTSize);
    const uint32_t bass_end_bin   = std::min(kBandNumBins,
        static_cast<uint32_t>(std::max(1.0f, 250.0f  / bin_hz)));
    const uint32_t mid_end_bin    = std::min(kBandNumBins,
        static_cast<uint32_t>(std::max(static_cast<float>(bass_end_bin + 1), 2000.0f / bin_hz)));
    float re[kBandFFTSize];
    float im[kBandFFTSize];

    for (uint32_t c = 0; c < num_chunks; ++c) {
        double sq = 0.0;
        for (uint32_t s = 0; s < chunk_samples; ++s) {
            float v = mono[c * chunk_samples + s];
            sq += static_cast<double>(v) * v;
        }
        audio_rms[c] = static_cast<float>(std::sqrt(sq / chunk_samples));

        // Per-chunk FFT (use min(chunk_samples, kBandFFTSize) samples; zero-pad rest)
        std::memset(re, 0, sizeof(re));
        std::memset(im, 0, sizeof(im));
        uint32_t fft_copy_len = std::min(chunk_samples, kBandFFTSize);
        for (uint32_t i = 0; i < fft_copy_len; ++i) {
            float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) *
                                               i / (kBandFFTSize - 1)));
            re[i] = mono[c * chunk_samples + i] * w;
        }
        fft_radix2(re, im, kBandFFTSize);
        double bass_e = 0.0, mid_e = 0.0, treble_e = 0.0;
        for (uint32_t k = 1; k < kBandNumBins; ++k) {
            double mag_sq = static_cast<double>(re[k]) * re[k]
                          + static_cast<double>(im[k]) * im[k];
            if (k <= bass_end_bin)       bass_e   += mag_sq;
            else if (k <= mid_end_bin)   mid_e    += mag_sq;
            else                         treble_e += mag_sq;
        }
        audio_bass[c]   = static_cast<float>(std::sqrt(bass_e));
        audio_mid[c]    = static_cast<float>(std::sqrt(mid_e));
        audio_treble[c] = static_cast<float>(std::sqrt(treble_e));

        // Sample visual time series at chunk midpoint, mapped from
        // [0, num_chunks*chunk_dur] onto the visual sample timestamp range.
        float chunk_t = (c + 0.5f) * chunk_dur;
        float visual_t = visual.front().timestamp_seconds +
            (visual.back().timestamp_seconds - visual.front().timestamp_seconds) *
            (chunk_t / (num_chunks * chunk_dur));
        v_brightness[c] = interp_at(visual, visual_t, &VisualSample::brightness);
        v_motion[c]     = interp_at(visual, visual_t, &VisualSample::motion);
        v_contrast[c]   = interp_at(visual, visual_t, &VisualSample::contrast);
    }

    m.energy_brightness_correlation = pearson_corr(audio_rms, v_brightness);
    m.energy_motion_correlation     = pearson_corr(audio_rms, v_motion);
    m.energy_contrast_correlation   = pearson_corr(audio_rms, v_contrast);

    m.band_brightness_correlations.bass   = pearson_corr(audio_bass,   v_brightness);
    m.band_brightness_correlations.mid    = pearson_corr(audio_mid,    v_brightness);
    m.band_brightness_correlations.treble = pearson_corr(audio_treble, v_brightness);
    m.band_motion_correlations.bass       = pearson_corr(audio_bass,   v_motion);
    m.band_motion_correlations.mid        = pearson_corr(audio_mid,    v_motion);
    m.band_motion_correlations.treble     = pearson_corr(audio_treble, v_motion);
    m.band_contrast_correlations.bass     = pearson_corr(audio_bass,   v_contrast);
    m.band_contrast_correlations.mid      = pearson_corr(audio_mid,    v_contrast);
    m.band_contrast_correlations.treble   = pearson_corr(audio_treble, v_contrast);

    // ---------------------------------------------------------------------
    // Onset-aligned reactivity
    // ---------------------------------------------------------------------
    // For each detected audio onset, look at the visual samples in the
    // following `max_latency_seconds` and decide whether the visual
    // "responded." A response counts if any of:
    //   - |brightness change from baseline| > kBrightnessThresh
    //   - |contrast change from baseline|   > kContrastThresh
    //   - peak motion in the window         > kMotionThresh
    // Latency = time from onset to the sample where the strongest change
    // occurred (taken across the three axes, normalized by the threshold).
    constexpr float kMaxLatencySec = 0.4f;       // generous; covers Smooth(fall_time≈0.4)
    constexpr float kBrightnessThresh = 0.02f;
    constexpr float kContrastThresh = 0.02f;
    constexpr float kMotionThresh = 0.01f;

    std::vector<float> onsets = detect_audio_onsets(audio, count, rate, channels);
    m.detected_onsets = static_cast<uint32_t>(onsets.size());

    if (!onsets.empty() && visual.size() >= 2) {
        // Map onset times (audio-buffer timeline) onto the visual sample
        // timeline. Both timelines start at the same origin (analysis-window
        // start), so we just clamp into the visual sample range.
        float visual_window_dur = visual.back().timestamp_seconds - visual.front().timestamp_seconds;
        if (visual_window_dur > 1e-6f) {
            std::vector<float> latencies;
            uint32_t responsive = 0;
            for (float onset_t : onsets) {
                if (onset_t < visual.front().timestamp_seconds) continue;
                if (onset_t > visual.back().timestamp_seconds) continue;

                float baseline_b = interp_at(visual, onset_t, &VisualSample::brightness);
                float baseline_c = interp_at(visual, onset_t, &VisualSample::contrast);

                float best_score = 0.0f;          // normalized "how big a response"
                float best_latency_sec = 0.0f;
                bool any_response = false;

                for (const auto& s : visual) {
                    float dt = s.timestamp_seconds - onset_t;
                    if (dt <= 0.0f) continue;
                    if (dt > kMaxLatencySec) break;

                    float db = std::fabs(s.brightness - baseline_b) / kBrightnessThresh;
                    float dc = std::fabs(s.contrast - baseline_c) / kContrastThresh;
                    float dm = s.motion / kMotionThresh;
                    float score = std::max({db, dc, dm});

                    if (score > 1.0f) any_response = true;
                    if (score > best_score) {
                        best_score = score;
                        best_latency_sec = dt;
                    }
                }
                if (any_response) {
                    ++responsive;
                    latencies.push_back(best_latency_sec * 1000.0f);
                }
            }
            if (!onsets.empty()) {
                m.onset_response_rate =
                    static_cast<float>(responsive) / static_cast<float>(onsets.size());
            }
            if (!latencies.empty()) {
                std::nth_element(latencies.begin(),
                                 latencies.begin() + latencies.size() / 2,
                                 latencies.end());
                m.reactivity_latency_ms = latencies[latencies.size() / 2];
            }
        }
    }

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
        add_delta(r.deltas, "brighter_more_reactive", "brighter_less_reactive",
                  a.av_reactivity.energy_brightness_correlation,
                  b.av_reactivity.energy_brightness_correlation);
        add_delta(r.deltas, "motion_more_reactive", "motion_less_reactive",
                  a.av_reactivity.energy_motion_correlation,
                  b.av_reactivity.energy_motion_correlation);
        add_delta(r.deltas, "contrast_more_reactive", "contrast_less_reactive",
                  a.av_reactivity.energy_contrast_correlation,
                  b.av_reactivity.energy_contrast_correlation);
    }

    return r;
}

} // namespace vivid
