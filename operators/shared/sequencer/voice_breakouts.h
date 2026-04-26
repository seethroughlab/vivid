#pragma once

// Voice-breakout helper — emits the four standardized per-voice control
// lanes (voice_ids, voice_gates, voice_velocities, voice_freqs) from a
// vivid::VoiceAllocator<N>'s active slots, sorted by note_id ascending.
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
//   [3] voice_freqs
//
// See docs/plans/midi-native-protocol/phase-2-synth-breakouts-and-poly-composability.md.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "operator_api/types.h"
#include "operator_api/voice_allocator.h"

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
    kVoiceBreakoutLaneCount  = 4,
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

template <typename SlotT>
inline void emit_voice_breakouts_from_sorted(
    const SlotT* slots, const int* sorted, int count,
    VividLaneOutput lanes[kVoiceBreakoutLaneCount]) {
    static_assert(std::is_base_of<vivid::VoiceSlot, SlotT>::value,
                  "SlotT must derive from vivid::VoiceSlot");

    auto emit = [&](int lane_idx, auto value_for_slot) {
        VividLaneOutput& out = lanes[lane_idx];
        if (!out.resize || !out.handle) return;
        float* buf = out.resize(out.handle, static_cast<uint32_t>(count));
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
}

// Emit the four voice_* lanes from a raw slot array. Templated on the slot
// type so synths whose per-voice struct INHERITS vivid::VoiceSlot but adds
// extra members (e.g. vivid_sampler::Voice with envelope + region + cursor)
// can pass their array directly — pointer arithmetic uses the right stride.
// Used by Sampler / SP404 / Slicer.
//
// Sorts active slots by note_id ascending. `lanes[]` must point to at least
// 4 valid VividLaneOutput entries indexed by VoiceBreakoutLane. Pass nullptr
// for any lane your caller doesn't need; gracefully skips. `sorted_out`, if
// non-null, receives the active-voice slot indices in note_id-sorted order
// (suitable for indexing parallel per-voice arrays the synth needs to align
// with the voices_out audio channels). Returns active-voice count.
template <typename SlotT>
inline uint32_t emit_voice_breakouts(const SlotT* slots, int capacity,
                                     VividLaneOutput lanes[kVoiceBreakoutLaneCount],
                                     int* sorted_out = nullptr) {
    static_assert(std::is_base_of<vivid::VoiceSlot, SlotT>::value,
                  "SlotT must derive from vivid::VoiceSlot");
    constexpr int kMaxSlots = 64;  // bounded by current synth voice caps
    int sorted[kMaxSlots];
    int count = collect_sorted_voice_indices(slots, capacity, sorted, kMaxSlots);
    emit_voice_breakouts_from_sorted(slots, sorted, count, lanes);

    if (sorted_out) {
        for (int i = 0; i < count; ++i) sorted_out[i] = sorted[i];
    }
    return static_cast<uint32_t>(count);
}

// Emit the four voice_* lanes from `alloc`'s active slots, sorted by
// note_id ascending. `lanes[]` must point to at least 4 valid VividLaneOutput
// entries indexed by VoiceBreakoutLane. Returns the number of active voices
// emitted (the length of every lane after this call).
//
// Pass nullptr for any lane your caller doesn't need; this function
// gracefully skips the resize/commit on null entries.
template <int N>
inline uint32_t emit_voice_breakouts(const vivid::VoiceAllocator<N>& alloc,
                                     VividLaneOutput lanes[kVoiceBreakoutLaneCount]) {
    int sorted[N];
    int count = collect_sorted_voice_indices(alloc.slots, N, sorted, N);
    emit_voice_breakouts_from_sorted(alloc.slots, sorted, count, lanes);
    return static_cast<uint32_t>(count);
}

}  // namespace vivid_sequencers
