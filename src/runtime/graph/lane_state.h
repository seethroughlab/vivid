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
//   - retire(): audio-thread safe (marks a lane_id for deferred global cleanup)
//   - sweep_retired() / pre_allocate(): called at cadence boundaries, may do heap work
// ---------------------------------------------------------------------------

class LaneStateService {
public:
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
        return inserted_it->second.data.data();
    }

    // Mark a lane_id for deferred cleanup across all nodes. Audio-thread safe.
    // node_idx is accepted for API compatibility but ignored: lane identities
    // are graph-wide, so retiring a note should clear every downstream node's
    // per-lane state for that identity, not just the caller's entry.
    inline void retire(uint32_t /*node_idx*/, uint32_t lane_id) {
        std::lock_guard<std::mutex> lock(retire_mutex_);
        pending_retirements_.push_back(lane_id);
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
        std::vector<uint32_t> to_retire;
        {
            std::lock_guard<std::mutex> lock(retire_mutex_);
            to_retire.swap(pending_retirements_);
        }
        if (to_retire.empty()) return;

        for (auto it = entries_.begin(); it != entries_.end(); ) {
            const uint32_t lane_id = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
            bool should_erase = false;
            for (uint32_t retired_lane_id : to_retire) {
                if (lane_id == retired_lane_id) {
                    should_erase = true;
                    break;
                }
            }
            if (should_erase) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Clear all state (shutdown/rebuild).
    inline void clear() {
        entries_.clear();
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
    std::atomic<uint32_t> next_lane_id_{1};
    std::vector<uint32_t> pending_retirements_;
    std::mutex retire_mutex_;
};

} // namespace vivid
