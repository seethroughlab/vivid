// ADR-0029 (phase 2): the polyphonic held-note set (audio/held_note_set.h) that feeds the note instancer.
// Pins the add/update/swap-remove/snapshot semantics, then races a writer (add/remove churn) against a
// reader (snapshot) so ThreadSanitizer proves the ordering + that the tolerated torn read is a well-defined
// atomic race (whole notes, no half-written note, no UB, no report). Portable — joins the `THREAD` leg.
#include "audio/held_note_set.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using vivid::audio::HeldNoteSet;
using vivid::audio::HeldSnapshot;

int main() {
    // --- single-threaded semantics -----------------------------------------------------------------
    HeldNoteSet<8> h;
    HeldSnapshot out[8];
    assert(h.snapshot(out, 8) == 0);

    h.add(/*note_id=*/1, 60, 0.5f);
    h.add(2, 64, 0.6f);
    h.add(3, 67, 0.7f);
    assert(h.snapshot(out, 8) == 3);
    assert(out[0].pitch == 60 && out[0].velocity == 0.5f);   // pitch + velocity travel together
    assert(out[2].pitch == 67 && out[2].velocity == 0.7f);

    h.add(2, 64, 0.9f);                                       // same note_id → update, not append
    assert(h.snapshot(out, 8) == 3);
    for (int i = 0; i < 3; ++i) if (out[i].pitch == 64) assert(out[i].velocity == 0.9f);

    h.remove(1);                                             // swap-remove: last (67) fills slot 0
    assert(h.snapshot(out, 8) == 2);
    bool has64 = false, has67 = false, has60 = false;
    for (int i = 0; i < 2; ++i) { has64 |= out[i].pitch == 64; has67 |= out[i].pitch == 67; has60 |= out[i].pitch == 60; }
    assert(has64 && has67 && !has60);
    h.remove(999);                                          // unknown id → no-op
    assert(h.snapshot(out, 8) == 2);
    assert(h.snapshot(out, 1) == 1);                        // clamp to caller max

    h.clear();
    assert(h.snapshot(out, 8) == 0);

    for (int i = 0; i < 12; ++i) h.add(1000 + i, 40 + i, 0.5f);   // capacity: K=8 holds ≤ 8
    assert(h.snapshot(out, 8) == 8);

    // --- concurrent stress (the ThreadSanitizer target) --------------------------------------------
    // A writer churns adds/removes over a small note-id space; a reader snapshots. Every held note must be
    // internally sane (pitch/velocity in the ranges the writer used) and the count ≤ K.
    HeldNoteSet<32> set;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (uint64_t k = 0; !stop.load(std::memory_order_relaxed); ++k) {
            const int32_t id = static_cast<int32_t>(k % 24);            // 24 possible held notes
            if ((k & 1) == 0) set.add(id, 20 + static_cast<int>(id), static_cast<float>(id) / 24.0f);
            else              set.remove(id);
        }
    });
    std::thread reader([&] {
        HeldSnapshot buf[32];
        for (long i = 0; i < 300000; ++i) {
            const int m = set.snapshot(buf, 32);
            assert(m >= 0 && m <= 32);
            for (int j = 0; j < m; ++j) {
                assert(buf[j].pitch >= 20 && buf[j].pitch <= 20 + 23);       // a whole pushed note
                assert(buf[j].velocity >= 0.0f && buf[j].velocity <= 1.0f);
            }
        }
    });
    reader.join();
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    std::puts("test_held_note_set: OK");
    return 0;
}
