#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// LaneStateService — per-lane persistent state keyed by (node_idx, lane_id).
//
// Operators use this to maintain identity-stable state that survives lane
// reordering and compaction. State follows lane_id, not positional index.
//
// RT-safety contract:
//   - allocate_lane_id(): audio-thread safe (atomic counter, no heap)
//   - get(): audio-thread safe IF the entry was pre-allocated by the frame
//     thread. Returns per-node scratch buffer for unknown lane_ids.
//   - retire(): audio-thread safe (marks a lane_id for deferred global cleanup)
//   - sweep_retired() / pre_allocate(): called at cadence boundaries, may do heap work
// ---------------------------------------------------------------------------

class LaneStateService {
public:
    void set_node_capacity(uint32_t count) {
        live_entry_count_capacity_ = count;
        live_entry_counts_.reset();
        pending_retirements_.reserve(128);  // avoids alloc on note-off on audio thread
        retiring_scratch_.reserve(128);
        if (count == 0) return;
        live_entry_counts_ = std::make_unique<std::atomic<uint32_t>[]>(count);
        for (uint32_t i = 0; i < count; ++i)
            live_entry_counts_[i].store(0, std::memory_order_relaxed);
    }

    // Allocate a fresh, globally unique lane_id. Audio-thread safe.
    uint32_t allocate_lane_id() {
        return next_lane_id_.fetch_add(1, std::memory_order_relaxed);
    }

    // Get per-lane state for (node_idx, lane_id).
    // Returns zero-initialized storage of byte_size bytes.
    // Allocates on first access (one heap allocation per voice creation,
    // not per-sample — acceptable for typical voice counts of 1-16).
    // Stable until retire() + sweep_retired().
    inline void* get(uint32_t node_idx, uint32_t lane_id, uint32_t byte_size) {
        uint64_t key = make_key(node_idx, lane_id);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            assert(it->second.data.size() == byte_size &&
                   "lane_state: byte_size mismatch for existing (node_idx, lane_id)");
            return it->second.data.data();
        }
        // First access for this (node_idx, lane_id): allocate identity-stable storage.
        Entry entry;
        entry.data.resize(byte_size, 0);
        auto [inserted_it, _] = entries_.emplace(key, std::move(entry));
        increment_live_entry_count(node_idx);
        return inserted_it->second.data.data();
    }

    // Mark a lane_id for deferred cleanup across all nodes. Audio-thread safe.
    // node_idx is accepted for API compatibility but ignored: lane identities
    // are graph-wide, so retiring a note should clear every downstream node's
    // per-lane state for that identity, not just the caller's entry.
    inline void retire(uint32_t /*node_idx*/, uint32_t lane_id) {
        std::lock_guard<std::mutex> lock(retire_mutex_);
        pending_retirements_.push_back(lane_id);
        retirements_pending_.store(true, std::memory_order_release);
    }

    // Pre-allocate storage for a lane_id. Frame-thread only.
    inline void pre_allocate(uint32_t node_idx, uint32_t lane_id, uint32_t byte_size) {
        uint64_t key = make_key(node_idx, lane_id);
        if (entries_.find(key) == entries_.end()) {
            Entry entry;
            entry.data.resize(byte_size, 0);
            entries_[key] = std::move(entry);
            increment_live_entry_count(node_idx);
        }
    }

    // Clean up retired state. Called on the owning executor's thread (frame or audio).
    inline void sweep_retired() {
        if (!retirements_pending_.load(std::memory_order_acquire)) return;  // hot path: zero lock
        {
            std::lock_guard<std::mutex> lock(retire_mutex_);
            retiring_scratch_.swap(pending_retirements_);
            retirements_pending_.store(false, std::memory_order_relaxed);
        }
        if (retiring_scratch_.empty()) return;

        for (auto it = entries_.begin(); it != entries_.end(); ) {
            const uint32_t lane_id = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
            bool should_erase = false;
            for (uint32_t retired_lane_id : retiring_scratch_) {
                if (lane_id == retired_lane_id) {
                    should_erase = true;
                    break;
                }
            }
            if (should_erase) {
                decrement_live_entry_count(static_cast<uint32_t>(it->first >> 32));
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        retiring_scratch_.clear();  // keep allocation for next sweep
    }

    // Clear all state (shutdown/rebuild).
    inline void clear() {
        entries_.clear();
        pending_retirements_.clear();
        retiring_scratch_.clear();
        retirements_pending_.store(false, std::memory_order_relaxed);
        next_lane_id_.store(1, std::memory_order_relaxed);
        for (uint32_t i = 0; i < live_entry_count_capacity_; ++i)
            live_entry_counts_[i].store(0, std::memory_order_relaxed);
    }

    uint32_t live_entry_count(uint32_t node_idx) const {
        if (!live_entry_counts_ || node_idx >= live_entry_count_capacity_) return 0;
        return live_entry_counts_[node_idx].load(std::memory_order_relaxed);
    }

private:
    static uint64_t make_key(uint32_t node_idx, uint32_t lane_id) {
        return (static_cast<uint64_t>(node_idx) << 32) | lane_id;
    }

    struct Entry {
        std::vector<uint8_t> data;
    };

    void increment_live_entry_count(uint32_t node_idx) {
        if (!live_entry_counts_ || node_idx >= live_entry_count_capacity_) return;
        live_entry_counts_[node_idx].fetch_add(1, std::memory_order_relaxed);
    }

    void decrement_live_entry_count(uint32_t node_idx) {
        if (!live_entry_counts_ || node_idx >= live_entry_count_capacity_) return;
        uint32_t prev = live_entry_counts_[node_idx].load(std::memory_order_relaxed);
        if (prev == 0) return;
        live_entry_counts_[node_idx].fetch_sub(1, std::memory_order_relaxed);
    }

    std::unordered_map<uint64_t, Entry> entries_;
    std::atomic<uint32_t> next_lane_id_{1};
    std::vector<uint32_t> pending_retirements_;
    std::vector<uint32_t> retiring_scratch_;  // persists allocation between sweeps
    std::mutex retire_mutex_;
    std::atomic<bool> retirements_pending_{false};
    std::unique_ptr<std::atomic<uint32_t>[]> live_entry_counts_;
    uint32_t live_entry_count_capacity_ = 0;
};

} // namespace vivid
