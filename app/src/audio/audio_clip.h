#pragma once
#include "audio/audio_clip_shared.h"   // audio_clip_ed::WarpPoint / TransientPoint / math
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Transport-locked audio clip. Holds stereo float PCM (resampled to the device rate at
// load) read phase-locked to the master beat. The audio thread calls render() each block.
//
// The Ableton-style audio clip editor extends the old bare looper with clip shaping:
// gain, reverse, fades, loop crossfade, a clip/loop region, pitch (semitones/cents), and
// the warp model (mode + markers + transients). The **non-warp / Repitch** playback path
// lives here (varispeed, allocation-free); the pitch-preserving Complex/Beats stretch
// (signalsmith, via a per-clip ClipDsp) applies the warp. With all fields at their defaults
// render() is bit-identical to the original looper.
namespace vivid::session {

enum class WarpMode { Complex = 0, Beats = 1, Repitch = 2 };

struct AudioClip {
    std::vector<float> L, R;       // PCM (R empty => mono)
    // UI waveform-preview cache: session_audio_waveform() rescans ALL of L for peak-per-bin, and the
    // session view called it EVERY frame per audio clip cell — an O(N) full-sample scan per frame that
    // dominated the render frame time. Cache the bins; invalidated when the requested bin count or the
    // sample data (size/ptr) changes. UI-thread only — the audio thread never reads/writes these, so no
    // synchronization is needed.
    mutable std::vector<float> wave_bins_;
    mutable size_t             wave_src_n_   = 0;         // L.size() when the cache was built
    mutable const float*       wave_src_ptr_ = nullptr;   // L.data() when the cache was built
    double             loop_beats = 4.0;
    double             src_bpm = 0.0;   // source tempo (0 = generated / unknown)
    uint32_t           sr = 0;          // sample rate the PCM is at (device rate; for fades/ms)
    std::string        name;
    std::string        src_path;        // absolute WAV path (empty = generated); persisted so the
                                        // loop reloads on session open (audio_clip_load_wav sets it).

    // --- clip shaping ---
    float  gain            = 1.0f;
    float  pitch_semitones = 0.0f;      // applied by the stretcher; stored here
    float  detune_cents    = 0.0f;
    bool   reverse         = false;
    float  fade_in_ms      = 0.f;
    float  fade_out_ms     = 0.f;
    float  loop_crossfade_ms = 0.f;

    // --- warp (markers + transients drive the stretcher; stored + persisted) ---
    bool                                       warp_enabled = false;
    WarpMode                                   warp_mode    = WarpMode::Complex;
    std::vector<audio_clip_ed::WarpPoint>      warp_points;
    std::vector<audio_clip_ed::TransientPoint> transients;

    bool ok() const { return !L.empty(); }

    // Write `frames` of audio into outL/outR for a block that starts at
    // `block_start_beats` and advances `delta` beats (phase-locked loop). The played
    // window is the fraction [trim0, trim1] of the buffer, stretched to loop_beats.
    // Applies reverse, per-loop equal-power fade in/out, and gain.
    void render(double block_start_beats, double delta, uint32_t frames,
                float* outL, float* outR, float trim0 = 0.f, float trim1 = 1.f) const {
        if (L.empty() || loop_beats <= 0.0) return;
        const double N = static_cast<double>(L.size());
        double a = std::min(std::max(static_cast<double>(trim0), 0.0), 1.0) * N;
        double b = std::min(std::max(static_cast<double>(trim1), 0.0), 1.0) * N;
        if (b <= a + 1.0) { a = 0.0; b = N; }   // degenerate -> whole buffer
        const double span = b - a;
        const size_t wrap = static_cast<size_t>(a);
        const bool stereo = !R.empty();

        // Fade lengths in window-samples (0 = no fade). Guarded against overlap.
        const double half = span * 0.5;
        const double fin  = std::min(half, fade_in_ms  > 0.f && sr ? fade_in_ms  * 1e-3 * sr : 0.0);
        const double fout = std::min(half, fade_out_ms > 0.f && sr ? fade_out_ms * 1e-3 * sr : 0.0);

        for (uint32_t i = 0; i < frames; ++i) {
            double beat = block_start_beats + delta * static_cast<double>(i) / static_cast<double>(frames);
            double ph = std::fmod(beat, loop_beats);
            if (ph < 0) ph += loop_beats;
            const double posf = ph / loop_beats * span;     // 0..span into the window (playback time)
            const double pos  = reverse ? (span - posf) : posf;   // read position (mirrored if reverse)
            const double rp   = a + pos;
            size_t i0 = static_cast<size_t>(rp);
            if (i0 >= L.size()) i0 = wrap;   // rp can reach b (== N when trim1=1, esp. reverse) -> wrap
            const double fr = rp - std::floor(rp);
            const size_t i1 = (i0 + 1 < L.size()) ? i0 + 1 : wrap;

            float amp = gain;                                 // fades apply to playback time (posf)
            if (fin  > 0.0 && posf < fin)         amp *= audio_clip_ed::equal_power_fade_in(static_cast<float>(posf / fin));
            if (fout > 0.0 && span - posf < fout) amp *= audio_clip_ed::equal_power_fade_in(static_cast<float>((span - posf) / fout));

            const float sl = static_cast<float>(L[i0] * (1.0 - fr) + L[i1] * fr) * amp;
            outL[i] = sl;
            outR[i] = stereo ? static_cast<float>(R[i0] * (1.0 - fr) + R[i1] * fr) * amp : sl;
        }
    }
};

// Procedural demo loops, one bar (4 beats) at the given sample rate / tempo.
AudioClip gen_sub_pulse(uint32_t sr, double bpm);
AudioClip gen_noise_sweep(uint32_t sr, double bpm);
AudioClip gen_bell_loop(uint32_t sr, double bpm);

// Decode a WAV (via miniaudio); warps the loop length to the nearest whole bar.
bool audio_clip_load_wav(const std::string& path, uint32_t sr_hint, double bpm, AudioClip& out);

}  // namespace vivid::session
