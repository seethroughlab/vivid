// ADR-0029 (phase 2): the atomic-slot spectrum ring (audio/analysis_ring.h) that backs a track's/master's
// frame-side FFT. Pins the snapshot semantics, then races a writer against a reader so ThreadSanitizer
// proves the release/acquire ordering + that the tolerated torn read is a well-defined atomic race (no UB,
// no report). Portable — joins the `THREAD`-labelled TSan CI leg.
#include "audio/analysis_ring.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using vivid::audio::AnalysisRing;

int main() {
    // --- single-threaded semantics -----------------------------------------------------------------
    AnalysisRing<8> r;
    float out[8];
    assert(r.snapshot(out, 8) == 8);                 // starts zero-filled
    for (int i = 0; i < 8; ++i) assert(out[i] == 0.f);

    for (int i = 1; i <= 8; ++i) r.push(static_cast<float>(i));   // ring now holds 1..8, newest = 8
    assert(r.snapshot(out, 8) == 8);
    for (int i = 0; i < 8; ++i) assert(out[i] == static_cast<float>(i + 1));   // oldest→newest
    assert(r.snapshot(out, 3) == 3);                 // just the last 3
    assert(out[0] == 6.f && out[1] == 7.f && out[2] == 8.f);
    assert(r.snapshot(out, 100) == 8);               // clamps to N

    r.push(9.f);                                     // laps: 9 overwrites the oldest (was 1)
    assert(r.snapshot(out, 8) == 8);
    assert(out[7] == 9.f && out[0] == 2.f);          // newest 9, oldest now 2

    // --- concurrent stress (the ThreadSanitizer target) --------------------------------------------
    // A writer pushes a monotonic ramp (mod 1000 to stay in a known range) while a reader snapshots.
    // Every sample the reader sees must be one the writer actually pushed (0..999) — a torn snapshot
    // mixes whole samples from adjacent laps, never a half-float.
    AnalysisRing<1024> ring;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (uint64_t k = 0; !stop.load(std::memory_order_relaxed); ++k)
            ring.push(static_cast<float>(k % 1000));
    });
    std::thread reader([&] {
        float buf[1024];
        for (long i = 0; i < 300000; ++i) {
            const int m = ring.snapshot(buf, 1024);
            assert(m == 1024);
            for (int j = 0; j < m; ++j) assert(buf[j] >= 0.f && buf[j] <= 999.f);   // a whole pushed sample
        }
    });
    reader.join();
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    std::puts("test_analysis_ring: OK");
    return 0;
}
