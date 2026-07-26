#pragma once
// A lock-free single-producer / single-consumer sample ring for the audio→visual bridge: the audio thread
// PUSHES one float per sample; the frame thread SNAPSHOTS the last N (oldest→newest) for the FFT
// (mini_fft.h). Each slot is a std::atomic<float>, so the deliberately-tolerated torn read — a 1-frame
// spectral blip when the writer laps the reader mid-snapshot — is a WELL-DEFINED data race on atomics,
// not UB on plain memory, and it is ThreadSanitizer-clean (ADR-0029, the same hardening the note bus got).
//
// Ordering: push writes the slot (relaxed) then advances `pos` (release); snapshot loads `pos` (acquire)
// then reads the slots (relaxed). The single acquire on the latest `pos` synchronizes-with the writer's
// release, so every slot written up to that head is visible — a reader behind the head sees consistent
// samples; only a lapped slot tears (benignly). Header-only, no allocation. N must be a power of two.
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace vivid::audio {

template <int N>
struct AnalysisRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "AnalysisRing size must be a power of two");

    std::atomic<float>    slot[N];
    std::atomic<uint32_t> pos{0};   // next write index (the published head)

    AnalysisRing() {
        for (int i = 0; i < N; ++i) slot[i].store(0.f, std::memory_order_relaxed);
    }

    // Audio thread: append one sample.
    void push(float v) {
        const uint32_t p = pos.load(std::memory_order_relaxed);
        slot[p & (N - 1)].store(v, std::memory_order_relaxed);
        pos.store((p + 1) & (N - 1), std::memory_order_release);   // publish: gates the slot store above
    }

    // Frame thread: copy the last min(n, N) samples (oldest→newest) into `out`; returns the count.
    int snapshot(float* out, int n) const {
        const int cnt = std::min(n, N);
        const uint32_t head = pos.load(std::memory_order_acquire);   // acquire: orders the slot reads below
        const uint32_t start = (head + static_cast<uint32_t>(N) - static_cast<uint32_t>(cnt)) & (N - 1);
        for (int i = 0; i < cnt; ++i)
            out[i] = slot[(start + static_cast<uint32_t>(i)) & (N - 1)].load(std::memory_order_relaxed);
        return cnt;
    }
};

}  // namespace vivid::audio
