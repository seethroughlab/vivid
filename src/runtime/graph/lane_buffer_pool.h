#pragma once

#include "runtime/graph/lane_buffer.h"
#include <memory>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// LaneBufferPool — pre-allocated pool of LaneBuffers for frame-thread use.
//
// acquire() pops a buffer from the free list. If the free list is empty, a
// new buffer is allocated (frame thread only — never on the audio thread).
// sweep() reclaims pool-owned buffers whose ref_count has dropped to zero.
// Called at tick start to return stale buffers to the free list.
// ---------------------------------------------------------------------------

class LaneBufferPool {
public:
    // growable: when true, buffers acquired from this pool may grow past
    // buffer_capacity on resize() (frame-thread pools only — never the audio
    // bridge, which must stay no-alloc).
    explicit LaneBufferPool(uint32_t buffer_capacity = 1024, bool growable = false)
        : buffer_capacity_(buffer_capacity), growable_(growable) {}

    // Acquire a mutable buffer. Returns a raw pointer — caller wraps in
    // LaneBufferRef after writing. Buffer data is NOT zeroed; caller must
    // write committed_length elements before committing.
    LaneBuffer* acquire() {
        if (!free_list_.empty()) {
            LaneBuffer* buf = free_list_.back();
            free_list_.pop_back();
            buf->reset();
            return buf;
        }
        // Allocate new (frame thread only).
        auto& ptr = all_buffers_.emplace_back(std::make_unique<LaneBuffer>(buffer_capacity_));
        ptr->pool_owned = true;
        ptr->allow_grow = growable_;
        return ptr.get();
    }

    // Reclaim buffers with ref_count == 0. Called at frame-tick start.
    void sweep() {
        for (auto& ptr : all_buffers_) {
            if (ptr->pool_owned &&
                ptr->ref_count.load(std::memory_order_acquire) == 0 &&
                !is_free(ptr.get())) {
                ptr->reset();
                free_list_.push_back(ptr.get());
            }
        }
    }

    // Pre-allocate buffers (called during graph compilation).
    void prewarm(uint32_t count) {
        all_buffers_.reserve(all_buffers_.size() + count);
        free_list_.reserve(free_list_.size() + count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& ptr = all_buffers_.emplace_back(std::make_unique<LaneBuffer>(buffer_capacity_));
            ptr->pool_owned = true;
            ptr->allow_grow = growable_;
            free_list_.push_back(ptr.get());
        }
    }

    // Release all buffers. Call on shutdown or graph rebuild.
    void clear() {
        free_list_.clear();
        all_buffers_.clear();
    }

    uint32_t total_count() const { return static_cast<uint32_t>(all_buffers_.size()); }
    uint32_t free_count() const { return static_cast<uint32_t>(free_list_.size()); }

private:
    bool is_free(LaneBuffer* buf) const {
        for (auto* f : free_list_)
            if (f == buf) return true;
        return false;
    }

    uint32_t buffer_capacity_;
    bool growable_ = false;
    std::vector<std::unique_ptr<LaneBuffer>> all_buffers_;
    std::vector<LaneBuffer*> free_list_;
};

} // namespace vivid
