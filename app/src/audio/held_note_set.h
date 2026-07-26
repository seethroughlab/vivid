#pragma once
// ADR-0029: a track's polyphonic HELD-note set. The audio thread maintains it from note on/off events
// (find-or-append on note-on, swap-remove on note-off); the frame thread snapshots it for the note
// instancer. Each slot packs {pitch, velocity} into one std::atomic<uint64_t> (like the active-notes bus),
// so the swap-remove that rewrites a live slot in place is a WELL-DEFINED atomic race for the reader — not
// UB on plain memory — and a torn snapshot mixes WHOLE notes (pitch + velocity travel together), never a
// half-written note. TSan-clean. `note_id` is audio-thread-only accounting (never read by the frame
// thread), so it stays a plain array. Single producer (audio thread); readers snapshot. `count` is
// published release on every change; the reader loads it acquire.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace vivid::audio {

// The snapshot output element (matches the public session ActiveNote layout {pitch, velocity}).
struct HeldSnapshot { int pitch; float velocity; };

template <int K>
struct HeldNoteSet {
    // Audio thread — note-on: update the slot with this note_id, else append (≤ K). Publishes count.
    void add(int32_t note_id, int pitch, float velocity) {
        for (uint32_t j = 0; j < n_; ++j)
            if (id_[j] == note_id) { note_[j].store(pack(pitch, velocity), std::memory_order_relaxed); return; }
        if (n_ < static_cast<uint32_t>(K)) {
            note_[n_].store(pack(pitch, velocity), std::memory_order_relaxed);
            id_[n_] = note_id;
            ++n_;
            count_.store(n_, std::memory_order_release);
        }
    }
    // Audio thread — note-off: swap-remove the slot with this note_id (last element fills the hole).
    void remove(int32_t note_id) {
        for (uint32_t j = 0; j < n_; ++j)
            if (id_[j] == note_id) {
                const uint32_t last = n_ - 1;
                if (j != last) {   // move `last` into `j` — an atomic store, so a mid-copy reader sees a whole note
                    note_[j].store(note_[last].load(std::memory_order_relaxed), std::memory_order_relaxed);
                    id_[j] = id_[last];
                }
                n_ = last;
                count_.store(n_, std::memory_order_release);
                return;
            }
    }
    // Audio thread — clear all (play→stop).
    void clear() { n_ = 0; count_.store(0, std::memory_order_release); }

    // Frame thread — copy up to `max` held notes into `out`; returns the count.
    int snapshot(HeldSnapshot* out, int max) const {
        const int n = std::min<int>(max, std::min<uint32_t>(count_.load(std::memory_order_acquire),
                                                            static_cast<uint32_t>(K)));
        for (int i = 0; i < n; ++i) out[i] = unpack(note_[i].load(std::memory_order_relaxed));
        return n;
    }
    uint32_t size() const { return n_; }   // audio-thread working count (not published)

private:
    static uint64_t pack(int pitch, float vel) {
        uint32_t vbits; std::memcpy(&vbits, &vel, sizeof(vbits));
        return (static_cast<uint64_t>(static_cast<uint32_t>(pitch)) << 32) | static_cast<uint64_t>(vbits);
    }
    static HeldSnapshot unpack(uint64_t w) {
        HeldSnapshot h;
        h.pitch = static_cast<int>(static_cast<uint32_t>(w >> 32));
        const uint32_t vbits = static_cast<uint32_t>(w & 0xffffffffu);
        std::memcpy(&h.velocity, &vbits, sizeof(h.velocity));
        return h;
    }

    std::atomic<uint64_t> note_[K];       // packed {pitch, velocity}
    int32_t               id_[K] = {};    // note_id — audio-thread-only, never read by the frame thread
    uint32_t              n_ = 0;         // audio-thread working count
    std::atomic<uint32_t> count_{0};      // published (release on every change)
};

}  // namespace vivid::audio
