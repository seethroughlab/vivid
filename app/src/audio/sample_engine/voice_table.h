#pragma once
// Polyphonic voice allocator — fixed-capacity slot pool with FIFO oldest-stealing.
// Ported from vivid-classic (src/operator_api/voice_table.h). The classic
// `process_note_buffer` helper (which pulled in the VividNoteBuffer wire type) is
// dropped: this app's ops walk their own VividNoteEvent stream and drive
// note_on/note_off directly, so the allocator needs no note-transport dependency.
//
// Slot identity is the `note_id` carried by every per-note event. Same-pitch
// overlap allocates distinct slots because each note-on carries a fresh note_id,
// so legato retriggers and explicit overlap both resolve to separate voices.
// Per-domain state (envelope, sample region, playback cursor) lives in a parallel
// array owned by the caller, indexed by the same slot index (see voice.h).
#include <cstdint>

namespace vivid {

struct VoiceSlot {
    bool     active                  = false;  // slot owns a voice (gate may be off during release tail)
    bool     gate                    = false;  // true while the note is held
    int      note                    = -1;     // MIDI note number (0..127), -1 = unset
    float    velocity                = 1.0f;   // 0..1 normalized
    uint64_t start_frame             = 0;      // global frame counter at note-on; used for stealing
    uint32_t trigger_offset_samples  = 0;      // offset within the audio buffer when triggered
    uint64_t note_id                 = 0;      // slot identity (matches the emitter's note_id)
    float    pitch_bend_semis        = 0.0f;   // current per-note pitch bend in semitones
    float    pressure                = 0.0f;   // current per-note pressure (Z-axis), 0..1
    float    timbre                  = 0.0f;   // current per-note timbre (Y-axis), 0..1
};

// Fixed-capacity polyphonic voice allocator. kMaxVoices is a compile-time bound;
// runtime polyphony can be capped lower by the caller (a `voices` param clamps the
// loop count).
template <int kMaxVoices>
class VoiceTable {
public:
    static constexpr int kCapacity = kMaxVoices;

    VoiceSlot slots[kMaxVoices];

    int active_count() const {
        int n = 0;
        for (int i = 0; i < kMaxVoices; ++i)
            if (slots[i].active) ++n;
        return n;
    }

    // Slot currently holding `note_id`, or -1. Keying by note_id (not MIDI note)
    // means same-pitch overlapping notes resolve to distinct slots.
    int find_active_id(uint64_t note_id) const {
        if (note_id == 0) return -1;
        for (int i = 0; i < kMaxVoices; ++i)
            if (slots[i].active && slots[i].note_id == note_id) return i;
        return -1;
    }

    // A free slot, falling back to the oldest active slot if all are in use.
    int find_free_or_steal() {
        for (int i = 0; i < kMaxVoices; ++i)
            if (!slots[i].active) return i;
        int oldest = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (slots[i].start_frame < slots[oldest].start_frame) oldest = i;
        return oldest;
    }

    // Allocate a slot for a note-on: reuse a slot already holding this note_id,
    // else a free slot, else steal the oldest. Returns the chosen slot index.
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

    // Release the gate on the slot holding `note_id`. The caller sets active=false
    // when the envelope-release tail finishes. Returns the slot index, or -1.
    int note_off(uint64_t note_id) {
        int idx = find_active_id(note_id);
        if (idx >= 0) slots[idx].gate = false;
        return idx;
    }

    // Force-clear all slots (transport reset / panic).
    void all_notes_off() {
        for (int i = 0; i < kMaxVoices; ++i) {
            slots[i].active = false;
            slots[i].gate   = false;
        }
    }
};

} // namespace vivid
