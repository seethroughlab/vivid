#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
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
//   - retire(): audio-thread safe (marks for deferred cleanup)
//   - sweep_retired() / pre_allocate(): frame-thread only (heap operations)
// ---------------------------------------------------------------------------

class LaneStateService {
public:
    // Allocate a fresh, globally unique lane_id. Audio-thread safe.
    uint32_t allocate_lane_id() {
        return next_lane_id_.fetch_add(1, std::memory_order_relaxed);
    }

    // Get per-lane state for (node_idx, lane_id).
    // Returns zero-initialized storage of byte_size bytes.
    // If the entry doesn't exist yet (first audio callback after allocation),
    // returns a per-node scratch buffer (zero-initialized, not identity-stable).
    inline void* get(uint32_t node_idx, uint32_t lane_id, uint32_t byte_size) {
        uint64_t key = make_key(node_idx, lane_id);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            assert(it->second.data.size() == byte_size &&
                   "lane_state: byte_size mismatch for existing (node_idx, lane_id)");
            return it->second.data.data();
        }
        // Entry doesn't exist yet. Return per-node scratch buffer.
        auto& scratch = scratch_[node_idx];
        if (scratch.size() < byte_size)
            scratch.resize(byte_size, 0);
        std::memset(scratch.data(), 0, byte_size);
        return scratch.data();
    }

    // Mark a (node_idx, lane_id) for deferred cleanup. Audio-thread safe.
    inline void retire(uint32_t node_idx, uint32_t lane_id) {
        uint64_t key = make_key(node_idx, lane_id);
        std::lock_guard<std::mutex> lock(retire_mutex_);
        pending_retirements_.push_back(key);
    }

    // Pre-allocate storage for a lane_id. Frame-thread only.
    inline void pre_allocate(uint32_t node_idx, uint32_t lane_id, uint32_t byte_size) {
        uint64_t key = make_key(node_idx, lane_id);
        if (entries_.find(key) == entries_.end()) {
            Entry entry;
            entry.data.resize(byte_size, 0);
            entries_[key] = std::move(entry);
        }
    }

    // Clean up retired state. Frame-thread only.
    inline void sweep_retired() {
        std::vector<uint64_t> to_retire;
        {
            std::lock_guard<std::mutex> lock(retire_mutex_);
            to_retire.swap(pending_retirements_);
        }
        for (uint64_t key : to_retire)
            entries_.erase(key);
    }

    // Clear all state (shutdown/rebuild).
    inline void clear() {
        entries_.clear();
        scratch_.clear();
        pending_retirements_.clear();
        next_lane_id_.store(1, std::memory_order_relaxed);
    }

private:
    static uint64_t make_key(uint32_t node_idx, uint32_t lane_id) {
        return (static_cast<uint64_t>(node_idx) << 32) | lane_id;
    }

    struct Entry {
        std::vector<uint8_t> data;
    };

    std::unordered_map<uint64_t, Entry> entries_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> scratch_;
    std::atomic<uint32_t> next_lane_id_{1};
    std::vector<uint64_t> pending_retirements_;
    std::mutex retire_mutex_;
};

} // namespace vivid
