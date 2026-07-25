// Host side of the active-notes bus (operator_api/note_bus.h). A small fixed per-track store the
// engine fills each frame from session_track_active_notes; a GPU op reads its track's notes by index.
// Lock-free (single publisher = the UI/frame thread; readers = render-thread ops): write the array,
// then store the count with release; a torn read is a benign 1-frame glitch, like the movie-audio bus.
#include "operator_api/note_bus.h"
#include <atomic>
#include <algorithm>

namespace {
struct Channel {
    VividActiveNote notes[VIVID_MAX_ACTIVE_NOTES];
    std::atomic<uint32_t> count{0};
};
Channel g_channels[VIVID_NOTE_BUS_TRACKS];
}  // namespace

extern "C" uint32_t vivid_track_active_notes(int track, VividActiveNote* out, uint32_t max) {
    if (track < 0 || track >= VIVID_NOTE_BUS_TRACKS || !out || max == 0) return 0;
    Channel& c = g_channels[track];
    uint32_t n = c.count.load(std::memory_order_acquire);
    if (n > VIVID_MAX_ACTIVE_NOTES) n = VIVID_MAX_ACTIVE_NOTES;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) out[i] = c.notes[i];
    return n;
}

extern "C" void vivid_note_bus_publish(int track, const VividActiveNote* notes, uint32_t count) {
    if (track < 0 || track >= VIVID_NOTE_BUS_TRACKS) return;
    Channel& c = g_channels[track];
    if (count > VIVID_MAX_ACTIVE_NOTES) count = VIVID_MAX_ACTIVE_NOTES;
    for (uint32_t i = 0; i < count; ++i) c.notes[i] = notes[i];
    c.count.store(count, std::memory_order_release);
}
