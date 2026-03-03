#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/adsr.h"
#include "operator_api/audio_dsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float PI_F    = static_cast<float>(M_PI);
static constexpr float TWO_PI_F = 2.0f * PI_F;

namespace adsr = vivid::adsr;

// =============================================================================
// Wavetable storage
// =============================================================================

static constexpr uint32_t SAMPLES_PER_FRAME = 2048;
static constexpr uint32_t MAX_FRAMES        = 256;

// Radix-2 Cooley-Tukey FFT/IFFT, in-place, N must be power-of-2.
// Used only at init time for mipmap generation.
static void fft_inplace(float* real, float* imag, int N, bool inverse) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }
    // Butterfly passes
    for (int len = 2; len <= N; len <<= 1) {
        float ang = TWO_PI_F / static_cast<float>(len) * (inverse ? -1.0f : 1.0f);
        float w_re = std::cos(ang), w_im = std::sin(ang);
        for (int i = 0; i < N; i += len) {
            float cur_re = 1.0f, cur_im = 0.0f;
            for (int j = 0; j < len / 2; ++j) {
                int u = i + j, v = u + len / 2;
                float t_re = cur_re * real[v] - cur_im * imag[v];
                float t_im = cur_re * imag[v] + cur_im * real[v];
                real[v] = real[u] - t_re;
                imag[v] = imag[u] - t_im;
                real[u] += t_re;
                imag[u] += t_im;
                float next_re = cur_re * w_re - cur_im * w_im;
                cur_im = cur_re * w_im + cur_im * w_re;
                cur_re = next_re;
            }
        }
    }
    if (inverse) {
        float inv_n = 1.0f / static_cast<float>(N);
        for (int i = 0; i < N; ++i) {
            real[i] *= inv_n;
            imag[i] *= inv_n;
        }
    }
}

static constexpr int NUM_MIP_LEVELS = 11; // log2(2048) - log2(2) + 1; level 0 = full, level 10 = fundamental only

struct Wavetable {
    std::vector<float> data;   // frames * SAMPLES_PER_FRAME (level 0 — full bandwidth)
    std::vector<float> mip[NUM_MIP_LEVELS - 1]; // levels 1..10
    uint32_t frame_count = 0;

    void allocate(uint32_t frames) {
        frame_count = std::min(frames, MAX_FRAMES);
        uint32_t needed = frame_count * SAMPLES_PER_FRAME;
        if (data.size() < needed) {
            data.assign(needed, 0.0f);
        } else {
            std::fill_n(data.data(), needed, 0.0f);
        }
        for (int l = 0; l < NUM_MIP_LEVELS - 1; ++l) {
            if (mip[l].size() < needed)
                mip[l].assign(needed, 0.0f);
            else
                std::fill_n(mip[l].data(), needed, 0.0f);
        }
    }

    float* frame_ptr(uint32_t f) {
        return data.data() + f * SAMPLES_PER_FRAME;
    }

    void build_mipmaps() {
        const int N = static_cast<int>(SAMPLES_PER_FRAME);
        std::vector<float> tmp_re(N), tmp_im(N);
        std::vector<float> freq_re(N), freq_im(N);

        for (uint32_t fr = 0; fr < frame_count; ++fr) {
            const float* src = data.data() + fr * SAMPLES_PER_FRAME;

            // Forward FFT of this frame
            std::copy(src, src + N, freq_re.data());
            std::fill(freq_im.begin(), freq_im.end(), 0.0f);
            fft_inplace(freq_re.data(), freq_im.data(), N, false);

            for (int L = 1; L < NUM_MIP_LEVELS; ++L) {
                int max_bin = N / 2 >> L; // number of harmonics to keep (1024 >> L)

                // Copy spectrum and zero bins above max_bin
                std::copy(freq_re.begin(), freq_re.end(), tmp_re.data());
                std::copy(freq_im.begin(), freq_im.end(), tmp_im.data());

                for (int bin = max_bin + 1; bin <= N / 2; ++bin) {
                    tmp_re[bin] = tmp_im[bin] = 0.0f;
                    if (bin < N) {
                        tmp_re[N - bin] = tmp_im[N - bin] = 0.0f;
                    }
                }

                // Inverse FFT
                fft_inplace(tmp_re.data(), tmp_im.data(), N, true);

                float* dst = mip[L - 1].data() + fr * SAMPLES_PER_FRAME;
                std::copy(tmp_re.data(), tmp_re.data() + N, dst);
            }
        }
    }

    // Bilinear interpolation (frame blend + sample interp) from a specific mip level
    float sample_level(float phase, float position, int level) const {
        const float* buf = (level == 0) ? data.data() : mip[level - 1].data();

        position = std::clamp(position, 0.0f, 1.0f);
        float frame_pos = position * static_cast<float>(frame_count - 1);
        uint32_t f0 = static_cast<uint32_t>(frame_pos);
        uint32_t f1 = std::min(f0 + 1, frame_count - 1);
        float ff = frame_pos - static_cast<float>(f0);

        phase = phase - std::floor(phase);
        float sp = phase * static_cast<float>(SAMPLES_PER_FRAME);
        uint32_t s0 = static_cast<uint32_t>(sp) % SAMPLES_PER_FRAME;
        uint32_t s1 = (s0 + 1) % SAMPLES_PER_FRAME;
        float sf = sp - std::floor(sp);

        const float* d0 = buf + f0 * SAMPLES_PER_FRAME;
        const float* d1 = buf + f1 * SAMPLES_PER_FRAME;

        float a = d0[s0] + (d0[s1] - d0[s0]) * sf;
        float b = d1[s0] + (d1[s1] - d1[s0]) * sf;
        return a + (b - a) * ff;
    }

    float sample(float phase, float position, float freq_hz, float sample_rate) const {
        if (data.empty() || frame_count == 0) return 0.0f;

        // Compute mip level from playback frequency
        float max_h = sample_rate / (2.0f * std::max(freq_hz, 1.0f));
        float level_f = std::log2(static_cast<float>(SAMPLES_PER_FRAME / 2) / std::max(max_h, 1.0f));
        level_f = std::clamp(level_f, 0.0f, static_cast<float>(NUM_MIP_LEVELS - 1));

        int lo = static_cast<int>(level_f);
        int hi = std::min(lo + 1, NUM_MIP_LEVELS - 1);
        float frac = level_f - static_cast<float>(lo);

        float s_lo = sample_level(phase, position, lo);
        if (frac < 0.001f) return s_lo;

        float s_hi = sample_level(phase, position, hi);
        return s_lo + (s_hi - s_lo) * frac;
    }
};

// =============================================================================
// Built-in wavetable generators
// =============================================================================

static void generate_basic(Wavetable& wt) {
    wt.allocate(32);
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sine     = std::sin(p * TWO_PI_F);
            float triangle = 4.0f * std::abs(p - 0.5f) - 1.0f;
            float saw      = 2.0f * p - 1.0f;
            float square   = p < 0.5f ? 1.0f : -1.0f;
            float s;
            if (t < 0.333f) {
                float b = t / 0.333f;
                s = sine * (1.0f - b) + triangle * b;
            } else if (t < 0.666f) {
                float b = (t - 0.333f) / 0.333f;
                s = triangle * (1.0f - b) + saw * b;
            } else {
                float b = (t - 0.666f) / 0.334f;
                s = saw * (1.0f - b) + square * b;
            }
            d[i] = s;
        }
    }
}

static void generate_analog(Wavetable& wt) {
    wt.allocate(32);
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;
            int nh = 3 + static_cast<int>(t * 12);
            for (int h = 1; h <= nh; ++h) {
                float amp = 1.0f / static_cast<float>(h);
                if (h % 2 == 1) amp *= 1.2f;
                float drift = std::sin(static_cast<float>(fr * h) * 0.1f) * 0.02f;
                sample += amp * std::sin((p + drift) * TWO_PI_F * static_cast<float>(h));
            }
            d[i] = std::tanh(sample * 0.8f);
        }
    }
}

static void generate_digital(Wavetable& wt) {
    wt.allocate(32);
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        float mod_index = t * 8.0f;
        float ratio = 1.0f + std::floor(t * 4.0f);
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float mod = std::sin(p * TWO_PI_F * ratio);
            d[i] = std::sin(p * TWO_PI_F + mod * mod_index);
        }
    }
}

static void generate_vocal(Wavetable& wt) {
    wt.allocate(32);
    const float formants[5][3] = {
        {800.0f, 1150.0f, 2800.0f},
        {400.0f, 2000.0f, 2550.0f},
        {350.0f, 2700.0f, 2900.0f},
        {450.0f, 800.0f,  2830.0f},
        {325.0f, 700.0f,  2530.0f}
    };
    const float amps[5][3] = {
        {1.0f, 0.6f, 0.2f},
        {1.0f, 0.4f, 0.3f},
        {1.0f, 0.2f, 0.3f},
        {1.0f, 0.8f, 0.1f},
        {1.0f, 0.8f, 0.1f}
    };
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        float vowel_pos = t * 4.0f;
        int v0 = static_cast<int>(vowel_pos);
        int v1 = std::min(v0 + 1, 4);
        float blend = vowel_pos - static_cast<float>(v0);
        v0 = std::min(v0, 4);

        float blended_formants[3];
        float blended_amps[3];
        for (int f = 0; f < 3; ++f) {
            blended_formants[f] = formants[v0][f] * (1.0f - blend) + formants[v1][f] * blend;
            blended_amps[f]     = amps[v0][f]     * (1.0f - blend) + amps[v1][f]     * blend;
        }

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;
            float fundamental = 120.0f;
            for (int h = 1; h <= 40; ++h) {
                float freq = fundamental * static_cast<float>(h);
                float amp = 0.0f;
                for (int f = 0; f < 3; ++f) {
                    float bw = 80.0f + static_cast<float>(f) * 40.0f;
                    float dist = (freq - blended_formants[f]) / bw;
                    amp += blended_amps[f] * std::exp(-dist * dist * 0.5f);
                }
                sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
            }
            d[i] = std::tanh(sample * 0.3f);
        }
    }
}

static void generate_texture(Wavetable& wt) {
    wt.allocate(32);
    uint32_t seed = 12345;
    auto rand_f = [&seed]() -> float {
        seed = seed * 1103515245 + 12345;
        return (static_cast<float>(seed & 0x7FFFFFFF) /
                static_cast<float>(0x7FFFFFFF)) * 2.0f - 1.0f;
    };
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float harm = std::sin(p * TWO_PI_F)
                       + 0.5f * std::sin(p * TWO_PI_F * 2.0f)
                       + 0.25f * std::sin(p * TWO_PI_F * 3.0f);
            harm *= 0.5f;
            d[i] = harm * (1.0f - t) + rand_f() * t;
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (uint32_t i = 1; i < SAMPLES_PER_FRAME - 1; ++i)
                d[i] = d[i] * 0.5f + (d[i-1] + d[i+1]) * 0.25f;
        }
    }
}

static void generate_pwm(Wavetable& wt) {
    wt.allocate(32);
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 31.0f;
        float pw = 0.1f + t * 0.8f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            d[i] = p < pw ? 1.0f : -1.0f;
        }
        for (int pass = 0; pass < 2; ++pass) {
            float prev = d[SAMPLES_PER_FRAME - 1];
            for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                float next = d[(i + 1) % SAMPLES_PER_FRAME];
                float smoothed = d[i] * 0.7f + (prev + next) * 0.15f;
                prev = d[i];
                d[i] = smoothed;
            }
        }
    }
}

static void generate_formant(Wavetable& wt) {
    wt.allocate(64);
    // 8 vowel anchor points: A, E, I, O, U, Ae, Oe, Nasal
    const float formants[8][4] = {
        { 730.0f, 1090.0f, 2440.0f, 3400.0f},  // A
        { 660.0f, 1720.0f, 2410.0f, 3400.0f},  // E
        { 270.0f, 2290.0f, 3010.0f, 3400.0f},  // I
        { 570.0f,  840.0f, 2410.0f, 3400.0f},  // O
        { 300.0f,  870.0f, 2240.0f, 3400.0f},  // U
        { 860.0f, 1550.0f, 2500.0f, 3400.0f},  // Ae
        { 450.0f, 1500.0f, 2500.0f, 3400.0f},  // Oe
        { 480.0f, 1270.0f, 2130.0f, 3320.0f}   // Nasal
    };
    const float form_amps[8][4] = {
        {1.0f, 0.6f, 0.2f, 0.1f},
        {1.0f, 0.4f, 0.3f, 0.1f},
        {1.0f, 0.2f, 0.3f, 0.1f},
        {1.0f, 0.8f, 0.1f, 0.05f},
        {1.0f, 0.8f, 0.1f, 0.05f},
        {1.0f, 0.5f, 0.25f, 0.1f},
        {1.0f, 0.6f, 0.2f, 0.08f},
        {1.0f, 0.5f, 0.3f, 0.12f}
    };
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        float t = static_cast<float>(fr) / 63.0f;
        float vowel_pos = t * 8.0f;
        int v0 = static_cast<int>(vowel_pos) % 8;
        int v1 = (v0 + 1) % 8;
        float blend = vowel_pos - std::floor(vowel_pos);

        float bf[4], ba[4];
        for (int f = 0; f < 4; ++f) {
            bf[f] = formants[v0][f] * (1.0f - blend) + formants[v1][f] * blend;
            ba[f] = form_amps[v0][f] * (1.0f - blend) + form_amps[v1][f] * blend;
        }

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;
            float fundamental = 120.0f;
            for (int h = 1; h <= 64; ++h) {
                float freq = fundamental * static_cast<float>(h);
                float amp = 0.0f;
                for (int f = 0; f < 4; ++f) {
                    float bw = 80.0f + static_cast<float>(f) * 40.0f;
                    float dist = (freq - bf[f]) / bw;
                    amp += ba[f] * std::exp(-dist * dist * 0.5f);
                }
                sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
            }
            d[i] = std::tanh(sample * 0.3f);
        }
    }
}

static void generate_harmonic(Wavetable& wt) {
    wt.allocate(64);
    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;

            if (fr < 16) {
                // Region 1: Linear rolloff, increasing partial count
                float t_local = static_cast<float>(fr) / 15.0f;
                int num_partials = 1 + static_cast<int>(t_local * 63.0f);
                for (int h = 1; h <= num_partials; ++h) {
                    float amp = 1.0f / static_cast<float>(h);
                    sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
                }
            } else if (fr < 32) {
                // Region 2: Odd-harmonic emphasis
                float t_local = static_cast<float>(fr - 16) / 15.0f;
                float even_weight = 1.0f - t_local;
                for (int h = 1; h <= 64; ++h) {
                    float amp = 1.0f / static_cast<float>(h);
                    if (h % 2 == 0) amp *= even_weight;
                    sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
                }
            } else if (fr < 48) {
                // Region 3: Even-harmonic emphasis
                float t_local = static_cast<float>(fr - 32) / 15.0f;
                float even_weight = t_local;
                float odd_weight  = 1.0f - 0.5f * t_local;
                for (int h = 1; h <= 64; ++h) {
                    float amp = 1.0f / static_cast<float>(h);
                    if (h % 2 == 0)
                        amp *= even_weight;
                    else
                        amp *= odd_weight;
                    sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
                }
            } else {
                // Region 4: Spectral tilt
                float t_local = static_cast<float>(fr - 48) / 15.0f;
                float tilt = 2.0f - t_local * 1.7f; // 2.0 → 0.3
                for (int h = 1; h <= 64; ++h) {
                    float amp = 1.0f / std::pow(static_cast<float>(h), tilt);
                    sample += amp * std::sin(p * TWO_PI_F * static_cast<float>(h));
                }
            }

            d[i] = sample;
        }

        // Normalize peak to ±1
        float peak = 0.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i)
            peak = std::max(peak, std::abs(d[i]));
        if (peak > 0.0f) {
            float inv = 1.0f / peak;
            for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i)
                d[i] *= inv;
        }
    }
}

static void generate_metallic(Wavetable& wt) {
    wt.allocate(32);

    // 4 regions with different inharmonic partial sets
    const int region_counts[4] = {5, 6, 7, 8};
    const float region_ratios[4][8] = {
        {1.0f, 2.0f,  3.0f,  4.2f,  5.4f,  0.0f,  0.0f,  0.0f},
        {1.0f, 1.5f,  2.3f,  3.1f,  4.7f,  6.2f,  0.0f,  0.0f},
        {1.0f, 1.19f, 1.56f, 2.0f,  2.44f, 2.83f, 3.15f, 0.0f},
        {1.0f, 1.34f, 1.87f, 2.15f, 2.58f, 3.24f, 3.81f, 4.53f}
    };
    const float region_amps[4][8] = {
        {1.0f, 0.5f,  0.3f,  0.25f, 0.2f,  0.0f,  0.0f,  0.0f},
        {1.0f, 0.6f,  0.4f,  0.3f,  0.2f,  0.15f, 0.0f,  0.0f},
        {1.0f, 0.7f,  0.5f,  0.4f,  0.3f,  0.2f,  0.15f, 0.0f},
        {1.0f, 0.8f,  0.6f,  0.5f,  0.4f,  0.3f,  0.25f, 0.2f}
    };

    for (uint32_t fr = 0; fr < wt.frame_count; ++fr) {
        float* d = wt.frame_ptr(fr);

        // Determine region and local t for interpolation
        int region = static_cast<int>(fr / 8);
        if (region > 3) region = 3;
        int next_region = std::min(region + 1, 3);
        float t_local = static_cast<float>(fr % 8) / 7.0f;

        // Interpolate between current and next region
        int max_partials = std::max(region_counts[region], region_counts[next_region]);

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;
            for (int h = 0; h < max_partials; ++h) {
                float r0 = (h < region_counts[region])      ? region_ratios[region][h]      : 0.0f;
                float r1 = (h < region_counts[next_region])  ? region_ratios[next_region][h] : 0.0f;
                float a0 = (h < region_counts[region])       ? region_amps[region][h]        : 0.0f;
                float a1 = (h < region_counts[next_region])  ? region_amps[next_region][h]   : 0.0f;

                float ratio = r0 * (1.0f - t_local) + r1 * t_local;
                float amp   = a0 * (1.0f - t_local) + a1 * t_local;
                if (amp > 0.0f && ratio > 0.0f)
                    sample += amp * std::sin(p * TWO_PI_F * ratio);
            }
            d[i] = sample;
        }

        // Normalize peak to ±1
        float peak = 0.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i)
            peak = std::max(peak, std::abs(d[i]));
        if (peak > 0.0f) {
            float inv = 1.0f / peak;
            for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i)
                d[i] *= inv;
        }
    }
}

// =============================================================================
// Phase warp
// =============================================================================

enum WarpMode {
    WARP_NONE, WARP_SYNC, WARP_BEND_PLUS, WARP_BEND_MINUS,
    WARP_MIRROR, WARP_ASYM, WARP_QUANTIZE, WARP_FM, WARP_FLIP
};

static float warp_phase(float phase, int mode, float amount, float last_sample) {
    if (amount <= 0.0f || mode == WARP_NONE) return phase;
    phase = phase - std::floor(phase);

    switch (mode) {
        case WARP_SYNC: {
            float r = 1.0f + amount * 7.0f;
            float sp = phase * r;
            return sp - std::floor(sp);
        }
        case WARP_BEND_PLUS:
            return std::pow(phase, 1.0f + amount * 3.0f);
        case WARP_BEND_MINUS:
            return std::pow(phase, 1.0f / (1.0f + amount * 3.0f));
        case WARP_MIRROR: {
            float mid = 0.5f - amount * 0.3f;
            if (phase > mid) return mid - (phase - mid);
            return phase / mid * 0.5f;
        }
        case WARP_ASYM: {
            float stretch = 0.5f + amount * 0.3f;
            if (phase < 0.5f) return (phase / 0.5f) * stretch;
            return stretch + ((phase - 0.5f) / 0.5f) * (1.0f - stretch);
        }
        case WARP_QUANTIZE: {
            int steps = std::max(4, static_cast<int>(256.0f - amount * 252.0f));
            return std::floor(phase * static_cast<float>(steps)) / static_cast<float>(steps);
        }
        case WARP_FM: {
            float mp = phase + last_sample * amount * 0.5f;
            return mp - std::floor(mp);
        }
        case WARP_FLIP:
            if (phase >= 0.5f) {
                float flipped = 1.0f - phase;
                return phase * (1.0f - amount) + flipped * amount;
            }
            return phase;
        default:
            return phase;
    }
}

// =============================================================================
// Biquad filter types
// =============================================================================

enum FilterType {
    FILTER_LP12, FILTER_LP24, FILTER_HP12, FILTER_BP, FILTER_NOTCH,
    FILTER_COMB, FILTER_LADDER, FILTER_FORMANT
};

// =============================================================================
// Additional filter states
// =============================================================================

struct CombFilterState {
    static constexpr int MAX_DELAY = 2048;
    float buffer[MAX_DELAY] = {};
    int   write_pos = 0;

    void reset() {
        std::memset(buffer, 0, sizeof(buffer));
        write_pos = 0;
    }

    float process(float input, float delay_samples, float feedback) {
        delay_samples = std::clamp(delay_samples, 1.0f, static_cast<float>(MAX_DELAY - 1));
        feedback = std::clamp(feedback, -0.98f, 0.98f);

        // Linear-interpolated fractional delay read
        int   d_int  = static_cast<int>(delay_samples);
        float d_frac = delay_samples - static_cast<float>(d_int);

        int read0 = (write_pos - d_int + MAX_DELAY) % MAX_DELAY;
        int read1 = (read0 - 1 + MAX_DELAY) % MAX_DELAY;

        float delayed = buffer[read0] * (1.0f - d_frac) + buffer[read1] * d_frac;

        float out = input + delayed * feedback;
        buffer[write_pos] = out;
        write_pos = (write_pos + 1) % MAX_DELAY;
        return out;
    }
};

struct LadderFilterState {
    float stage[4] = {};

    void reset() {
        stage[0] = stage[1] = stage[2] = stage[3] = 0.0f;
    }

    float process(float input, float cutoff_hz, float reso, float sr) {
        cutoff_hz = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
        float g  = std::tan(PI_F * cutoff_hz / sr);
        float fb = reso * 4.0f;

        // Nonlinear input with feedback
        float x = std::tanh(input - fb * stage[3]);

        // 4 cascaded one-pole filters (trapezoidal integration)
        for (int i = 0; i < 4; ++i) {
            float v = (x - stage[i]) * g / (1.0f + g);
            float y = v + stage[i];
            stage[i] = y + v;
            x = y;
        }
        return x;
    }
};

struct FormantFilterState {
    // 3 parallel biquad bandpasses, each with transposed direct form II state
    float z1[3] = {};
    float z2[3] = {};

    // Vowel formant frequencies: A, E, I, O, U — 3 formants each
    static constexpr float FORMANTS[5][3] = {
        { 800.0f, 1150.0f, 2900.0f},  // A
        { 350.0f, 2000.0f, 2800.0f},  // E
        { 270.0f, 2300.0f, 3000.0f},  // I
        { 450.0f,  800.0f, 2830.0f},  // O
        { 325.0f,  700.0f, 2530.0f},  // U
    };
    static constexpr float GAINS[3] = {1.0f, 0.5f, 0.25f};

    void reset() {
        std::memset(z1, 0, sizeof(z1));
        std::memset(z2, 0, sizeof(z2));
    }

    float process(float input, float morph, float reso, float sr) {
        // morph 0..1 selects between 5 vowels (0=A, 0.25=E, 0.5=I, 0.75=O, 1=U)
        float pos = morph * 4.0f;
        int   idx = std::min(static_cast<int>(pos), 3);
        float frac = pos - static_cast<float>(idx);

        float Q = 1.0f + reso * 19.0f;
        float out = 0.0f;

        for (int b = 0; b < 3; ++b) {
            // Interpolate formant frequency between adjacent vowels
            float freq = FORMANTS[idx][b] * (1.0f - frac) + FORMANTS[idx + 1][b] * frac;
            freq = std::min(freq, sr * 0.45f);

            // Bandpass biquad coefficients
            float omega = TWO_PI_F * freq / sr;
            float sin_w = std::sin(omega);
            float cos_w = std::cos(omega);
            float alpha = sin_w / (2.0f * Q);

            float b0 =  sin_w * 0.5f;
            float b1 =  0.0f;
            float b2 = -sin_w * 0.5f;
            float a0 =  1.0f + alpha;
            float a1 = -2.0f * cos_w;
            float a2 =  1.0f - alpha;

            float inv_a0 = 1.0f / a0;
            b0 *= inv_a0; b1 *= inv_a0; b2 *= inv_a0;
            a1 *= inv_a0; a2 *= inv_a0;

            // Transposed direct form II
            float y = b0 * input + z1[b];
            z1[b] = b1 * input - a1 * y + z2[b];
            z2[b] = b2 * input - a2 * y;

            out += y * GAINS[b];
        }
        return out;
    }
};

// =============================================================================
// WavetableSynth operator
// =============================================================================

struct WavetableSynth : vivid::OperatorBase {
    static constexpr const char* kName   = "WavetableSynth";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    // --- Parameters ---

    // Core
    vivid::Param<int>   wavetable        {"wavetable",        0,        {"Basic", "Analog", "Digital", "Vocal", "Texture", "PWM", "Formant", "Harmonic", "Metallic"}};
    vivid::Param<float> position         {"position",         0.0f,     0.0f, 1.0f};
    vivid::Param<float> amplitude        {"amplitude",        0.3f,     0.0f, 1.0f};

    // Warp
    vivid::Param<int>   warp_mode        {"warp_mode",        0,        {"None", "Sync", "BendPlus", "BendMinus", "Mirror", "Asym", "Quantize", "FM", "Flip"}};
    vivid::Param<float> warp_amount      {"warp_amount",      0.0f,     0.0f, 1.0f};

    // Unison
    vivid::Param<int>   unison_voices    {"unison_voices",    1,        1, 16};
    vivid::Param<float> unison_spread    {"unison_spread",    20.0f,    0.0f, 100.0f};
    vivid::Param<float> unison_stereo    {"unison_stereo",    1.0f,     0.0f, 1.0f};
    vivid::Param<int>   unison_spread_mode {"unison_spread_mode", 0, {"Linear", "Exponential", "Random"}};

    // Sub oscillator
    vivid::Param<float> sub_level        {"sub_level",        0.0f,     0.0f, 1.0f};
    vivid::Param<int>   sub_octave       {"sub_octave",       0,        {"-1", "-2"}};
    vivid::Param<int>   sub_waveform     {"sub_waveform",     0,        {"Sine", "Triangle", "Saw", "Square", "Noise"}};

    // Noise oscillator
    vivid::Param<float> noise_level      {"noise_level",      0.0f,     0.0f, 1.0f};
    vivid::Param<int>   noise_type       {"noise_type",       0,        {"White", "Pink"}};

    // Portamento
    vivid::Param<float> portamento       {"portamento",       0.0f,     0.0f, 2000.0f};

    // Amplitude envelope
    vivid::Param<float> attack           {"attack",           0.01f,    0.001f, 5.0f};
    vivid::Param<float> decay            {"decay",            0.1f,     0.001f, 5.0f};
    vivid::Param<float> sustain          {"sustain",          0.7f,     0.0f,   1.0f};
    vivid::Param<float> release          {"release",          0.3f,     0.001f, 10.0f};

    // Filter
    vivid::Param<int>   filter_type      {"filter_type",      1,        {"LP12", "LP24", "HP12", "BP", "Notch", "Comb", "Ladder", "Formant"}};
    vivid::Param<float> filter_cutoff    {"filter_cutoff",    20000.0f, 20.0f,  20000.0f};
    vivid::Param<float> filter_resonance {"filter_resonance", 0.0f,     0.0f,   1.0f};
    vivid::Param<float> filter_keytrack  {"filter_keytrack",  0.0f,     0.0f,   1.0f};
    vivid::Param<float> filter_drive     {"filter_drive",     0.0f,     0.0f,   1.0f};

    // Filter envelope
    vivid::Param<float> filter_attack    {"filter_attack",    0.01f,    0.001f, 10.0f};
    vivid::Param<float> filter_decay     {"filter_decay",     0.3f,     0.001f, 10.0f};
    vivid::Param<float> filter_sustain   {"filter_sustain",   0.0f,     0.0f,   1.0f};
    vivid::Param<float> filter_release   {"filter_release",   0.3f,     0.001f, 10.0f};
    vivid::Param<float> filter_env_amount{"filter_env_amount",0.0f,    -1.0f,   1.0f};

    // Position envelope
    vivid::Param<float> position_attack     {"position_attack",     0.01f, 0.001f, 10.0f};
    vivid::Param<float> position_decay      {"position_decay",      0.3f,  0.001f, 10.0f};
    vivid::Param<float> position_sustain    {"position_sustain",    0.0f,  0.0f,   1.0f};
    vivid::Param<float> position_release    {"position_release",    0.3f,  0.001f, 10.0f};
    vivid::Param<float> position_env_amount {"position_env_amount", 0.0f, -1.0f,   1.0f};

    // Velocity
    vivid::Param<float> vel_to_volume    {"vel_to_volume",    1.0f,     0.0f,   1.0f};
    vivid::Param<float> vel_to_attack    {"vel_to_attack",    0.0f,    -1.0f,   1.0f};

    // Stereo & misc
    vivid::Param<float> stereo_spread    {"stereo_spread",    0.5f,     0.0f,   1.0f};
    vivid::Param<float> detune           {"detune",           0.0f,     0.0f,   50.0f};
    vivid::Param<bool>  env_bypass       {"env_bypass",       false};

    // --- Voice state ---

    static constexpr int kMaxVoices = 16;

    struct Voice {
        float  note           = 0;
        float  velocity       = 0;
        double phase          = 0;
        double sub_phase      = 0;
        float  current_freq   = 0;
        float  target_freq    = 0;
        float  detune_offset  = 0;  // cents offset for unison
        float  pan            = 0;  // -1..1 for unison stereo
        float  last_sample    = 0;  // FM warp feedback
        audio_dsp::WhiteNoise white_noise;
        audio_dsp::PinkNoise  pink_noise;
        uint64_t note_id      = 0;
        int    gate_slot      = -1;

        adsr::State amp_env;
        adsr::State filt_env;
        adsr::State pos_env;

        // Biquad filter state (2 cascaded stages for LP24)
        float fz1[2] = {};
        float fz2[2] = {};

        // Additional filter states
        CombFilterState    comb;
        LadderFilterState  ladder;
        FormantFilterState formant;

        bool is_active() const { return amp_env.is_active(); }

        void reset_filter() {
            fz1[0] = fz1[1] = 0.0f;
            fz2[0] = fz2[1] = 0.0f;
            comb.reset();
            ladder.reset();
            formant.reset();
        }
    };

    Voice    voices_[kMaxVoices] = {};
    uint64_t note_counter_       = 0;

    // Previous spread inputs for gate edge detection
    float    prev_gates_[kMaxVoices] = {};
    float    prev_notes_[kMaxVoices] = {};
    uint32_t prev_spread_len_        = 0;

    // All wavetables pre-computed in constructor so process() never generates.
    Wavetable all_tables_[9];

    WavetableSynth() {
        generate_basic(all_tables_[0]);
        generate_analog(all_tables_[1]);
        generate_digital(all_tables_[2]);
        generate_vocal(all_tables_[3]);
        generate_texture(all_tables_[4]);
        generate_pwm(all_tables_[5]);
        generate_formant(all_tables_[6]);
        generate_harmonic(all_tables_[7]);
        generate_metallic(all_tables_[8]);
        for (auto& t : all_tables_) t.build_mipmaps();
    }

    // --- Param / port registration ---

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        // -- Groups --
        param_group(wavetable,  "Core");
        param_group(position,   "Core");
        param_group(amplitude,  "Core");

        param_group(warp_mode,   "Warp");
        param_group(warp_amount, "Warp");

        param_group(unison_voices, "Unison");
        param_group(unison_spread, "Unison");
        param_group(unison_stereo, "Unison");
        param_group(unison_spread_mode, "Unison");

        param_group(sub_level,    "Sub");
        param_group(sub_octave,   "Sub");
        param_group(sub_waveform, "Sub");

        param_group(noise_level, "Noise");
        param_group(noise_type,  "Noise");

        param_group(portamento, "Portamento");

        param_group(attack,  "Amp Envelope");
        param_group(decay,   "Amp Envelope");
        param_group(sustain, "Amp Envelope");
        param_group(release, "Amp Envelope");

        param_group(filter_type,      "Filter");
        param_group(filter_cutoff,    "Filter");
        param_group(filter_resonance, "Filter");
        param_group(filter_keytrack,  "Filter");
        param_group(filter_drive,     "Filter");

        param_group(filter_attack,     "Filter Envelope");
        param_group(filter_decay,      "Filter Envelope");
        param_group(filter_sustain,    "Filter Envelope");
        param_group(filter_release,    "Filter Envelope");
        param_group(filter_env_amount, "Filter Envelope");

        param_group(position_attack,     "Position Envelope");
        param_group(position_decay,      "Position Envelope");
        param_group(position_sustain,    "Position Envelope");
        param_group(position_release,    "Position Envelope");
        param_group(position_env_amount, "Position Envelope");

        param_group(vel_to_volume, "Velocity");
        param_group(vel_to_attack, "Velocity");

        param_group(stereo_spread, "Output");
        param_group(detune,        "Output");
        param_group(env_bypass,    "Output");

        // -- Display hints --
        display_hint(attack,  VIVID_DISPLAY_KNOB);
        display_hint(decay,   VIVID_DISPLAY_KNOB);
        display_hint(sustain, VIVID_DISPLAY_KNOB);
        display_hint(release, VIVID_DISPLAY_KNOB);

        display_hint(filter_cutoff,    VIVID_DISPLAY_KNOB);
        display_hint(filter_resonance, VIVID_DISPLAY_KNOB);
        display_hint(filter_keytrack,  VIVID_DISPLAY_KNOB);
        display_hint(filter_drive,     VIVID_DISPLAY_KNOB);

        display_hint(noise_level, VIVID_DISPLAY_KNOB);

        display_hint(filter_attack,  VIVID_DISPLAY_KNOB);
        display_hint(filter_decay,   VIVID_DISPLAY_KNOB);
        display_hint(filter_sustain, VIVID_DISPLAY_KNOB);
        display_hint(filter_release, VIVID_DISPLAY_KNOB);

        display_hint(position_attack,  VIVID_DISPLAY_KNOB);
        display_hint(position_decay,   VIVID_DISPLAY_KNOB);
        display_hint(position_sustain, VIVID_DISPLAY_KNOB);
        display_hint(position_release, VIVID_DISPLAY_KNOB);

        // -- Multi-column layouts --
        // Amp ADSR: 4 columns
        layout_row(attack,  4, 0);
        layout_row(decay,   4, 1);
        layout_row(sustain, 4, 2);
        layout_row(release, 4, 3);

        // Filter knobs: 4 columns
        layout_row(filter_cutoff,    4, 0);
        layout_row(filter_resonance, 4, 1);
        layout_row(filter_keytrack,  4, 2);
        layout_row(filter_drive,     4, 3);

        // Noise: 2 columns
        layout_row(noise_level, 2, 0);
        layout_row(noise_type,  2, 1);

        // Filter Envelope ADSR: 4 columns
        layout_row(filter_attack,  4, 0);
        layout_row(filter_decay,   4, 1);
        layout_row(filter_sustain, 4, 2);
        layout_row(filter_release, 4, 3);

        // Position Envelope ADSR: 4 columns
        layout_row(position_attack,  4, 0);
        layout_row(position_decay,   4, 1);
        layout_row(position_sustain, 4, 2);
        layout_row(position_release, 4, 3);

        // Velocity: 2 columns
        layout_row(vel_to_volume, 2, 0);
        layout_row(vel_to_attack, 2, 1);

        // Output: 2 columns for spread/detune, env_bypass full-width
        layout_row(stereo_spread, 2, 0);
        layout_row(detune,        2, 1);

        out.push_back(&wavetable);
        out.push_back(&position);
        out.push_back(&amplitude);
        out.push_back(&warp_mode);
        out.push_back(&warp_amount);
        out.push_back(&unison_voices);
        out.push_back(&unison_spread);
        out.push_back(&unison_stereo);
        out.push_back(&unison_spread_mode);
        out.push_back(&sub_level);
        out.push_back(&sub_octave);
        out.push_back(&sub_waveform);
        out.push_back(&noise_level);
        out.push_back(&noise_type);
        out.push_back(&portamento);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&filter_type);
        out.push_back(&filter_cutoff);
        out.push_back(&filter_resonance);
        out.push_back(&filter_keytrack);
        out.push_back(&filter_drive);
        out.push_back(&filter_attack);
        out.push_back(&filter_decay);
        out.push_back(&filter_sustain);
        out.push_back(&filter_release);
        out.push_back(&filter_env_amount);
        out.push_back(&position_attack);
        out.push_back(&position_decay);
        out.push_back(&position_sustain);
        out.push_back(&position_release);
        out.push_back(&position_env_amount);
        out.push_back(&vel_to_volume);
        out.push_back(&vel_to_attack);
        out.push_back(&stereo_spread);
        out.push_back(&detune);
        out.push_back(&env_bypass);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 0
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 1
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 2
        out.push_back({"filter_env", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 3
        out.push_back({"pitch_mod",  VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 4
        out.push_back({"amp_mod",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 5
        out.push_back({"position_mod", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});  // 6
        out.push_back({"output_left",  VIVID_PORT_AUDIO_FLOAT,    VIVID_PORT_OUTPUT}); // out 0
        out.push_back({"output_right", VIVID_PORT_AUDIO_FLOAT,    VIVID_PORT_OUTPUT}); // out 1
        out.push_back({"envelopes",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT}); // out 2
    }

    // --- Helpers ---

    static float cents_to_ratio(float cents) {
        return std::pow(2.0f, cents / 1200.0f);
    }

    static float read_spread_slot(const VividSpreadPort* sp, int slot, float fallback = 0.0f) {
        if (sp && sp->data && slot >= 0 && static_cast<uint32_t>(slot) < sp->length)
            return sp->data[slot];
        return fallback;
    }

    static float midi_to_freq(float note) {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    // --- Voice management ---

    int find_free_voice() const {
        for (int i = 0; i < kMaxVoices; ++i)
            if (!voices_[i].is_active()) return i;
        return -1;
    }

    int find_voice_to_steal() const {
        int idx = -1;
        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].is_active() && voices_[i].note_id < oldest) {
                oldest = voices_[i].note_id;
                idx = i;
            }
        }
        return idx;
    }

    int find_voice_by_slot(int slot) const {
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].is_active() &&
                voices_[i].amp_env.stage != adsr::RELEASE &&
                voices_[i].gate_slot == slot)
                return i;
        }
        return -1;
    }

    void trigger_note_on(float note, float vel, int slot, float porta_ms) {
        int num_uni   = unison_voices.int_value();
        float uni_spr = unison_spread.value;
        float uni_st  = unison_stereo.value;

        for (int u = 0; u < num_uni; ++u) {
            // Check if there's already a voice for this slot+unison index
            // For portamento: reuse existing voice instead of allocating new
            int vi = -1;
            if (porta_ms > 0.0f) {
                // Find existing voice for this slot with matching unison position
                int found = 0;
                for (int i = 0; i < kMaxVoices; ++i) {
                    if (voices_[i].is_active() &&
                        voices_[i].amp_env.stage != adsr::RELEASE &&
                        voices_[i].gate_slot == slot) {
                        if (found == u) { vi = i; break; }
                        ++found;
                    }
                }
            }

            if (vi >= 0) {
                // Portamento: update target frequency, don't reset envelope
                voices_[vi].note = note;
                voices_[vi].target_freq = midi_to_freq(note);
                voices_[vi].velocity = vel;
                voices_[vi].note_id = ++note_counter_;
                continue;
            }

            // Allocate new voice
            vi = find_free_voice();
            if (vi < 0) vi = find_voice_to_steal();
            if (vi < 0) break;

            Voice& v = voices_[vi];
            v.note = note;
            v.velocity = vel;
            v.gate_slot = slot;
            v.note_id = ++note_counter_;

            // Unison detune & pan
            if (num_uni > 1) {
                float t = static_cast<float>(u) / static_cast<float>(num_uni - 1);
                float centered = t - 0.5f;

                int mode = unison_spread_mode.int_value();
                if (mode == 1) {
                    // Exponential: wider spacing at extremes
                    float sign = (centered >= 0.0f) ? 1.0f : -1.0f;
                    centered = sign * std::pow(std::abs(centered) * 2.0f, 1.5f) * 0.5f;
                } else if (mode == 2) {
                    // Random: deterministic hash from voice index + note
                    uint32_t seed = static_cast<uint32_t>(vi) * 1664525u
                                  + static_cast<uint32_t>(note * 100.0f);
                    seed ^= seed >> 16; seed *= 0x45d9f3bu; seed ^= seed >> 16;
                    centered = (static_cast<float>(seed & 0xFFFFu) / 65535.0f - 0.5f);
                }

                v.detune_offset = centered * uni_spr;
                v.pan = (t - 0.5f) * 2.0f * uni_st;
            } else {
                v.detune_offset = 0.0f;
                v.pan = 0.0f;
            }

            float freq = midi_to_freq(note);
            v.target_freq = freq;
            v.current_freq = freq; // no portamento for new voice
            v.phase = 0.0;
            v.sub_phase = 0.0;
            v.last_sample = 0.0f;
            v.white_noise.state = 12345u + static_cast<uint32_t>(vi) * 1664525u;
            v.pink_noise = {};
            v.pink_noise.white.state = 67890u + static_cast<uint32_t>(vi) * 1664525u;

            adsr::gate_on(v.amp_env);
            adsr::gate_on(v.filt_env);
            adsr::gate_on(v.pos_env);
            v.reset_filter();
        }
    }

    void trigger_note_off_slot(int slot) {
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].is_active() &&
                voices_[i].amp_env.stage != adsr::RELEASE &&
                voices_[i].gate_slot == slot) {
                adsr::gate_off(voices_[i].amp_env);
                adsr::gate_off(voices_[i].filt_env);
                adsr::gate_off(voices_[i].pos_env);
            }
        }
    }

    // --- Biquad filter ---

    float apply_biquad(Voice& v, float input, float cutoff_hz, float reso,
                       int ftype, float sr) {
        cutoff_hz = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
        reso = std::clamp(reso, 0.0f, 1.0f);

        float omega = TWO_PI_F * cutoff_hz / sr;
        float sin_w = std::sin(omega);
        float cos_w = std::cos(omega);
        float Q     = 0.5f + reso * 19.5f;
        float alpha = sin_w / (2.0f * Q);

        float b0, b1, b2, a0, a1, a2;

        switch (ftype) {
            case FILTER_LP12:
            case FILTER_LP24:
                b0 = (1.0f - cos_w) * 0.5f;
                b1 =  1.0f - cos_w;
                b2 = (1.0f - cos_w) * 0.5f;
                a0 =  1.0f + alpha;
                a1 = -2.0f * cos_w;
                a2 =  1.0f - alpha;
                break;
            case FILTER_HP12:
                b0 = (1.0f + cos_w) * 0.5f;
                b1 = -(1.0f + cos_w);
                b2 = (1.0f + cos_w) * 0.5f;
                a0 =  1.0f + alpha;
                a1 = -2.0f * cos_w;
                a2 =  1.0f - alpha;
                break;
            case FILTER_BP:
                b0 =  sin_w * 0.5f;
                b1 =  0.0f;
                b2 = -sin_w * 0.5f;
                a0 =  1.0f + alpha;
                a1 = -2.0f * cos_w;
                a2 =  1.0f - alpha;
                break;
            case FILTER_NOTCH:
                b0 =  1.0f;
                b1 = -2.0f * cos_w;
                b2 =  1.0f;
                a0 =  1.0f + alpha;
                a1 = -2.0f * cos_w;
                a2 =  1.0f - alpha;
                break;
            default:
                return input;
        }

        // Normalize
        float inv_a0 = 1.0f / a0;
        b0 *= inv_a0; b1 *= inv_a0; b2 *= inv_a0;
        a1 *= inv_a0; a2 *= inv_a0;

        // Stage 1 (transposed direct form II)
        float out = b0 * input + v.fz1[0];
        v.fz1[0] = b1 * input - a1 * out + v.fz2[0];
        v.fz2[0] = b2 * input - a2 * out;

        // Stage 2 for LP24 (4-pole)
        if (ftype == FILTER_LP24) {
            float in2 = out;
            out = b0 * in2 + v.fz1[1];
            v.fz1[1] = b1 * in2 - a1 * out + v.fz2[1];
            v.fz2[1] = b2 * in2 - a2 * out;
        }

        return out;
    }

    // --- Filter dispatch ---

    float apply_filter(Voice& v, float input, float cutoff_hz, float reso,
                       int ftype, float sr) {
        switch (ftype) {
            case FILTER_LP12: case FILTER_LP24: case FILTER_HP12:
            case FILTER_BP:   case FILTER_NOTCH:
                return apply_biquad(v, input, cutoff_hz, reso, ftype, sr);
            case FILTER_COMB: {
                float delay_samples = sr / std::max(cutoff_hz, 20.0f);
                float feedback = reso * 0.98f;
                return v.comb.process(input, delay_samples, feedback);
            }
            case FILTER_LADDER:
                return v.ladder.process(input, cutoff_hz, reso, sr);
            case FILTER_FORMANT: {
                float morph = std::log2(cutoff_hz / 20.0f)
                            / std::log2(20000.0f / 20.0f);
                morph = std::clamp(morph, 0.0f, 1.0f);
                return v.formant.process(input, morph, reso, sr);
            }
            default:
                return input;
        }
    }

    // --- Gate processing ---

    void update_gates(const VividProcessContext* ctx) {
        if (!ctx->input_spreads) return;

        const auto& notes_sp = ctx->input_spreads[0];
        const auto& vel_sp   = ctx->input_spreads[1];
        const auto& gates_sp = ctx->input_spreads[2];

        uint32_t len = gates_sp.length;
        if (len > static_cast<uint32_t>(kMaxVoices)) len = kMaxVoices;

        float porta_ms = portamento.value;

        for (uint32_t i = 0; i < len; ++i) {
            float cur_gate = read_spread_slot(&gates_sp, static_cast<int>(i));
            float cur_note = read_spread_slot(&notes_sp, static_cast<int>(i));
            float cur_vel  = read_spread_slot(&vel_sp,   static_cast<int>(i), 0.8f);

            float prev_gate = (i < prev_spread_len_) ? prev_gates_[i] : 0.0f;
            float prev_note = (i < prev_spread_len_) ? prev_notes_[i] : 0.0f;

            bool on        = (cur_gate > 0.5f) && (prev_gate <= 0.5f);
            bool off       = (cur_gate <= 0.5f) && (prev_gate > 0.5f);
            bool retrigger = (cur_gate > 0.5f) && (prev_gate > 0.5f) &&
                             (std::abs(cur_note - prev_note) > 0.5f);

            if (on || retrigger) {
                if (retrigger && porta_ms <= 0.0f)
                    trigger_note_off_slot(static_cast<int>(i));
                trigger_note_on(cur_note, cur_vel, static_cast<int>(i), retrigger ? porta_ms : 0.0f);
            } else if (off) {
                trigger_note_off_slot(static_cast<int>(i));
            }

            prev_gates_[i] = cur_gate;
            prev_notes_[i] = cur_note;
        }

        for (uint32_t i = len; i < prev_spread_len_; ++i) {
            if (prev_gates_[i] > 0.5f)
                trigger_note_off_slot(static_cast<int>(i));
            prev_gates_[i] = 0.0f;
            prev_notes_[i] = 0.0f;
        }

        prev_spread_len_ = len;
    }

    // --- Main process ---

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out_l = audio->output_buffers[0];
        float* out_r = audio->output_buffers[1];
        uint32_t frames = audio->buffer_size;
        float sr  = static_cast<float>(audio->sample_rate);
        float dt  = 1.0f / sr;

        // Read params
        int   wt_idx       = std::clamp(wavetable.int_value(), 0, 8);
        float pos          = position.value;
        float amp          = amplitude.value;
        int   warp_m       = warp_mode.int_value();
        float warp_a       = warp_amount.value;
        int   num_uni      = unison_voices.int_value();
        float sub_lvl      = sub_level.value;
        int   sub_oct      = sub_octave.int_value();
        int   sub_wave     = sub_waveform.int_value();
        float noise_lvl    = noise_level.value;
        int   noise_tp     = noise_type.int_value();
        float porta_ms     = portamento.value;
        float att          = attack.value;
        float dec          = decay.value;
        float sus          = sustain.value;
        float rel          = release.value;
        int   ftype        = filter_type.int_value();
        float f_cutoff     = filter_cutoff.value;
        float f_reso       = filter_resonance.value;
        float f_keytrack   = filter_keytrack.value;
        float f_drive      = filter_drive.value;
        float f_att        = filter_attack.value;
        float f_dec        = filter_decay.value;
        float f_sus        = filter_sustain.value;
        float f_rel        = filter_release.value;
        float f_env_amt    = filter_env_amount.value;
        float p_att        = position_attack.value;
        float p_dec        = position_decay.value;
        float p_sus        = position_sustain.value;
        float p_rel        = position_release.value;
        float p_env_amt    = position_env_amount.value;
        float v2vol        = vel_to_volume.value;
        float v2atk        = vel_to_attack.value;
        float spread       = stereo_spread.value;
        float det_cents    = detune.value;
        bool  bypass       = env_bypass.value > 0.5f;

        const Wavetable& wt = all_tables_[wt_idx];

        // Modulation spread inputs
        const VividSpreadPort* filter_env_sp = ctx->input_spreads ? &ctx->input_spreads[3] : nullptr;
        const VividSpreadPort* pitch_mod_sp  = ctx->input_spreads ? &ctx->input_spreads[4] : nullptr;
        const VividSpreadPort* amp_mod_sp    = ctx->input_spreads ? &ctx->input_spreads[5] : nullptr;
        const VividSpreadPort* position_mod_sp = ctx->input_spreads ? &ctx->input_spreads[6] : nullptr;

        update_gates(ctx);

        // Portamento rate (per-sample exponential glide)
        float porta_rate = 1.0f;
        if (porta_ms > 0.0f) {
            float porta_samples = porta_ms * 0.001f * sr;
            porta_rate = 1.0f - std::exp(-4.0f / porta_samples);
        }

        // Sub oscillator divisor
        float sub_div = (sub_oct == 1) ? 4.0f : 2.0f; // choice 0="-1"(÷2), 1="-2"(÷4)

        // Filter active check
        bool filter_active = (ftype >= FILTER_COMB) ||
                             (f_cutoff < 19999.0f) || (f_reso > 0.01f) ||
                             (std::abs(f_env_amt) > 0.001f) || (f_drive > 0.001f);
        bool pos_env_active = p_env_amt != 0.0f;

        float norm = 1.0f / std::sqrt(static_cast<float>(kMaxVoices));

        // Pre-compute per-voice stereo pan gains
        uint32_t spread_len = prev_spread_len_;
        float voice_gain_l[kMaxVoices] = {};
        float voice_gain_r[kMaxVoices] = {};

        for (int vi = 0; vi < kMaxVoices; ++vi) {
            Voice& v = voices_[vi];
            if (!v.is_active()) continue;

            // Combine slot-based stereo spread and unison pan
            float pan = v.pan; // unison pan
            if (num_uni <= 1 && spread_len > 1 && v.gate_slot >= 0) {
                // No unison: use slot-based spread (like original Polysynth)
                pan = (static_cast<float>(v.gate_slot) /
                       static_cast<float>(spread_len - 1) * 2.0f - 1.0f) * spread;
            }
            float theta = (pan + 1.0f) * PI_F * 0.25f;
            voice_gain_l[vi] = std::cos(theta);
            voice_gain_r[vi] = std::sin(theta);
        }

        std::memset(out_l, 0, frames * sizeof(float));
        std::memset(out_r, 0, frames * sizeof(float));

        for (uint32_t s = 0; s < frames; ++s) {
            float left_mix  = 0.0f;
            float right_mix = 0.0f;

            for (int vi = 0; vi < kMaxVoices; ++vi) {
                Voice& v = voices_[vi];
                if (!v.is_active()) continue;

                // Velocity→attack modulation
                float eff_att = att;
                if (v2atk != 0.0f) {
                    float vel_mod = v2atk * (1.0f - v.velocity);
                    eff_att *= std::pow(2.0f, vel_mod * 2.0f);
                    eff_att = std::clamp(eff_att, 0.001f, 10.0f);
                }

                // Advance envelopes
                adsr::advance(v.amp_env, dt, eff_att, dec, sus, rel);
                if (!v.is_active()) continue;

                if (filter_active)
                    adsr::advance(v.filt_env, dt, f_att, f_dec, f_sus, f_rel);
                if (pos_env_active)
                    adsr::advance(v.pos_env, dt, p_att, p_dec, p_sus, p_rel);

                // Portamento: glide current_freq toward target_freq
                if (porta_ms > 0.0f && v.current_freq != v.target_freq) {
                    v.current_freq += (v.target_freq - v.current_freq) * porta_rate;
                    if (std::abs(v.current_freq - v.target_freq) < 0.01f)
                        v.current_freq = v.target_freq;
                }

                // Pitch modulation
                float pitch_offset = read_spread_slot(pitch_mod_sp, v.gate_slot);
                float freq = v.current_freq *
                             cents_to_ratio(v.detune_offset + det_cents) *
                             std::pow(2.0f, pitch_offset / 12.0f);

                float phase_inc = static_cast<float>(freq) / sr;

                // Phase warp + wavetable sample
                float warped = warp_phase(static_cast<float>(v.phase), warp_m, warp_a, v.last_sample);

                // Position modulation (internal envelope + external spread)
                float effective_pos = pos;
                if (pos_env_active)
                    effective_pos += v.pos_env.env_value * p_env_amt;
                float ext_pos = read_spread_slot(position_mod_sp, v.gate_slot);
                effective_pos += ext_pos;
                effective_pos = std::clamp(effective_pos, 0.0f, 1.0f);

                float sig = wt.sample(warped, effective_pos, freq, sr);
                v.last_sample = sig;

                // Sub oscillator
                if (sub_lvl > 0.0f) {
                    float sub_freq = v.current_freq / sub_div;
                    float sub_inc  = sub_freq / sr;
                    float sub_sig;
                    if (sub_wave == 4) {
                        sub_sig = v.white_noise.next();
                    } else {
                        // Map param order (Sine=0, Tri=1, Saw=2, Sq=3)
                        // to audio_dsp::waveform order (sine=0, saw=1, sq=2, tri=3)
                        static constexpr int wf_map[] = {0, 3, 1, 2};
                        sub_sig = static_cast<float>(audio_dsp::waveform(v.sub_phase, wf_map[sub_wave]));
                    }
                    sig = sig * (1.0f - sub_lvl) + sub_sig * sub_lvl;
                    v.sub_phase += static_cast<double>(sub_inc);
                    if (v.sub_phase >= 1.0) v.sub_phase -= 1.0;
                }

                // Noise oscillator
                if (noise_lvl > 0.001f) {
                    float n = (noise_tp == 0) ? v.white_noise.next()
                                              : v.pink_noise.next();
                    sig += n * noise_lvl;
                }

                // Per-voice biquad filter
                if (filter_active) {
                    float cutoff = f_cutoff;

                    // Filter envelope modulation (bipolar)
                    float env_mod = v.filt_env.env_value * f_env_amt;
                    cutoff *= std::pow(2.0f, env_mod * 4.0f);

                    // External filter envelope modulation
                    float ext_fenv = read_spread_slot(filter_env_sp, v.gate_slot);
                    if (ext_fenv != 0.0f)
                        cutoff *= std::pow(2.0f, ext_fenv * 4.0f);

                    // Keytracking
                    if (f_keytrack > 0.0f) {
                        float oct_from_c4 = std::log2(v.current_freq / 261.63f);
                        cutoff *= std::pow(2.0f, oct_from_c4 * f_keytrack);
                    }

                    // Filter drive (gain-compensated soft clip)
                    if (f_drive > 0.001f) {
                        float d = 1.0f + f_drive * 7.0f;
                        sig = std::tanh(sig * d) / std::tanh(d);
                    }

                    sig = apply_filter(v, sig, cutoff, f_reso, ftype, sr);
                }

                // Envelope & velocity
                float env = bypass ? 1.0f : v.amp_env.env_value;
                float vel_vol = 1.0f - v2vol * (1.0f - v.velocity);
                sig *= env * vel_vol;
                sig *= read_spread_slot(amp_mod_sp, v.gate_slot, 1.0f);

                left_mix  += sig * voice_gain_l[vi];
                right_mix += sig * voice_gain_r[vi];

                // Advance phase
                v.phase += static_cast<double>(phase_inc);
                if (v.phase >= 1.0) v.phase -= 1.0;
            }

            out_l[s] = left_mix * amp * norm;
            out_r[s] = right_mix * amp * norm;
        }

        // Write per-voice envelope values to output spread
        if (ctx->output_spreads) {
            auto& env_sp = ctx->output_spreads[2];
            uint32_t active_count = 0;
            for (int vi = 0; vi < kMaxVoices; ++vi) {
                if (voices_[vi].is_active()) {
                    if (active_count < env_sp.capacity)
                        env_sp.data[active_count] = voices_[vi].amp_env.env_value;
                    active_count++;
                }
            }
            env_sp.length = std::min(active_count, env_sp.capacity);
        }
    }
};

VIVID_REGISTER(WavetableSynth)
