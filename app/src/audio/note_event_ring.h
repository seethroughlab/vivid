#pragma once
// A track's DISCRETE note on/off EVENTS (not the held-note SET) as a lock-free SPSC ring: the audio
// thread pushes one entry per note-on/off in a block; the frame thread drains everything queued since
// last frame and republishes it on the host note-event bus for one-shot visual ops (a note-on spawns
// a burst; a re-struck held pitch fires again — which the membership-only held set cannot express).
//
// Single producer (audio thread) / single consumer (frame thread). The `buf_` writes are plain but
// gated by the release/acquire pair on the monotonic write cursor, so the whole thing is TSan-clean
// (same discipline as the recording tap in transport.h). Overflow (frame stalled) drops the newest
// events — bounded, and note-driven visuals degrade to "a few missed spawns", never UB.
#include <atomic>
#include <cstdint>

namespace vivid::audio {

struct NoteEventLite {
    uint8_t  kind;         // 1 = note-on, 0 = note-off
    int      pitch;        // MIDI pitch
    float    velocity;     // 0..1
    int32_t  note_id;      // per-voice identity (distinguishes a re-struck same pitch)
    uint32_t sample_offset;// within-block offset (for future sub-frame timing)
};

template <int N>
struct NoteEventRing {
    // Audio thread: enqueue one event. Drops if the ring is full (frame drains every frame, so this
    // only happens under a severe main-thread stall).
    void push(uint8_t kind, int pitch, float velocity, int32_t note_id, uint32_t sample_offset) {
        const uint64_t w = w_.load(std::memory_order_relaxed);
        const uint64_t r = r_.load(std::memory_order_acquire);
        if (w - r >= static_cast<uint64_t>(N)) return;   // full → drop newest
        buf_[w % N] = { kind, pitch, velocity, note_id, sample_offset };
        w_.store(w + 1, std::memory_order_release);       // publishes the slot write above
    }

    // Frame thread: drain up to `max` queued events into `out`, advancing the read cursor. Returns count.
    int drain(NoteEventLite* out, int max) {
        const uint64_t w = w_.load(std::memory_order_acquire);   // orders the slot reads below
        uint64_t r = r_.load(std::memory_order_relaxed);
        int n = 0;
        while (r < w && n < max) { out[n++] = buf_[r % N]; ++r; }
        r_.store(r, std::memory_order_release);
        return n;
    }

    // Audio thread: drop everything (play→stop), matching HeldNoteSet::clear semantics.
    void clear() { r_.store(w_.load(std::memory_order_relaxed), std::memory_order_release); }

private:
    std::atomic<uint64_t> w_{0};   // audio thread advances
    std::atomic<uint64_t> r_{0};   // frame thread advances
    NoteEventLite         buf_[N];
};

}  // namespace vivid::audio
