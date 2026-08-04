#pragma once
// Pure onset/transient detection — the json-free core split out of audio_clip_shared.h so the audio ops
// (the Sampler's auto-slice, ADR-0049) can detect onsets on PCM without pulling nlohmann/json into every
// audio-op translation unit. audio_clip_shared.h includes this, so its clients see these symbols
// unchanged. No GLFW/WGPU/json/runtime headers — fully unit-testable.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace audio_clip_ed {

struct TransientPoint {
    uint32_t source_sample = 0;
    float strength = 0.0f;
};

struct SliceRegion {
    uint32_t start = 0;
    uint32_t end = 0;
};

// Detect onset frames via a simple envelope-follower + rising-edge threshold. `sensitivity` 0..1
// (higher = more onsets). Averages |L|+|R|; a min gap of sr/50 avoids double-triggering one hit.
inline std::vector<TransientPoint> detect_transients(const std::vector<float>& left,
                                                     const std::vector<float>& right,
                                                     uint32_t sample_rate,
                                                     float sensitivity) {
    std::vector<TransientPoint> out;
    const size_t n = std::min(left.size(), right.size());
    if (n < 8 || sample_rate == 0) return out;
    const uint32_t min_gap = std::max(1u, sample_rate / 50u);
    const float threshold = 0.04f + (1.0f - std::clamp(sensitivity, 0.0f, 1.0f)) * 0.24f;
    float env = 0.0f;
    uint32_t last = 0;
    bool have_last = false;
    for (uint32_t i = 0; i < n; ++i) {
        const float mag = 0.5f * (std::fabs(left[i]) + std::fabs(right[i]));
        const float delta = mag - env;
        env = std::max(mag, env * 0.96f);
        if (delta > threshold && (!have_last || i - last >= min_gap)) {
            out.push_back({i, std::clamp(delta, 0.0f, 1.0f)});
            last = i;
            have_last = true;
        }
    }
    return out;
}

}  // namespace audio_clip_ed
