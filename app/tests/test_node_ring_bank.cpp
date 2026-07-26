// ADR-0029 (phase 2): the per-node atomic-slot ring bank (audio/node_ring_bank.h) backing the per-node
// FFT-capture + scope rings. Pins the snapshot semantics + per-node isolation, then races a writer against
// a reader across several nodes so ThreadSanitizer proves the ordering + that the tolerated torn read is a
// well-defined atomic race (no UB, no report). Portable — joins the `THREAD` leg.
#include "audio/node_ring_bank.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using vivid::audio::NodeRingBank;

int main() {
    // --- single-threaded semantics -----------------------------------------------------------------
    NodeRingBank b;
    float out[8];
    assert(b.snapshot(0, out, 8) == 0);              // unallocated → nothing
    b.allocate(/*nnodes=*/4, /*ring_len=*/8);
    assert(b.allocated());
    assert(b.snapshot(0, out, 8) == 8);              // allocated → zero-filled
    for (int i = 0; i < 8; ++i) assert(out[i] == 0.f);

    for (int i = 1; i <= 8; ++i) b.push(2, static_cast<float>(i));   // node 2 gets 1..8
    assert(b.snapshot(2, out, 8) == 8);
    for (int i = 0; i < 8; ++i) assert(out[i] == static_cast<float>(i + 1));   // oldest→newest
    assert(b.snapshot(2, out, 3) == 3 && out[0] == 6.f && out[2] == 8.f);      // last 3
    assert(b.snapshot(2, out, 99) == 8);             // clamp to ring_len

    for (int i = 0; i < 8; ++i) assert(b.snapshot(0, out, 8) == 8 && out[i] == 0.f);   // node 0 untouched (isolation)
    assert(b.snapshot(9, out, 8) == 0);              // node out of range

    float run[4] = { 10.f, 11.f, 12.f, 13.f };       // a block push laps the oldest
    b.push_block(2, run, 4);
    assert(b.snapshot(2, out, 8) == 8);
    assert(out[7] == 13.f && out[0] == 5.f);         // newest 13, oldest now 5

    // --- concurrent stress (the ThreadSanitizer target) --------------------------------------------
    // A writer pushes a per-node ramp into all 4 nodes while a reader snapshots them. Every observed
    // sample must be one the writer pushed for that node (its ramp stays in a known range).
    NodeRingBank bank;
    bank.allocate(4, 1024);
    for (int node = 0; node < 4; ++node)   // pre-fill so the reader never sees the initial zeros
        for (int k = 0; k < 1024; ++k) bank.push(node, static_cast<float>(node * 1000 + (k % 1000)));
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (uint64_t k = 0; !stop.load(std::memory_order_relaxed); ++k)
            for (int node = 0; node < 4; ++node)
                bank.push(node, static_cast<float>(node * 1000 + (k % 1000)));   // node in [n*1000, n*1000+999]
    });
    std::thread reader([&] {
        float buf[1024];
        for (long i = 0; i < 250000; ++i) {
            const int node = static_cast<int>(i & 3);
            const int m = bank.snapshot(node, buf, 1024);
            assert(m == 1024);
            const float lo = static_cast<float>(node * 1000), hi = lo + 999.f;
            for (int j = 0; j < m; ++j) assert(buf[j] >= lo && buf[j] <= hi);   // a whole pushed sample
        }
    });
    reader.join();
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    std::puts("test_node_ring_bank: OK");
    return 0;
}
