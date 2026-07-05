// Headless smoke test for the A2 warp engine (ClipDsp + process_clip): the pitch-preserving
// stretch path must run allocation-safely and produce finite, non-silent output. (Actual
// pitch/tempo quality is verified by ear live; this guards the RT path.)
#include "audio/clip_dsp.h"
#include "test_helpers.h"
#include <cmath>
#include <vector>

using namespace vivid::session;

int main() {
    const uint32_t SR = 48000, F = 512;
    const double TWO_PI = 6.283185307179586;

    // A 2-second 220 Hz sine "clip".
    AudioClip c; c.sr = SR; c.loop_beats = 4.0; c.src_bpm = 120.0;
    const size_t N = SR * 2;
    c.L.resize(N); c.R.resize(N);
    for (size_t i = 0; i < N; ++i) { float v = static_cast<float>(std::sin(TWO_PI * 220.0 * i / SR)); c.L[i] = v; c.R[i] = v; }

    ClipDsp dsp; dsp.init(SR);
    CHECK(dsp.ready);

    const double delta = F * (120.0 / 60.0) / SR;   // beats advanced per block at 120 BPM
    float outL[F], outR[F];

    // Complex warp: run ~2s of blocks; output must stay finite and carry real energy.
    c.warp_enabled = true; c.warp_mode = WarpMode::Complex;
    {
        bool any_nan = false; double energy = 0.0; double beats = 0.0;
        for (int blk = 0; blk < 200; ++blk) {
            for (uint32_t i = 0; i < F; ++i) { outL[i] = outR[i] = 0.f; }
            process_clip(c, dsp, beats, delta, F, SR, outL, outR, 0.f, 1.f);
            for (uint32_t i = 0; i < F; ++i) { if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) any_nan = true; energy += outL[i] * outL[i]; }
            beats += delta;
        }
        CHECK(!any_nan);
        CHECK(energy > 1.0);   // stretcher produced a real signal (past its latency ramp)
    }

    // Pitch shift up an octave — still finite, still produces signal.
    c.pitch_semitones = 12.f; dsp.reset();
    {
        bool any_nan = false; double energy = 0.0; double beats = 0.0;
        for (int blk = 0; blk < 100; ++blk) {
            process_clip(c, dsp, beats, delta, F, SR, outL, outR, 0.f, 1.f);
            for (uint32_t i = 0; i < F; ++i) { if (!std::isfinite(outL[i])) any_nan = true; energy += outL[i] * outL[i]; }
            beats += delta;
        }
        CHECK(!any_nan);
        CHECK(energy > 1.0);
    }

    // Beats mode with a transient shouldn't crash (reset path exercised).
    c.warp_mode = WarpMode::Beats;
    c.transients = { { SR / 2, 1.0f }, { SR, 1.0f } };
    dsp.reset();
    {
        bool any_nan = false; double beats = 0.0;
        for (int blk = 0; blk < 100; ++blk) {
            process_clip(c, dsp, beats, delta, F, SR, outL, outR, 0.f, 1.f);
            for (uint32_t i = 0; i < F; ++i) if (!std::isfinite(outL[i])) any_nan = true;
            beats += delta;
        }
        CHECK(!any_nan);
    }

    // Warp disabled -> falls back to the A1 varispeed render(); sine stays finite + audible.
    c.warp_enabled = false; c.pitch_semitones = 0.f;
    {
        for (uint32_t i = 0; i < F; ++i) { outL[i] = outR[i] = 0.f; }
        process_clip(c, dsp, 0.0, delta, F, SR, outL, outR, 0.f, 1.f);
        bool any_nan = false; double energy = 0.0;
        for (uint32_t i = 0; i < F; ++i) { if (!std::isfinite(outL[i])) any_nan = true; energy += outL[i] * outL[i]; }
        CHECK(!any_nan);
        CHECK(energy > 0.01);
    }

    return vivid::test::summary("test_clip_dsp");
}
