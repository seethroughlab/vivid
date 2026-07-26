#pragma once
// ADR-0029: a bank of per-node lock-free sample rings for the audio→visual bridge. The audio thread PUSHES
// samples into a node's ring; the frame thread SNAPSHOTS a node's last N (oldest→newest) for its FFT /
// scope. Slots are std::atomic<float> and each node's write head is std::atomic<uint32_t>, so the tolerated
// torn read (a lapped slot mid-snapshot) is a WELL-DEFINED atomic race — not UB on plain memory — and
// TSan-clean. Same hardening as AnalysisRing / the note bus, but for the per-node capture rings
// (Track::node_an, Track::node_scope), which are arrays-of-rings: one flat heap buffer of nnodes*N slots.
// Backed by unique_ptr<atomic[]> (not vector<atomic>, which isn't movable), matching the ctl_pub idiom.
//
// Ordering (per node): a push writes the slot(s) relaxed, then advances the head (release); snapshot loads
// the head (acquire) then reads the slots (relaxed) — the acquire on the latest head synchronizes-with the
// writer's release, so every slot written up to it is visible. Allocated once (never resized), so the write
// pointer is stable under the audio thread; the caller publishes allocation via its own release barrier
// (Track::node_an via node_analyze_mask; node_scope is allocated eagerly at track init, before any render).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace vivid::audio {

struct NodeRingBank {
    // UI thread, before any push. `ring_len` must be a power of two. Idempotent — a second call is a no-op,
    // so the buffer is allocated exactly once and never reallocated.
    void allocate(int nnodes, int ring_len) {
        if (slots_) return;
        nnodes_ = nnodes; n_ = ring_len; mask_ = static_cast<uint32_t>(ring_len - 1);
        slots_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(nnodes) * ring_len);
        heads_ = std::make_unique<std::atomic<uint32_t>[]>(nnodes);
        const size_t total = static_cast<size_t>(nnodes) * ring_len;
        for (size_t i = 0; i < total; ++i) slots_[i].store(0.f, std::memory_order_relaxed);
        for (int i = 0; i < nnodes; ++i) heads_[i].store(0, std::memory_order_relaxed);
    }
    bool allocated() const { return slots_ != nullptr; }

    // Audio thread: append a run of `n` samples to node `node`'s ring with a single head publish (relaxed
    // slot stores gated by one release), matching the original per-block head advance.
    void push_block(int node, const float* v, int n) {
        std::atomic<float>* r = slots_.get() + static_cast<size_t>(node) * n_;
        uint32_t h = heads_[node].load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { r[h & mask_].store(v[i], std::memory_order_relaxed); ++h; }
        heads_[node].store(h, std::memory_order_release);   // publish: gates the slot stores above
    }
    // Convenience single-sample push (used by tests).
    void push(int node, float v) { push_block(node, &v, 1); }

    // Frame thread: copy node `node`'s last min(cnt, N) samples (oldest→newest) into `out`; returns count.
    int snapshot(int node, float* out, int cnt) const {
        if (!slots_ || node < 0 || node >= nnodes_) return 0;
        const int c = std::min(cnt, n_);
        const std::atomic<float>* r = slots_.get() + static_cast<size_t>(node) * n_;
        const uint32_t head = heads_[node].load(std::memory_order_acquire);   // acquire: orders the loads below
        const uint32_t start = (head + static_cast<uint32_t>(n_) - static_cast<uint32_t>(c)) & mask_;
        for (int i = 0; i < c; ++i)
            out[i] = r[(start + static_cast<uint32_t>(i)) & mask_].load(std::memory_order_relaxed);
        return c;
    }

private:
    std::unique_ptr<std::atomic<float>[]>    slots_;
    std::unique_ptr<std::atomic<uint32_t>[]> heads_;
    int      nnodes_ = 0, n_ = 0;
    uint32_t mask_ = 0;
};

}  // namespace vivid::audio
