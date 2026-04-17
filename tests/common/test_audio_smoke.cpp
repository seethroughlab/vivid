#include "audio/audio_smoke.h"

#include "test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

std::vector<float> fill_sine(float amp, float hz, uint32_t frames, uint32_t channels) {
    std::vector<float> buf(static_cast<size_t>(frames) * channels);
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        const float s = amp * std::sin(2.0f * 3.14159265358979323846f * hz * t);
        for (uint32_t c = 0; c < channels; ++c) buf[i * channels + c] = s;
    }
    return buf;
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_audio_smoke ===\n");

    // Active, well-behaved signal: should pass defaults.
    {
        auto buf = fill_sine(0.35f, 440.0f, kFrames, kChannels);
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(r.ok, "active sine passes default smoke spec");
        check(r.rms > 0.2f && r.rms < 0.3f, "active sine RMS roughly 0.25");
        check(r.peak < 0.4f, "active sine peak near 0.35");
        check(r.dc_ratio < 0.15f, "active sine DC ratio bounded");
    }

    // Silent input: must fail by default.
    {
        std::vector<float> buf(kFrames * kChannels, 0.0f);
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(!r.ok, "silent input fails by default (non-silent expected)");
        check(r.reason != nullptr, "silent failure has reason string");
    }

    // Silent input with allow_silent=true: should pass.
    {
        std::vector<float> buf(kFrames * kChannels, 0.0f);
        vivid::audio_smoke::Spec spec{};
        spec.allow_silent = true;
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels, spec);
        check(r.ok, "silent input passes when allow_silent=true");
    }

    // NaN in output: must fail.
    {
        auto buf = fill_sine(0.3f, 220.0f, kFrames, kChannels);
        buf[100] = std::numeric_limits<float>::quiet_NaN();
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(!r.ok, "NaN in output fails smoke");
        check(r.nan_count >= 1, "NaN count reports at least one sample");
    }

    // Infinity in output: must fail.
    {
        auto buf = fill_sine(0.3f, 220.0f, kFrames, kChannels);
        buf[200] = std::numeric_limits<float>::infinity();
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(!r.ok, "Inf in output fails smoke");
        check(r.inf_count >= 1, "Inf count reports at least one sample");
    }

    // Amplitude blowup: peak above ceiling must fail.
    {
        std::vector<float> buf(kFrames * kChannels, 15.0f);
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(!r.ok, "peak above ceiling fails smoke");
    }

    // DC-heavy output: must fail.
    {
        // Mean 0.5, peak 0.5 → dc_ratio = 1.0 > 0.1 default
        std::vector<float> buf(kFrames * kChannels, 0.5f);
        const auto r = vivid::audio_smoke::check(buf.data(), kFrames, kChannels);
        check(!r.ok, "pure-DC output fails smoke");
    }

    // Block-at-a-time equivalence: feeding one-shot vs. per-block gives the
    // same stats and same pass/fail verdict.
    {
        const auto whole = fill_sine(0.25f, 330.0f, kFrames * 4, kChannels);
        const auto r_one = vivid::audio_smoke::check(whole.data(), kFrames * 4, kChannels);
        vivid::audio_smoke::Accum a{};
        for (int block = 0; block < 4; ++block) {
            const float* p = whole.data() + block * kFrames * kChannels;
            vivid::audio_smoke::check_block(a, p, kFrames, kChannels);
        }
        const auto r_multi = vivid::audio_smoke::finalize(a, vivid::audio_smoke::Spec{});
        check(r_one.ok && r_multi.ok, "one-shot and per-block both pass for active signal");
        check(std::fabs(r_one.rms - r_multi.rms) < 1.0e-6f, "one-shot and per-block RMS agree");
        check(std::fabs(r_one.peak - r_multi.peak) < 1.0e-6f, "one-shot and per-block peak agree");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
