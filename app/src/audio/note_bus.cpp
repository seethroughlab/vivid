// Host side of the active-notes bus (operator_api/note_bus.h). A small fixed set of position slots the
// engine fills each frame from session_track_active_notes, each TAGGED with the stable id of the track
// occupying it; a GPU op reads a track's notes by that stable id (so it follows the track). Lock-free
// (single publisher = the UI/frame thread; readers = render-thread ops): write the notes, then the
// count (release), then the id (release) — so a reader that matches the id sees the notes+count that
// go with it.
//
// The publisher may overwrite a slot while a reader is mid-read (both run every frame), so the note
// slots are stored as ATOMICS — each {pitch, velocity} packed into one std::atomic<uint64_t>, written
// and read relaxed. That makes the "benign torn read" (documented for the movie-audio bus too) a
// WELL-DEFINED data race on atomics rather than UB on plain memory: a torn snapshot mixes whole notes
// from adjacent frames (each note stays self-consistent — its pitch and velocity travel together in one
// 64-bit store), a 1-frame visual glitch at worst. This is also why the bus is clean under
// ThreadSanitizer (ADR-0029): every cross-thread access is an atomic with explicit ordering.
#include "operator_api/note_bus.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
// Pack/unpack a note into one 64-bit word: pitch in the high 32 bits, the velocity float's bits in the
// low 32 (memcpy — C++17 has no std::bit_cast). Keeps a note atomic as a unit.
inline uint64_t pack_note(int pitch, float vel) {
    uint32_t vbits; std::memcpy(&vbits, &vel, sizeof(vbits));
    return (static_cast<uint64_t>(static_cast<uint32_t>(pitch)) << 32) | static_cast<uint64_t>(vbits);
}
inline VividActiveNote unpack_note(uint64_t w) {
    VividActiveNote n;
    n.pitch = static_cast<int>(static_cast<uint32_t>(w >> 32));
    const uint32_t vbits = static_cast<uint32_t>(w & 0xffffffffu);
    std::memcpy(&n.velocity, &vbits, sizeof(n.velocity));
    return n;
}

struct Channel {
    std::atomic<int>      track_id{-1};   // stable id of the track in this slot, or -1 (free)
    std::atomic<uint64_t> notes[VIVID_MAX_ACTIVE_NOTES];   // packed {pitch, velocity}, one atomic per slot
    std::atomic<uint32_t> count{0};
};
Channel g_channels[VIVID_NOTE_BUS_TRACKS];
}  // namespace

extern "C" uint32_t vivid_track_active_notes(int track_id, VividActiveNote* out, uint32_t max) {
    if (track_id < 0 || !out || max == 0) return 0;
    for (int s = 0; s < VIVID_NOTE_BUS_TRACKS; ++s) {
        Channel& c = g_channels[s];
        if (c.track_id.load(std::memory_order_acquire) != track_id) continue;
        uint32_t n = c.count.load(std::memory_order_acquire);   // acquire: orders the note stores below
        if (n > VIVID_MAX_ACTIVE_NOTES) n = VIVID_MAX_ACTIVE_NOTES;
        if (n > max) n = max;
        for (uint32_t i = 0; i < n; ++i) out[i] = unpack_note(c.notes[i].load(std::memory_order_relaxed));
        return n;
    }
    return 0;
}

extern "C" void vivid_note_bus_publish(int slot, int track_id, const VividActiveNote* notes, uint32_t count) {
    if (slot < 0 || slot >= VIVID_NOTE_BUS_TRACKS) return;
    Channel& c = g_channels[slot];
    if (count > VIVID_MAX_ACTIVE_NOTES) count = VIVID_MAX_ACTIVE_NOTES;
    for (uint32_t i = 0; i < count && notes; ++i)
        c.notes[i].store(pack_note(notes[i].pitch, notes[i].velocity), std::memory_order_relaxed);
    c.count.store(notes ? count : 0, std::memory_order_release);   // release: publishes the note stores
    c.track_id.store(track_id, std::memory_order_release);         // published last: gates the notes above
}
