// Active-notes bus (operator_api/note_bus.h + audio/note_bus.cpp): correctness + a concurrent stress
// that gives ThreadSanitizer a real cross-thread channel to verify (ADR-0029). The bus is the freshest
// lock-free channel (a UI-thread publisher, render-thread reader ops); this pins its stable-id search +
// bounds, and races a publisher against a reader so TSan proves the release/acquire ordering + that the
// deliberately-tolerated torn read is a well-defined data race on atomics (no UB, no report).
#include "operator_api/note_bus.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    VividActiveNote out[VIVID_MAX_ACTIVE_NOTES];

    // --- 1. single-threaded correctness ---------------------------------------------------------
    const VividActiveNote in[4] = { {60, 0.5f}, {64, 0.6f}, {67, 0.7f}, {72, 0.8f} };
    vivid_note_bus_publish(0, 7, in, 4);                          // slot 0 <- track stable-id 7
    uint32_t n = vivid_track_active_notes(7, out, VIVID_MAX_ACTIVE_NOTES);
    assert(n == 4);
    assert(out[0].pitch == 60 && out[0].velocity == 0.5f);       // pitch + velocity travel together
    assert(out[3].pitch == 72 && out[3].velocity == 0.8f);
    assert(vivid_track_active_notes(99, out, VIVID_MAX_ACTIVE_NOTES) == 0);   // no such track id
    assert(vivid_track_active_notes(7, out, 2) == 2);            // clamp to the caller's max
    vivid_note_bus_publish(0, -1, nullptr, 0);                   // free the slot
    assert(vivid_track_active_notes(7, out, VIVID_MAX_ACTIVE_NOTES) == 0);

    // a track can be found regardless of which position slot holds it
    vivid_note_bus_publish(5, 21, in, 3);
    assert(vivid_track_active_notes(21, out, VIVID_MAX_ACTIVE_NOTES) == 3 && out[1].pitch == 64);
    vivid_note_bus_publish(5, -1, nullptr, 0);

    // --- 2. concurrent stress (the ThreadSanitizer target) --------------------------------------
    // A publisher hammers one slot with varying note sets while a reader pulls that track by id. Every
    // observed snapshot must be internally sane (count bounded, each note in range) even when torn.
    constexpr int kTrack = 42;
    std::atomic<bool> stop{false};
    std::thread pub([&] {
        VividActiveNote buf[VIVID_MAX_ACTIVE_NOTES];
        for (uint64_t k = 0; !stop.load(std::memory_order_relaxed); ++k) {
            const uint32_t c = 1 + static_cast<uint32_t>(k % VIVID_MAX_ACTIVE_NOTES);
            for (uint32_t i = 0; i < c; ++i) {
                buf[i].pitch    = static_cast<int>((k + i) % 128);
                buf[i].velocity = static_cast<float>((k + i) % 101) / 100.0f;   // 0.00 .. 1.00
            }
            vivid_note_bus_publish(3, kTrack, buf, c);
        }
    });
    std::thread rdr([&] {
        VividActiveNote o[VIVID_MAX_ACTIVE_NOTES];
        for (long i = 0; i < 400000; ++i) {
            const uint32_t m = vivid_track_active_notes(kTrack, o, VIVID_MAX_ACTIVE_NOTES);
            assert(m <= VIVID_MAX_ACTIVE_NOTES);
            for (uint32_t j = 0; j < m; ++j) {
                assert(o[j].pitch >= 0 && o[j].pitch < 128);          // a whole note is never torn
                assert(o[j].velocity >= 0.0f && o[j].velocity <= 1.0f);
            }
        }
    });
    rdr.join();
    stop.store(true, std::memory_order_relaxed);
    pub.join();

    std::puts("test_note_bus: OK");
    return 0;
}
