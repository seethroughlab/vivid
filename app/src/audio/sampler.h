#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

// A minimal transport-locked sampler: holds stereo float PCM and reads it phase-
// locked to the master beat (loop spans `loop_beats`). The audio-clip counterpart
// to a MIDI clip — the audio thread calls render() each block.
namespace vivid_poc {

struct Sampler {
    std::vector<float> L, R;       // PCM (R empty => mono)
    double             loop_beats = 4.0;
    double             src_bpm = 0.0;   // source tempo (0 = generated / unknown)
    std::string        name;

    bool ok() const { return !L.empty(); }

    // Write `frames` of audio into outL/outR for a block that starts at
    // `block_start_beats` and advances `delta` beats (phase-locked loop). The
    // played window is the fraction [trim0, trim1] of the buffer, stretched to
    // loop_beats (so trimming = choosing the looped region).
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
        for (uint32_t i = 0; i < frames; ++i) {
            double beat = block_start_beats + delta * static_cast<double>(i) / static_cast<double>(frames);
            double ph = std::fmod(beat, loop_beats);
            if (ph < 0) ph += loop_beats;
            const double pos = a + ph / loop_beats * span;
            const size_t i0 = static_cast<size_t>(pos);
            const double fr = pos - static_cast<double>(i0);
            const size_t i1 = (i0 + 1 < L.size()) ? i0 + 1 : wrap;
            outL[i] = static_cast<float>(L[i0] * (1.0 - fr) + L[i1] * fr);
            outR[i] = stereo ? static_cast<float>(R[i0] * (1.0 - fr) + R[i1] * fr) : outL[i];
        }
    }
};

// Procedural demo loops, one bar (4 beats) at the given sample rate / tempo.
Sampler gen_sub_pulse(uint32_t sr, double bpm);
Sampler gen_noise_sweep(uint32_t sr, double bpm);
Sampler gen_bell_loop(uint32_t sr, double bpm);

// Decode a WAV (via miniaudio); warps the loop length to the nearest whole bar.
bool sampler_load_wav(const std::string& path, uint32_t sr_hint, double bpm, Sampler& out);

}  // namespace vivid_poc
