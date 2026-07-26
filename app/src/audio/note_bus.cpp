// Host side of the active-notes bus (operator_api/note_bus.h). A small fixed set of position slots the
// engine fills each frame from session_track_active_notes, each TAGGED with the stable id of the track
// occupying it; a GPU op reads a track's notes by that stable id (so it follows the track). Lock-free
// (single publisher = the UI/frame thread; readers = render-thread ops): write the notes, then the
// count (release), then the id (release) — so a reader that matches the id sees the notes+count that
// go with it. A torn read is a benign 1-frame glitch, like the movie-audio bus.
#include "operator_api/note_bus.h"
#include <atomic>

namespace {
struct Channel {
    std::atomic<int>      track_id{-1};   // stable id of the track in this slot, or -1 (free)
    VividActiveNote       notes[VIVID_MAX_ACTIVE_NOTES];
    std::atomic<uint32_t> count{0};
};
Channel g_channels[VIVID_NOTE_BUS_TRACKS];
}  // namespace

extern "C" uint32_t vivid_track_active_notes(int track_id, VividActiveNote* out, uint32_t max) {
    if (track_id < 0 || !out || max == 0) return 0;
    for (int s = 0; s < VIVID_NOTE_BUS_TRACKS; ++s) {
        Channel& c = g_channels[s];
        if (c.track_id.load(std::memory_order_acquire) != track_id) continue;
        uint32_t n = c.count.load(std::memory_order_acquire);
        if (n > VIVID_MAX_ACTIVE_NOTES) n = VIVID_MAX_ACTIVE_NOTES;
        if (n > max) n = max;
        for (uint32_t i = 0; i < n; ++i) out[i] = c.notes[i];
        return n;
    }
    return 0;
}

extern "C" void vivid_note_bus_publish(int slot, int track_id, const VividActiveNote* notes, uint32_t count) {
    if (slot < 0 || slot >= VIVID_NOTE_BUS_TRACKS) return;
    Channel& c = g_channels[slot];
    if (count > VIVID_MAX_ACTIVE_NOTES) count = VIVID_MAX_ACTIVE_NOTES;
    for (uint32_t i = 0; i < count && notes; ++i) c.notes[i] = notes[i];
    c.count.store(notes ? count : 0, std::memory_order_release);
    c.track_id.store(track_id, std::memory_order_release);   // published last: gates the notes above
}
