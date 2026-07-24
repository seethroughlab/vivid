#pragma once
// Sample-playback voice: linear-interpolated repitch, forward/reverse, loop with
// equal-power crossfade, per-region gain/pan/velocity, and ADSR. Ported from
// vivid-classic (operators/shared/sampler_common/voice.h); the classic
// GateTracker (lane-array edge detector) is dropped — this app drives voices from
// note events via the VoiceTable, not from a gate lane.
#include "audio/sample_engine/sample_data.h"
#include "audio/sample_engine/adsr.h"
#include "audio/sample_engine/voice_table.h"
#include <algorithm>
#include <cmath>

namespace vivid {
namespace sample_engine {

// A single sample-playback instance. Inherits slot bookkeeping from
// vivid::VoiceSlot so it stays compatible with the shared allocator while
// carrying sampler-specific extras (envelope, region, playback cursor).
struct Voice : public vivid::VoiceSlot {
    double playback_pos = 0.0;
    double playback_rate = 1.0;
    vivid::adsr::State envelope;
    const SampleRegion* region = nullptr;
    bool one_shot = false;
};

struct VoiceRenderOptions {
    bool reverse = false;
    bool force_loop = false;
    uint32_t loop_crossfade_frames = 0;
};

// ---- Voice lifecycle -----------------------------------------------------

inline void voice_note_on(Voice& v, int note, float velocity,
                          const SampleRegion* region, double playback_rate,
                          uint64_t frame, bool one_shot,
                          bool reverse = false) {
    v.active = true;
    v.gate = true;
    v.note = note;
    v.velocity = velocity;
    v.region = region;
    v.playback_rate = playback_rate;
    if (reverse && region && region->data && !region->data->samples_L.empty()) {
        const uint32_t sample_end = static_cast<uint32_t>(region->data->samples_L.size());
        const uint32_t end = (region->loop_enabled && region->loop_end > region->loop_start)
            ? std::min(region->loop_end, sample_end)
            : sample_end;
        v.playback_pos = static_cast<double>(end > 0 ? end - 1 : 0);
    } else {
        v.playback_pos = 0.0;
    }
    v.one_shot = one_shot;
    v.start_frame = frame;
    vivid::adsr::gate_on(v.envelope);
}

inline void voice_note_off(Voice& v) {
    if (v.one_shot) return;
    v.gate = false;
    vivid::adsr::gate_off(v.envelope);
}

// ---- Per-sample rendering ------------------------------------------------

// Optional per-call expression scales:
//   rate_scale — multiplier on playback_rate (>1 = pitch up)
//   gain_scale — multiplier on per-voice output level
// Both default to 1.0. Accumulates (does not assign) into out_L/out_R so many
// voices can sum into the same buffer.
inline void voice_render_frame(Voice& v, float& out_L, float& out_R,
                               float dt, float attack, float decay,
                               float sustain, float release,
                               float rate_scale = 1.0f,
                               float gain_scale = 1.0f,
                               VoiceRenderOptions options = {}) {
    if (!v.active || !v.region || !v.region->data) return;

    const auto& data = *v.region->data;
    size_t num_frames = data.samples_L.size();
    if (num_frames == 0) { v.active = false; v.gate = false; return; }

    const bool has_region_loop = v.region->loop_enabled &&
                                 v.region->loop_end > v.region->loop_start;
    const bool loop_active = has_region_loop || options.force_loop;
    const uint32_t loop_start = has_region_loop
        ? std::min(v.region->loop_start, static_cast<uint32_t>(num_frames - 1))
        : 0u;
    const uint32_t requested_loop_end = has_region_loop
        ? v.region->loop_end
        : static_cast<uint32_t>(num_frames);
    const uint32_t loop_end = std::max(loop_start + 1,
        std::min(requested_loop_end, static_cast<uint32_t>(num_frames)));
    const double loop_len = static_cast<double>(loop_end - loop_start);

    auto wrap_forward = [&]() {
        v.playback_pos = static_cast<double>(loop_start) +
            std::fmod(v.playback_pos - static_cast<double>(loop_start), loop_len);
    };
    auto wrap_reverse = [&]() {
        double overshoot = static_cast<double>(loop_start) - v.playback_pos;
        overshoot = std::fmod(overshoot, loop_len);
        v.playback_pos = static_cast<double>(loop_end - 1) - overshoot;
    };

    // Bounds check and loop wrapping.
    if (options.reverse) {
        if (loop_active && v.playback_pos < static_cast<double>(loop_start)) {
            wrap_reverse();
        } else if (v.playback_pos < 0.0) {
            v.active = false; v.gate = false; return;
        }
    } else {
        if (loop_active && v.playback_pos >= static_cast<double>(loop_end)) {
            wrap_forward();
        } else if (v.playback_pos >= static_cast<double>(num_frames)) {
            v.active = false; v.gate = false; return;
        }
    }

    auto interp = [&](double pos, float& l, float& r) {
        pos = std::clamp(pos, 0.0, static_cast<double>(num_frames - 1));
        const size_t idx = static_cast<size_t>(pos);
        const float frac = static_cast<float>(pos - static_cast<double>(idx));
        size_t idx_next = idx + 1;
        if (idx_next >= num_frames) idx_next = loop_active ? loop_start : idx;
        l = data.samples_L[idx] * (1.0f - frac) + data.samples_L[idx_next] * frac;
        if (data.stereo)
            r = data.samples_R[idx] * (1.0f - frac) + data.samples_R[idx_next] * frac;
        else
            r = l;
    };

    float sample_L = 0.0f, sample_R = 0.0f;
    interp(v.playback_pos, sample_L, sample_R);

    const uint32_t xfade_frames = loop_active
        ? std::min<uint32_t>(options.loop_crossfade_frames,
                             static_cast<uint32_t>(std::max(1.0, loop_len * 0.5)))
        : 0u;
    if (xfade_frames > 0) {
        constexpr float kHalfPi = 1.57079632679f;
        float t = -1.0f;
        double blend_pos = v.playback_pos;
        if (options.reverse) {
            const double fade_end = static_cast<double>(loop_start + xfade_frames);
            if (v.playback_pos < fade_end) {
                t = static_cast<float>((fade_end - v.playback_pos) / static_cast<double>(xfade_frames));
                blend_pos = static_cast<double>(loop_end - 1) -
                            (static_cast<double>(loop_start) - v.playback_pos);
            }
        } else {
            const double fade_start = static_cast<double>(loop_end - xfade_frames);
            if (v.playback_pos >= fade_start) {
                t = static_cast<float>((v.playback_pos - fade_start) / static_cast<double>(xfade_frames));
                blend_pos = static_cast<double>(loop_start) + (v.playback_pos - fade_start);
            }
        }
        if (t >= 0.0f) {
            t = std::clamp(t, 0.0f, 1.0f);
            float blend_L = 0.0f, blend_R = 0.0f;
            interp(blend_pos, blend_L, blend_R);
            const float a = std::cos(t * kHalfPi);
            const float b = std::sin(t * kHalfPi);
            sample_L = sample_L * a + blend_L * b;
            sample_R = sample_R * a + blend_R * b;
        }
    }

    // Region volume + velocity.
    const float gain = v.velocity * db_to_linear(v.region->volume_db);
    sample_L *= gain;
    sample_R *= gain;

    // Pan (-1 = full left, 0 = center, 1 = full right).
    const float pan = v.region->pan;
    sample_L *= 1.0f - std::max(0.0f, pan);
    sample_R *= 1.0f + std::min(0.0f, pan);

    // Advance + apply the amplitude envelope.
    vivid::adsr::advance(v.envelope, dt, attack, decay, sustain, release);
    const float env_scaled = v.envelope.env_value * gain_scale;
    sample_L *= env_scaled;
    sample_R *= env_scaled;

    // Advance the playback cursor.
    const double advance = v.playback_rate * static_cast<double>(rate_scale);
    v.playback_pos += options.reverse ? -advance : advance;

    // Loop wrap after large advances.
    if (loop_active) {
        if (options.reverse && v.playback_pos < static_cast<double>(loop_start)) wrap_reverse();
        else if (!options.reverse && v.playback_pos >= static_cast<double>(loop_end)) wrap_forward();
    }

    if (v.envelope.stage == vivid::adsr::IDLE) { v.active = false; v.gate = false; }

    out_L += sample_L;
    out_R += sample_R;
}

// ---- Voice allocation over a parallel Voice[] array ----------------------

inline int find_free_voice(Voice* voices, int count) {
    for (int i = 0; i < count; ++i)
        if (!voices[i].active) return i;
    return -1;
}

inline int steal_oldest_voice(Voice* voices, int count) {
    if (count <= 0) return -1;
    int oldest = 0;
    for (int i = 1; i < count; ++i)
        if (voices[i].start_frame < voices[oldest].start_frame) oldest = i;
    return oldest;
}

} // namespace sample_engine
} // namespace vivid
