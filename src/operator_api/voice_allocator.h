#pragma once

// Polyphonic voice allocator — fixed-capacity slot pool with FIFO oldest-stealing.
//
// Used by voice synths (Sampler, SP404, AnalogOsc, WavetableOsc, FmSynth, ...) to
// turn a stream of native note events (VividNoteBuffer) into stable voice-slot
// indices. The slot index is suitable for use as a per-voice state key.
//
// Slot identity is the `note_id` (uint64) carried by every per-note event.
// Same-pitch overlap allocates distinct slots because each note-on carries a
// fresh note_id from the emitter, so legato retriggers and explicit overlap
// both work cleanly.
//
// The allocator owns the bookkeeping — note number, velocity, gate state, a
// frame timestamp used for oldest-voice stealing, the note_id for slot
// lookup, and the current per-note expression state (pitch_bend_semis,
// pressure, timbre). Per-domain extras (envelopes, sample regions, oscillator
// phases, etc.) live in a parallel array owned by the caller, indexed by slot.
//
// See docs/plans/midi-native-protocol/phase-1-wire-format.md for the design.

#include <cstdint>

#include "operator_api/note_types.h"

namespace vivid {

// One allocator slot. Voice synths should keep a parallel array of per-voice
// state, indexed by the same slot index.
struct VoiceSlot {
    bool     active                  = false;  // slot owns a voice (gate may be off during release tail)
    bool     gate                    = false;  // true while the note is held
    int      note                    = -1;     // MIDI note number (0..127), -1 = unset; used by synths for freq lookup
    float    velocity                = 1.0f;   // 0..1 normalized
    uint64_t start_frame             = 0;      // global frame counter at note-on; used for stealing
    uint32_t trigger_offset_samples  = 0;      // offset within the audio buffer when triggered
    uint64_t note_id                 = 0;      // slot identity (matches the emitter's note_id)
    float    pitch_bend_semis        = 0.0f;   // current per-note pitch bend in semitones
    float    pressure                = 0.0f;   // current per-note pressure (Z-axis), 0..1
    float    timbre                  = 0.0f;   // current per-note timbre (Y-axis), 0..1
};

// Fixed-capacity polyphonic voice allocator.
//
// kMaxVoices is a compile-time bound; runtime polyphony can be capped lower by
// the caller (typical pattern: a `voices` param clamps the loop count).
template <int kMaxVoices>
class VoiceAllocator {
public:
    static constexpr int kCapacity = kMaxVoices;

    VoiceSlot slots[kMaxVoices];

    int active_count() const {
        int n = 0;
        for (int i = 0; i < kMaxVoices; ++i)
            if (slots[i].active) ++n;
        return n;
    }

    // Find the slot currently holding the given note_id, or -1 if none.
    // This is the canonical slot lookup — keying by note_id (not by MIDI
    // note number) means same-pitch overlapping notes correctly resolve to
    // distinct slots.
    int find_active_id(uint64_t note_id) const {
        if (note_id == 0) return -1;
        for (int i = 0; i < kMaxVoices; ++i)
            if (slots[i].active && slots[i].note_id == note_id) return i;
        return -1;
    }

    // Find a free slot, falling back to the oldest active slot if all are in use.
    int find_free_or_steal() {
        for (int i = 0; i < kMaxVoices; ++i)
            if (!slots[i].active) return i;
        int oldest = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (slots[i].start_frame < slots[oldest].start_frame) oldest = i;
        return oldest;
    }

    // Allocate a slot for a note-on. If a slot already holds this note_id
    // (which would only happen on emitter bugs — note_ids are supposed to be
    // unique per note-on), reuses it. Otherwise allocates a free slot or
    // steals the oldest active one. Returns the chosen slot index.
    //
    // Per-note expression fields (pitch_bend_semis, pressure, timbre) are
    // reset to defaults; subsequent expression events on this note_id will
    // mutate them.
    int note_on(int note, float velocity, uint64_t note_id,
                uint64_t frame, uint32_t offset = 0) {
        int idx = find_active_id(note_id);
        if (idx < 0) idx = find_free_or_steal();
        VoiceSlot& s = slots[idx];
        s.active                 = true;
        s.gate                   = true;
        s.note                   = note;
        s.velocity               = velocity;
        s.start_frame            = frame;
        s.trigger_offset_samples = offset;
        s.note_id                = note_id;
        s.pitch_bend_semis       = 0.0f;
        s.pressure               = 0.0f;
        s.timbre                 = 0.0f;
        return idx;
    }

    // Release the gate on the slot holding the given note_id. Caller decides
    // when to set `active = false` (typically when an envelope-release tail
    // finishes). Returns the slot index released, or -1 if no slot matched.
    int note_off(uint64_t note_id) {
        int idx = find_active_id(note_id);
        if (idx >= 0) slots[idx].gate = false;
        return idx;
    }

    // Apply per-note expression to the slot holding the given note_id. No-op
    // on miss. Returns the slot index updated, or -1 if no slot matched.
    int apply_pitch_bend(uint64_t note_id, float semis) {
        int idx = find_active_id(note_id);
        if (idx >= 0) slots[idx].pitch_bend_semis = semis;
        return idx;
    }

    int apply_pressure(uint64_t note_id, float value_0_1) {
        int idx = find_active_id(note_id);
        if (idx >= 0) slots[idx].pressure = value_0_1;
        return idx;
    }

    int apply_timbre(uint64_t note_id, float value_0_1) {
        int idx = find_active_id(note_id);
        if (idx >= 0) slots[idx].timbre = value_0_1;
        return idx;
    }

    // Force-clear all slots. Useful on transport reset / panic.
    void all_notes_off() {
        for (int i = 0; i < kMaxVoices; ++i) {
            slots[i].active = false;
            slots[i].gate   = false;
        }
    }

    // Drive the allocator from a VividNoteBuffer. The three callbacks let the
    // caller layer per-domain state on top (envelope gate, sample region
    // lookup, oscillator phase reset, expression-driven DSP routing, etc.).
    // All callbacks may be no-op lambdas.
    //
    //   on_on(int slot, int note, float velocity, uint32_t offset, uint64_t note_id)
    //   on_off(int slot, int note, uint64_t note_id)
    //   on_expression(int slot, VividNoteEventType kind, float value)
    //
    // `base_frame` is the global frame counter at the start of the audio
    // buffer; it's added to `event.frame_offset_samples` to produce a stable
    // monotonic `start_frame` for stealing.
    template <typename OnOn, typename OnOff, typename OnExpr>
    void process_note_buffer(const VividNoteBuffer* notes, uint64_t base_frame,
                             OnOn&& on_on, OnOff&& on_off, OnExpr&& on_expression) {
        if (!notes) return;
        for (uint32_t e = 0; e < notes->count; ++e) {
            const auto& ev = notes->events[e];
            if (ev.note_id == 0) continue;  // global stream — synths ignore
            switch (ev.type) {
                case VIVID_NOTE_ON: {
                    const int idx = note_on(ev.note_number, ev.value,
                                            ev.note_id,
                                            base_frame + ev.frame_offset_samples,
                                            ev.frame_offset_samples);
                    on_on(idx, ev.note_number, ev.value,
                          ev.frame_offset_samples, ev.note_id);
                    break;
                }
                case VIVID_NOTE_OFF: {
                    const int idx = note_off(ev.note_id);
                    if (idx >= 0) on_off(idx, slots[idx].note, ev.note_id);
                    break;
                }
                case VIVID_NOTE_PITCH_BEND: {
                    const int idx = apply_pitch_bend(ev.note_id, ev.value);
                    if (idx >= 0)
                        on_expression(idx, VIVID_NOTE_PITCH_BEND, ev.value);
                    break;
                }
                case VIVID_NOTE_PRESSURE: {
                    const int idx = apply_pressure(ev.note_id, ev.value);
                    if (idx >= 0)
                        on_expression(idx, VIVID_NOTE_PRESSURE, ev.value);
                    break;
                }
                case VIVID_NOTE_TIMBRE: {
                    const int idx = apply_timbre(ev.note_id, ev.value);
                    if (idx >= 0)
                        on_expression(idx, VIVID_NOTE_TIMBRE, ev.value);
                    break;
                }
                default: break;
            }
        }
    }
};

} // namespace vivid
