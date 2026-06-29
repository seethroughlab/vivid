#pragma once

#include "operator_api/types.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace vivid {

// ── Canonical timing vocabulary ─────────────────────────────────────────
//
// Every time-driven operator selects its clock with a single `clock_mode`
// param. There are two label sets: the "full" set for operators with a
// meaningful internal clock (a frequency/rate param), and the "synced" set
// for operators that only make sense driven by an external or metronome
// clock. Numeric values are stable so saved-graph param ints never need
// remapping when an operator switches to this vocabulary.
constexpr int kClockModeInternal  = 0;  // own frequency / time
constexpr int kClockModeExternal  = 1;  // beat_phase input port
constexpr int kClockModeMetronome = 2;  // global metronome + sync_division

// For the 2-option "synced" shape (no internal clock):
constexpr int kClockModeSyncedExternal  = 0;  // beat_phase input port
constexpr int kClockModeSyncedMetronome = 1;  // global metronome

struct MetronomeTransport {
    float bpm = 120.0f;
    int beats_per_bar = 4;
    double beats_elapsed = 0.0;
    float beat_phase = 0.0f;
    float bar_phase = 0.0f;
    float beat_ms = 500.0f;
};

// Declared here for API parity; the inline definition lives in gpu_operator.h
// after VividGpuContext is fully defined.
MetronomeTransport metronome_transport(const VividGpuContext* ctx);

// Canonical mode-selector labels. The "full" set is for operators with an
// internal clock; the "synced" set is for operators that only sync.
inline std::vector<std::string> clock_mode_full_labels() {
    return {"internal", "external", "metronome"};
}

inline std::vector<std::string> clock_mode_synced_labels() {
    return {"external", "metronome"};
}

// For operators that are self-clocked or metronome-locked but take no external
// clock input (e.g. Delay, Clock): {internal=0, metronome=1}.
inline std::vector<std::string> clock_mode_tempo_labels() {
    return {"internal", "metronome"};
}

inline std::vector<std::string> metronome_division_labels() {
    return {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
            "dotted 1/4", "dotted 1/8", "dotted 1/16",
            "triplet 1/4", "triplet 1/8", "triplet 1/16"};
}

inline MetronomeTransport metronome_transport(const VividFrameContext* ctx) {
    MetronomeTransport out{};
    if (!ctx) return out;
    out.bpm = ctx->metronome_bpm;
    out.beats_per_bar = static_cast<int>(ctx->metronome_beats_per_bar);
    out.beats_elapsed = ctx->metronome_beats_elapsed;
    out.beat_phase = ctx->metronome_beat_phase;
    out.bar_phase = ctx->metronome_bar_phase;
    out.beat_ms = ctx->metronome_beat_ms;
    return out;
}

inline MetronomeTransport metronome_transport(const VividAudioContext* ctx) {
    MetronomeTransport out{};
    if (!ctx) return out;
    out.bpm = ctx->metronome_bpm;
    out.beats_per_bar = static_cast<int>(ctx->metronome_beats_per_bar);
    out.beats_elapsed = ctx->metronome_beats_elapsed;
    out.beat_phase = ctx->metronome_beat_phase;
    out.bar_phase = ctx->metronome_bar_phase;
    out.beat_ms = ctx->metronome_beat_ms;
    return out;
}

inline MetronomeTransport metronome_transport_sample(const MetronomeTransport& base,
                                                     uint32_t sample_index,
                                                     uint32_t sample_rate) {
    MetronomeTransport out = base;
    if (sample_rate == 0 || out.bpm <= 0.0f) return out;

    const double beats_per_sample =
        (static_cast<double>(out.bpm) / 60.0) / static_cast<double>(sample_rate);
    const double beat_offset = static_cast<double>(sample_index) * beats_per_sample;
    out.beats_elapsed += beat_offset;
    out.beat_phase = static_cast<float>(std::fmod(static_cast<double>(out.beat_phase) + beat_offset, 1.0));
    return out;
}

inline float sync_cycle_beats(int division) {
    static constexpr float kDivisionDurations[] = {
        4.0f,    // 1/1
        2.0f,    // 1/2
        1.0f,    // 1/4
        0.5f,    // 1/8
        0.25f,   // 1/16
        0.125f,  // 1/32
        1.5f,    // dotted 1/4
        0.75f,   // dotted 1/8
        0.375f,  // dotted 1/16
        2.0f / 3.0f,   // triplet 1/4
        1.0f / 3.0f,   // triplet 1/8
        1.0f / 6.0f,   // triplet 1/16
    };
    int idx = std::clamp(division, 0, static_cast<int>(std::size(kDivisionDurations)) - 1);
    return kDivisionDurations[idx];
}

inline double cycle_phase_from_total_beats(double total_beats, int division, double phase_offset = 0.0) {
    const double cycle_beats = static_cast<double>(sync_cycle_beats(division));
    if (cycle_beats <= 0.0) return 0.0;
    double phase = std::fmod((total_beats / cycle_beats) + phase_offset, 1.0);
    if (phase < 0.0) phase += 1.0;
    return phase;
}

inline double advance_external_total_beats(float external_phase, float& prev_phase, int& beat_count) {
    float clamped_phase = std::clamp(external_phase, 0.0f, 1.0f);
    if ((clamped_phase - prev_phase) < -0.5f) {
        ++beat_count;
    }
    prev_phase = clamped_phase;
    return static_cast<double>(beat_count) + static_cast<double>(clamped_phase);
}

// `clock_mode` for synced-shape operators: external=0, metronome=1.
inline float resolve_clock_phase(int clock_mode,
                                 float external_phase,
                                 const MetronomeTransport& metronome) {
    if (clock_mode == kClockModeSyncedMetronome) {
        return metronome.beat_phase;
    }
    return external_phase;
}

inline float resolve_bar_phase(int clock_mode,
                               float external_bar_phase,
                               const MetronomeTransport& metronome) {
    if (clock_mode == kClockModeSyncedMetronome) {
        return metronome.bar_phase;
    }
    return external_bar_phase;
}

}  // namespace vivid
