// Headless test for the ADR-0032 Phase D2 hardware-input bus (audio/audio_input_bus.*): the lock-free
// SPSC stereo ring the RT callback writes and the AudioInput graph op drains. Pure — no device, no
// session — so it links in the deps-free suite. Verifies round-trip fidelity, planar deinterleave,
// underrun zero-padding, and the small-ring drop-on-full policy.
#include "audio/audio_input_bus.h"
#include "test_helpers.h"

#include <vector>

using vivid::audio_input_write;
using vivid::audio_input_pull;
using vivid::audio_input_reset;

int main() {
    audio_input_reset();

    // Empty ring → pull returns 0 and zero-fills the output.
    {
        float l[4] = { 9, 9, 9, 9 }, r[4] = { 9, 9, 9, 9 };
        const uint32_t got = audio_input_pull(l, r, 4);
        CHECK(got == 0);
        for (int i = 0; i < 4; ++i) { CHECK(l[i] == 0.f); CHECK(r[i] == 0.f); }
    }

    // Write one interleaved block, pull it back: L = even samples, R = odd samples (deinterleave).
    {
        const float in[8] = { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f };   // 4 stereo frames
        audio_input_write(in, 4);
        float l[4] = {}, r[4] = {};
        const uint32_t got = audio_input_pull(l, r, 4);
        CHECK(got == 4);
        CHECK(l[0] == 0.1f); CHECK(r[0] == -0.1f);
        CHECK(l[1] == 0.2f); CHECK(r[1] == -0.2f);
        CHECK(l[2] == 0.3f); CHECK(r[2] == -0.3f);
        CHECK(l[3] == 0.4f); CHECK(r[3] == -0.4f);
    }

    // Underrun: write 2 frames, ask for 4 → 2 real + 2 zero-padded.
    {
        audio_input_reset();
        const float in[4] = { 1.f, 2.f, 3.f, 4.f };   // 2 frames
        audio_input_write(in, 2);
        float l[4] = { 7, 7, 7, 7 }, r[4] = { 7, 7, 7, 7 };
        const uint32_t got = audio_input_pull(l, r, 4);
        CHECK(got == 2);
        CHECK(l[0] == 1.f); CHECK(r[0] == 2.f);
        CHECK(l[1] == 3.f); CHECK(r[1] == 4.f);
        CHECK(l[2] == 0.f); CHECK(r[2] == 0.f);   // zero-padded
        CHECK(l[3] == 0.f); CHECK(r[3] == 0.f);
    }

    // Steady-state balance: write N then read N, many times, never overflowing (the common case).
    {
        audio_input_reset();
        std::vector<float> block(2 * 512, 0.5f);
        for (int iter = 0; iter < 100; ++iter) {
            audio_input_write(block.data(), 512);
            float l[512] = {}, r[512] = {};
            const uint32_t got = audio_input_pull(l, r, 512);
            CHECK(got == 512);
            CHECK(l[0] == 0.5f); CHECK(r[511] == 0.5f);
        }
    }

    // Drop-on-full: a stalled consumer (no pulls) must not grow unbounded — writes past the small ring
    // are dropped rather than blocking or corrupting. After overfilling, a drain yields exactly what the
    // ring could hold (never more than capacity, never a crash).
    {
        audio_input_reset();
        std::vector<float> big(2 * 4096, 1.f);
        audio_input_write(big.data(), 4096);   // fits (< capacity 8192)
        audio_input_write(big.data(), 4096);   // now near/over full — the tail is dropped, no crash
        audio_input_write(big.data(), 4096);   // further writes drop
        std::vector<float> l(9000), r(9000);
        const uint32_t got = audio_input_pull(l.data(), r.data(), 9000);
        CHECK(got < 8192);       // bounded by ring capacity — no unbounded growth
        CHECK(got >= 4096);      // at least the first block survived
    }

    return vivid::test::summary("test_audio_input_bus");
}
