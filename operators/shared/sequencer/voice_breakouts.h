#pragma once

// Voice-breakout helper — emits the standardized per-voice control lanes
// (voice_ids, voice_gates, voice_velocities, voice_freqs, plus the Phase 4
// expression lanes voice_pitch_bend, voice_pressure, voice_timbre) from a
// vivid::VoiceTable<N>'s active slots, sorted by note_id ascending.
//
// Used by every voice synth (FmSynth, Sampler, SP404, Slicer, AnalogOsc,
// WavetableOsc, WavetableLayer, ...) and by NoteBreakout. Centralizes the
// active-voice sort + lane resize/commit pattern so cross-operator
// ordering stays identical — a hard requirement for graphs that fan one
// note stream into multiple synths and expect their voice_* lanes to line
// up by note_id (cross-cutting decision #4 in the migration plan).
//
// Audio-thread safe: stack-allocated index buffer (bounded by N ≤ 16 in
// practice), no allocations.
//
// Lane order in the array passed to emit_voice_breakouts:
//   [0] voice_ids
//   [1] voice_gates
//   [2] voice_velocities
//   [3] voice_freqs           (frequency in Hz, includes pitch_bend folded in)
//   [4] voice_pitch_bend      (raw pitch_bend_semis from the slot)
//   [5] voice_pressure        (slot.pressure 0..1)
//   [6] voice_timbre          (slot.timbre   0..1)
//
// Synths declare only the first 4 lane-array OUTPUT ports and pass a
// `lanes[kVoiceBreakoutLaneCount]` array with slots [4..6] default-init —
// the helper skips entries whose handle/resize is null, so the expression
// lanes are silently omitted. NoteBreakout exposes all 7 ports (voice
// synths fold pitch_bend into voice_freqs already, and pressure/timbre
// have no fixed mapping per synth — surfacing them on NoteBreakout lets
// downstream operators bind them however they want).
//
// ---------------------------------------------------------------------------
// IMPORTANT: indexing into ctx->output_lanes[]
// ---------------------------------------------------------------------------
// `ctx->output_lanes[]` is sized and indexed by OVERALL output port position
// (graph_compiler.cpp resizes by `output_port_count`, which counts every
// output port — audio buffers, custom_ref, lane arrays — in declaration
// order). The runtime calls `make_lane_output()` for every output port,
// so non-lane slots have a valid LaneBuffer handle even though the audio /
// custom_ref data flows through a separate path. There is no "skip on null"
// for these slots — writing into an audio port's lane handle silently
// corrupts data into a buffer no one reads, and shifts every subsequent
// emit call into the wrong lane port.
//
// To pass the right slice to this helper, count how many non-lane OUTPUT
// ports your operator declares before its first voice_* lane port and use
// that as the starting index. For example:
//   - Operator with `output` (audio) → start at output_lanes[1]
//   - Operator with `output` + `voices_out` (audio) → start at output_lanes[2]
//   - NoteBreakout with `notes_out` (custom_ref) → start at output_lanes[1]
//
// Add a brief comment at each call site recording the offset and reason,
// since `output_lanes[0..3]` looks plausible until you trace the indexing.
//
// See docs/plans/midi-native-protocol/phase-2-synth-breakouts-and-poly-composability.md
// and phase-4-tracker-expression.md.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "operator_api/types.h"
#include "operator_api/voice_table.h"

namespace vivid_sequencers {

// Stable per-tone frequency for a voice slot, including the current pitch-bend.
inline float voice_freq_hz(const vivid::VoiceSlot& s) {
    return 440.0f * std::pow(
        2.0f,
        (static_cast<float>(s.note) - 69.0f + s.pitch_bend_semis) / 12.0f);
}

// Indexes into the lanes[] array that emit_voice_breakouts expects.
enum VoiceBreakoutLane : int {
    kVoiceBreakoutIds        = 0,
    kVoiceBreakoutGates      = 1,
    kVoiceBreakoutVelocities = 2,
    kVoiceBreakoutFreqs      = 3,
    // Phase 4 expression lanes. NoteBreakout exposes these; voice synths
    // declare only the first 4 ports and leave [4..6] zero-initialized
    // (the emit helper's null-handle guard skips them).
    kVoiceBreakoutPitchBend  = 4,
    kVoiceBreakoutPressure   = 5,
    kVoiceBreakoutTimbre     = 6,
    kVoiceBreakoutLaneCount  = 7,
};

template <typename SlotT>
inline int collect_sorted_voice_indices(const SlotT* slots, int capacity,
                                        int* sorted_out, int max_sorted = 64) {
    static_assert(std::is_base_of<vivid::VoiceSlot, SlotT>::value,
                  "SlotT must derive from vivid::VoiceSlot");
    if (!sorted_out || max_sorted <= 0) return 0;

    int count = 0;
    for (int i = 0; i < capacity && count < max_sorted; ++i) {
        if (slots[i].active) sorted_out[count++] = i;
    }
    std::sort(sorted_out, sorted_out + count,
              [slots](int a, int b) {
                  return slots[a].note_id < slots[b].note_id;
              });
    return count;
}

// ---------------------------------------------------------------------------
// Value-API overloads (lane-value clean-break, Phase 6). Identical sort + emit,
// writing through VividValueOutput (the successor to VividLaneOutput) over the
// SAME backing buffers — behavior-identical. value_outs[] is indexed by
// VoiceBreakoutLane; callers pass `ctx->value_outputs + first_voice_lane_port`.
// ---------------------------------------------------------------------------
template <typename SlotT>
inline void emit_voice_breakouts_from_sorted(
    const SlotT* slots, const int* sorted, int count,
    VividValueOutput value_outs[kVoiceBreakoutLaneCount]) {
    static_assert(std::is_base_of<vivid::VoiceSlot, SlotT>::value,
                  "SlotT must derive from vivid::VoiceSlot");
    auto emit = [&](int lane_idx, auto value_for_slot) {
        VividValueOutput& out = value_outs[lane_idx];
        if (!out.resize || !out.handle) return;
        float* buf = static_cast<float*>(out.resize(out.handle, static_cast<uint32_t>(count)));
        if (buf) {
            for (int i = 0; i < count; ++i) {
                buf[i] = value_for_slot(static_cast<const vivid::VoiceSlot&>(
                    slots[sorted[i]]));
            }
        }
        if (out.commit) out.commit(out.handle, static_cast<uint32_t>(count));
    };
    emit(kVoiceBreakoutIds,
         [](const vivid::VoiceSlot& s) { return static_cast<float>(s.note_id); });
    emit(kVoiceBreakoutGates,
         [](const vivid::VoiceSlot& s) { return s.gate ? 1.0f : 0.0f; });
    emit(kVoiceBreakoutVelocities,
         [](const vivid::VoiceSlot& s) { return s.velocity; });
    emit(kVoiceBreakoutFreqs,
         [](const vivid::VoiceSlot& s) { return voice_freq_hz(s); });
    emit(kVoiceBreakoutPitchBend,
         [](const vivid::VoiceSlot& s) { return s.pitch_bend_semis; });
    emit(kVoiceBreakoutPressure,
         [](const vivid::VoiceSlot& s) { return s.pressure; });
    emit(kVoiceBreakoutTimbre,
         [](const vivid::VoiceSlot& s) { return s.timbre; });
}

template <typename SlotT>
inline uint32_t emit_voice_breakouts(const SlotT* slots, int capacity,
                                     VividValueOutput value_outs[kVoiceBreakoutLaneCount],
                                     int* sorted_out = nullptr) {
    static_assert(std::is_base_of<vivid::VoiceSlot, SlotT>::value,
                  "SlotT must derive from vivid::VoiceSlot");
    constexpr int kMaxSlots = 64;
    int sorted[kMaxSlots];
    int count = collect_sorted_voice_indices(slots, capacity, sorted, kMaxSlots);
    emit_voice_breakouts_from_sorted(slots, sorted, count, value_outs);
    if (sorted_out) {
        for (int i = 0; i < count; ++i) sorted_out[i] = sorted[i];
    }
    return static_cast<uint32_t>(count);
}

template <int N>
inline uint32_t emit_voice_breakouts(const vivid::VoiceTable<N>& alloc,
                                     VividValueOutput value_outs[kVoiceBreakoutLaneCount]) {
    int sorted[N];
    int count = collect_sorted_voice_indices(alloc.slots, N, sorted, N);
    emit_voice_breakouts_from_sorted(alloc.slots, sorted, count, value_outs);
    return static_cast<uint32_t>(count);
}

}  // namespace vivid_sequencers
