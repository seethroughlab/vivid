// Headless test for the A1 audio-clip playback shaping (gain / reverse / fades) in
// AudioClip::render — no audio device needed, it's pure math over vectors.
#include "audio/audio_clip.h"
#include "test_helpers.h"
#include <vector>

using vivid::session::AudioClip;

// Render one full loop's worth of frames starting at beat 0.
static void render_loop(const AudioClip& s, std::vector<float>& L, std::vector<float>& R, uint32_t frames) {
    L.assign(frames, 0.f); R.assign(frames, 0.f);
    const double delta = s.loop_beats;   // advance exactly one loop over `frames`
    s.render(0.0, delta, frames, L.data(), R.data(), 0.f, 1.f);
}

int main() {
    const uint32_t SR = 48000, F = 2048;

    // Constant-0.5 buffer: interpolation is flat, so shaping is easy to read.
    auto make_const = [&](float v) {
        AudioClip s; s.sr = SR; s.loop_beats = 4.0; s.L.assign(SR, v); return s;   // 1s mono
    };

    // gain scales the output uniformly.
    {
        AudioClip s = make_const(0.5f); s.gain = 1.0f;
        std::vector<float> L, R; render_loop(s, L, R, F);
        CHECK_NEAR(L[F / 2], 0.5, 1e-4);
        s.gain = 0.5f; render_loop(s, L, R, F);
        CHECK_NEAR(L[F / 2], 0.25, 1e-4);   // halved
        CHECK_NEAR(R[F / 2], 0.25, 1e-4);   // mono duplicated to R
    }

    // fade-in attenuates the first samples; the middle is untouched.
    {
        AudioClip s = make_const(0.5f);
        s.fade_in_ms = 200.f;   // long fade so the first frame is clearly down
        std::vector<float> L, R; render_loop(s, L, R, F);
        CHECK(L[0] < 0.1f);                 // near silence at the very start
        CHECK_NEAR(L[F / 2], 0.5, 1e-3);    // mid-loop unaffected
        CHECK(L[2] < L[F / 4]);             // rising toward full
    }

    // fade-out attenuates the last samples.
    {
        AudioClip s = make_const(0.5f);
        s.fade_out_ms = 200.f;
        std::vector<float> L, R; render_loop(s, L, R, F);
        CHECK_NEAR(L[F / 2], 0.5, 1e-3);
        CHECK(L[F - 1] < 0.1f);             // near silence at the end
    }

    // reverse plays the window backwards: a ramp buffer reads high near the loop start.
    {
        AudioClip s; s.sr = SR; s.loop_beats = 4.0;
        s.L.resize(SR); for (uint32_t i = 0; i < SR; ++i) s.L[i] = static_cast<float>(i) / SR;  // 0..1 ramp
        std::vector<float> Lf, Rf, Lr, Rr;
        s.reverse = false; render_loop(s, Lf, Rf, F);
        s.reverse = true;  render_loop(s, Lr, Rr, F);
        CHECK(Lf[1] < 0.1f);               // forward: starts near 0
        CHECK(Lr[1] > 0.9f);               // reversed: starts near 1
    }

    // Defaults are unchanged: gain 1, no fade/reverse -> the plain looper value.
    {
        AudioClip s = make_const(0.42f);
        std::vector<float> L, R; render_loop(s, L, R, F);
        CHECK_NEAR(L[F / 2], 0.42, 1e-4);
    }

    return vivid::test::summary("test_audio_clip");
}
