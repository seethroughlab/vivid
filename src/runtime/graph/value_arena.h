#pragma once

#include "runtime/graph/value_buffer.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// ValueArena — pre-allocated pool of ValueBuffers (lane-value clean-break,
// Phase 3). Successor to LaneBufferPool, generalized to any CPU payload type.
//
// One arena holds one payload type. `growable` (frame-thread arenas) may
// allocate on acquire()/ensure(); fixed arenas (audio) never allocate after
// prewarm — acquire() returns nullptr when exhausted, preserving RT safety.
// sweep() reclaims pool-owned buffers whose ref_count dropped to zero (frame
// thread). Additive: not yet consumed by execution.
// ---------------------------------------------------------------------------

class ValueArena {
public:
    explicit ValueArena(VividValueType value_type = VIVID_VALUE_FLOAT,
                        uint32_t buffer_capacity = 1024, bool growable = false)
        : value_type_(value_type), buffer_capacity_(buffer_capacity), growable_(growable) {}

    // Acquire a mutable buffer of this arena's payload type. Returns a raw
    // pointer (caller wraps in ValueRef after writing). For a fixed arena with
    // an empty free list, returns nullptr (RT-safe: no allocation).
    ValueBuffer* acquire() {
        if (!free_list_.empty()) {
            ValueBuffer* buf = free_list_.back();
            free_list_.pop_back();
            buf->reset();
            return buf;
        }
        if (!growable_) return nullptr;  // fixed arena exhausted — no alloc
        return make_buffer();
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

    // Pre-allocate `count` buffers (called during graph compilation, audio stopped).
    void prewarm(uint32_t count) {
        all_buffers_.reserve(all_buffers_.size() + count);
        free_list_.reserve(free_list_.size() + count);
        for (uint32_t i = 0; i < count; ++i)
            free_list_.push_back(make_buffer());
    }

    void clear() { free_list_.clear(); all_buffers_.clear(); }

    VividValueType value_type() const { return value_type_; }
    uint32_t buffer_capacity() const { return buffer_capacity_; }
    bool growable() const { return growable_; }
    uint32_t total_count() const { return static_cast<uint32_t>(all_buffers_.size()); }
    uint32_t free_count() const { return static_cast<uint32_t>(free_list_.size()); }

private:
    ValueBuffer* make_buffer() {
        auto& ptr = all_buffers_.emplace_back(
            std::make_unique<ValueBuffer>(value_type_, buffer_capacity_));
        ptr->pool_owned = true;
        ptr->allow_grow = growable_;
        return ptr.get();
    }
    bool is_free(ValueBuffer* buf) const {
        for (auto* f : free_list_)
            if (f == buf) return true;
        return false;
    }

    VividValueType value_type_;
    uint32_t buffer_capacity_;
    bool growable_ = false;
    std::vector<std::unique_ptr<ValueBuffer>> all_buffers_;
    std::vector<ValueBuffer*> free_list_;
};

// ---------------------------------------------------------------------------
// ValueHealthCounters — runtime-health diagnostics for the value transport
// (lane-value clean-break, Phase 3). Folds audit 01-R2-F7 (bridge overflow)
// and adds the identity-truncation / dropped-many counters the design asks for.
// Monotonic; surfaced via runtime health once the bridge is wired (Phase 5).
// ---------------------------------------------------------------------------

struct ValueHealthCounters {
    uint32_t bridge_overflow     = 0;  // a many-value exceeded a fixed bridge slot
    uint32_t identity_truncation = 0;  // stable-id identity dropped to positional
    uint32_t dropped_many        = 0;  // a many-value could not be transported

    void reset() { bridge_overflow = identity_truncation = dropped_many = 0; }
};

} // namespace vivid
