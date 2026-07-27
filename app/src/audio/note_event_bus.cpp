// Host side of the note-event bus (operator_api/note_events.h). The frame thread drains each track's
// audio-thread event ring and publishes a per-frame snapshot here; GPU ops read a track's events by
// stable id. Single publisher (the UI/frame thread); readers are render-thread ops.
//
// Unlike the active-notes bus, an event ({kind,pitch,velocity,note_id} = 16 bytes) is too wide to pack
// into one atomic, so a per-slot atomic can't make a concurrent overwrite well-defined. Instead each
// channel DOUBLE-BUFFERS: the publisher fills the back buffer then flips an atomic index (release); a
// reader reads the front index (acquire) and its count. A reader therefore always sees a fully-written,
// self-consistent event array — no torn events — and the flip is the only cross-thread synchronization,
// so it is TSan-clean (ADR-0029 discipline).
#include "operator_api/note_events.h"
#include <atomic>
#include <cstdint>

namespace {
struct EvChannel {
    std::atomic<int>      track_id{-1};   // stable id in this slot, or -1 (free)
    std::atomic<int>      active{0};      // buffer index readers should read (0/1)
    VividNoteHit        buf[2][VIVID_MAX_NOTE_EVENTS];
    std::atomic<uint32_t> count[2];       // per-buffer event count
    EvChannel() { count[0].store(0); count[1].store(0); }
};
EvChannel g_channels[VIVID_NOTE_EVENT_TRACKS];
}  // namespace

extern "C" uint32_t vivid_track_note_events(int track_id, VividNoteHit* out, uint32_t max) {
    if (track_id < 0 || !out || max == 0) return 0;
    for (int s = 0; s < VIVID_NOTE_EVENT_TRACKS; ++s) {
        EvChannel& c = g_channels[s];
        if (c.track_id.load(std::memory_order_acquire) != track_id) continue;
        const int a = c.active.load(std::memory_order_acquire);   // acquire: orders buf/count below
        uint32_t n = c.count[a].load(std::memory_order_relaxed);
        if (n > VIVID_MAX_NOTE_EVENTS) n = VIVID_MAX_NOTE_EVENTS;
        if (n > max) n = max;
        for (uint32_t i = 0; i < n; ++i) out[i] = c.buf[a][i];
        return n;
    }
    return 0;
}

extern "C" void vivid_note_event_bus_publish(int slot, int track_id, const VividNoteHit* ev, uint32_t count) {
    if (slot < 0 || slot >= VIVID_NOTE_EVENT_TRACKS) return;
    EvChannel& c = g_channels[slot];
    if (count > VIVID_MAX_NOTE_EVENTS) count = VIVID_MAX_NOTE_EVENTS;
    const int back = 1 - c.active.load(std::memory_order_relaxed);   // single publisher: relaxed is fine
    for (uint32_t i = 0; i < count && ev; ++i) c.buf[back][i] = ev[i];
    c.count[back].store(ev ? count : 0, std::memory_order_relaxed);
    c.active.store(back, std::memory_order_release);                 // release: publishes buf+count above
    c.track_id.store(track_id, std::memory_order_release);           // published last: gates the slot
}
