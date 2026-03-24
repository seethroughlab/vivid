#pragma once

#include "sample_bank.h"
#include "operator_api/adsr.h"
#include <algorithm>
#include <cmath>

namespace vivid_sampler {

// ---------------------------------------------------------------------------
// Voice — a single sample playback instance
// ---------------------------------------------------------------------------

struct Voice {
    bool active = false;
    int note = -1;
    float velocity = 1.0f;
    double playback_pos = 0.0;
    double playback_rate = 1.0;
    vivid::adsr::State envelope;
    const SampleRegion* region = nullptr;
    bool one_shot = false;
    uint64_t start_frame = 0;
};

// ---------------------------------------------------------------------------
// Voice lifecycle
// ---------------------------------------------------------------------------

inline void voice_note_on(Voice& v, int note, float velocity,
                          const SampleRegion* region, double playback_rate,
                          uint64_t frame, bool one_shot) {
    v.active = true;
    v.note = note;
    v.velocity = velocity;
    v.region = region;
    v.playback_rate = playback_rate;
    v.playback_pos = 0.0;
    v.one_shot = one_shot;
    v.start_frame = frame;
    vivid::adsr::gate_on(v.envelope);
}

inline void voice_note_off(Voice& v) {
    if (v.one_shot) return;
    vivid::adsr::gate_off(v.envelope);
}

// ---------------------------------------------------------------------------
// Per-sample rendering
// ---------------------------------------------------------------------------

inline void voice_render_frame(Voice& v, float& out_L, float& out_R,
                               float dt, float attack, float decay,
                               float sustain, float release) {
    if (!v.active || !v.region || !v.region->data) return;

    const auto& data = *v.region->data;
    size_t num_frames = data.samples_L.size();
    if (num_frames == 0) { v.active = false; return; }

    // Bounds check
    if (v.playback_pos >= static_cast<double>(num_frames)) {
        if (v.region->loop_enabled && v.region->loop_end > v.region->loop_start) {
            // Wrap into loop region
            double loop_len = static_cast<double>(v.region->loop_end - v.region->loop_start);
            v.playback_pos = v.region->loop_start +
                std::fmod(v.playback_pos - v.region->loop_start, loop_len);
        } else {
            v.active = false;
            return;
        }
    }

    // Linear interpolation
    size_t idx = static_cast<size_t>(v.playback_pos);
    float frac = static_cast<float>(v.playback_pos - static_cast<double>(idx));

    size_t idx_next = idx + 1;
    if (idx_next >= num_frames) {
        if (v.region->loop_enabled && v.region->loop_end > v.region->loop_start) {
            idx_next = v.region->loop_start;
        } else {
            idx_next = idx; // clamp at end
        }
    }

    float sample_L = data.samples_L[idx] * (1.0f - frac) + data.samples_L[idx_next] * frac;
    float sample_R;
    if (data.stereo) {
        sample_R = data.samples_R[idx] * (1.0f - frac) + data.samples_R[idx_next] * frac;
    } else {
        sample_R = sample_L;
    }

    // Apply region volume and velocity
    float gain = v.velocity * db_to_linear(v.region->volume_db);
    sample_L *= gain;
    sample_R *= gain;

    // Apply pan (-1.0 = full left, 0.0 = center, 1.0 = full right)
    float pan = v.region->pan;
    float pan_L = 1.0f - std::max(0.0f, pan);  // 1.0 at center/left, fades toward 0 at right
    float pan_R = 1.0f + std::min(0.0f, pan);  // 1.0 at center/right, fades toward 0 at left
    sample_L *= pan_L;
    sample_R *= pan_R;

    // Advance ADSR and apply envelope
    vivid::adsr::advance(v.envelope, dt, attack, decay, sustain, release);
    sample_L *= v.envelope.env_value;
    sample_R *= v.envelope.env_value;

    // Advance playback position
    v.playback_pos += v.playback_rate;

    // Handle loop wrapping
    if (v.region->loop_enabled && v.region->loop_end > v.region->loop_start) {
        if (v.playback_pos >= static_cast<double>(v.region->loop_end)) {
            double loop_len = static_cast<double>(v.region->loop_end - v.region->loop_start);
            v.playback_pos = v.region->loop_start +
                std::fmod(v.playback_pos - v.region->loop_start, loop_len);
        }
    }

    // Deactivate if envelope reached IDLE
    if (v.envelope.stage == vivid::adsr::IDLE) {
        v.active = false;
    }

    // Accumulate (not assign) — allows summing multiple voices
    out_L += sample_L;
    out_R += sample_R;
}

// ---------------------------------------------------------------------------
// Voice allocation
// ---------------------------------------------------------------------------

inline int find_free_voice(Voice* voices, int count) {
    for (int i = 0; i < count; ++i) {
        if (!voices[i].active) return i;
    }
    return -1;
}

inline int steal_oldest_voice(Voice* voices, int count) {
    if (count <= 0) return -1;
    int oldest = 0;
    for (int i = 1; i < count; ++i) {
        if (voices[i].start_frame < voices[oldest].start_frame) {
            oldest = i;
        }
    }
    return oldest;
}

// ---------------------------------------------------------------------------
// Gate edge detection
// ---------------------------------------------------------------------------

struct GateTracker {
    static constexpr int kMaxSlots = 16;
    float prev_gates[kMaxSlots] = {};
    uint32_t prev_len = 0;

    // Returns: +1 = rising edge (note on), -1 = falling edge (note off), 0 = no change
    int detect(int slot, float current_gate) const {
        if (slot < 0 || slot >= kMaxSlots) return 0;
        float prev = (static_cast<uint32_t>(slot) < prev_len) ? prev_gates[slot] : 0.0f;
        if (current_gate > 0.5f && prev <= 0.5f) return 1;
        if (current_gate <= 0.5f && prev > 0.5f) return -1;
        return 0;
    }

    // Call at end of buffer to update state. Releases disappeared slots.
    void update(const float* gate_data, uint32_t len) {
        // Release any slots that disappeared (spread shrank)
        for (uint32_t i = len; i < prev_len; ++i) {
            prev_gates[i] = 0.0f;
        }
        // Copy current gates
        uint32_t copy_len = std::min(len, static_cast<uint32_t>(kMaxSlots));
        for (uint32_t i = 0; i < copy_len; ++i) {
            prev_gates[i] = gate_data ? gate_data[i] : 0.0f;
        }
        prev_len = copy_len;
    }
};

// ---------------------------------------------------------------------------
// Spread input helper
// ---------------------------------------------------------------------------

struct SpreadInput {
    const float* data = nullptr;
    uint32_t length = 0;
};

inline SpreadInput read_spread_input(const VividSpreadPort* spreads, int port_index) {
    SpreadInput result;
    if (spreads) {
        result.data = spreads[port_index].data;
        result.length = spreads[port_index].length;
    }
    return result;
}

} // namespace vivid_sampler
