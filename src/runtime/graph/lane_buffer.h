#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Forward-declare WebGPU buffer handle (avoids pulling webgpu.h into audio paths).
struct WGPUBufferImpl;
typedef WGPUBufferImpl* WGPUBuffer;

namespace vivid {

// GPU backing state for lane buffers.
enum class LaneGpuBacking : uint8_t {
    CpuOnly = 0,  // no GPU buffer allocated
    CpuDirty,     // CPU has newer data, GPU buffer exists but stale
    GpuDirty,     // GPU has newer data (future: GPU compute producers)
    Synced,        // CPU and GPU hold identical data
};

// ---------------------------------------------------------------------------
// LaneBuffer — lane storage with intrusive refcount and optional GPU backing.
//
// Pre-allocated during graph compilation (audio stopped). During execution,
// resize() returns a pointer into the existing allocation — no heap work.
// Audio-thread safe: no allocation, no locking, no blocking.
//
// Refcounting is intrusive and lock-free. release() never deallocates —
// pool-owned buffers are reclaimed by LaneBufferPool::sweep() on the frame
// thread. Node-local buffers (out_lane_bufs) are stack-lifetime.
// ---------------------------------------------------------------------------

struct LaneBuffer {
    std::vector<float> data;
    uint32_t committed_length = 0;
    uint32_t lane_set_id = 0;
    std::atomic<uint32_t> ref_count{0};
    bool pool_owned = false;

    // GPU storage-buffer backing (Phase 4). CpuOnly by default.
    LaneGpuBacking gpu_backing = LaneGpuBacking::CpuOnly;
    WGPUBuffer gpu_buffer = nullptr;
    uint32_t gpu_buffer_capacity = 0; // allocated size in floats

    explicit LaneBuffer(uint32_t capacity = 0)
        : data(capacity, 0.0f) {}

    // Non-copyable due to atomic member, but movable for container use.
    LaneBuffer(const LaneBuffer&) = delete;
    LaneBuffer& operator=(const LaneBuffer&) = delete;
    ~LaneBuffer() { release_gpu(); }

    LaneBuffer(LaneBuffer&& o) noexcept
        : data(std::move(o.data))
        , committed_length(o.committed_length)
        , lane_set_id(o.lane_set_id)
        , ref_count(o.ref_count.load(std::memory_order_relaxed))
        , pool_owned(o.pool_owned)
        , gpu_backing(o.gpu_backing)
        , gpu_buffer(o.gpu_buffer)
        , gpu_buffer_capacity(o.gpu_buffer_capacity) {
        o.committed_length = 0;
        o.ref_count.store(0, std::memory_order_relaxed);
        o.gpu_buffer = nullptr;
        o.gpu_backing = LaneGpuBacking::CpuOnly;
        o.gpu_buffer_capacity = 0;
    }
    LaneBuffer& operator=(LaneBuffer&& o) noexcept {
        if (this != &o) {
            release_gpu();
            data = std::move(o.data);
            committed_length = o.committed_length;
            lane_set_id = o.lane_set_id;
            ref_count.store(o.ref_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            pool_owned = o.pool_owned;
            gpu_backing = o.gpu_backing;
            gpu_buffer = o.gpu_buffer;
            gpu_buffer_capacity = o.gpu_buffer_capacity;
            o.committed_length = 0;
            o.ref_count.store(0, std::memory_order_relaxed);
            o.gpu_buffer = nullptr;
            o.gpu_backing = LaneGpuBacking::CpuOnly;
            o.gpu_buffer_capacity = 0;
        }
        return *this;
    }

    void retain() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    void release() { ref_count.fetch_sub(1, std::memory_order_acq_rel); }

    // Returns a writable pointer. For non-pool buffers (frame-thread output
    // builders), grows the backing vector if needed. For pool-owned buffers,
    // returns nullptr if length exceeds pre-allocated capacity (no alloc on
    // audio thread). Growth is bounded by max_lane_elements (caller's responsibility).
    float* resize(uint32_t length) {
        if (length == 0) return data.data();
        if (length > static_cast<uint32_t>(data.size())) {
            if (pool_owned) return nullptr; // audio-safe: no alloc
            data.resize(length, 0.0f);      // frame-thread growth OK
        }
        return data.data();
    }

    void commit(uint32_t length) {
        committed_length = (length <= static_cast<uint32_t>(data.size()))
                               ? length
                               : static_cast<uint32_t>(data.size());
        if (gpu_buffer) gpu_backing = LaneGpuBacking::CpuDirty;
    }

    void reset() {
        committed_length = 0;
        release_gpu();
    }

    // Release GPU storage buffer. Safe to call even if no GPU buffer exists.
    void release_gpu();
};

// ---------------------------------------------------------------------------
// LaneBufferRef — RAII reference to an immutable LaneBuffer.
//
// Lightweight value type: copy = retain, destroy = release. The underlying
// buffer is never freed by release — pool sweep or node destruction handles
// that. Safe to hold on audio and frame threads.
// ---------------------------------------------------------------------------

struct LaneBufferRef {
    LaneBuffer* buf = nullptr;

    LaneBufferRef() = default;

    explicit LaneBufferRef(LaneBuffer* b) : buf(b) {
        if (buf) buf->retain();
    }

    ~LaneBufferRef() {
        if (buf) buf->release();
    }

    LaneBufferRef(const LaneBufferRef& o) : buf(o.buf) {
        if (buf) buf->retain();
    }

    LaneBufferRef& operator=(const LaneBufferRef& o) {
        if (this != &o) {
            if (buf) buf->release();
            buf = o.buf;
            if (buf) buf->retain();
        }
        return *this;
    }

    LaneBufferRef(LaneBufferRef&& o) noexcept : buf(o.buf) {
        o.buf = nullptr;
    }

    LaneBufferRef& operator=(LaneBufferRef&& o) noexcept {
        if (this != &o) {
            if (buf) buf->release();
            buf = o.buf;
            o.buf = nullptr;
        }
        return *this;
    }

    explicit operator bool() const { return buf && buf->committed_length > 0; }
    const float* data() const { return buf ? buf->data.data() : nullptr; }
    uint32_t length() const { return buf ? buf->committed_length : 0; }
    bool empty() const { return !buf || buf->committed_length == 0; }
};

// Create a ref to a node-local (non-pool) buffer. The buffer must outlive
// all refs — guaranteed for CompiledNode::out_lane_bufs during execution.
inline LaneBufferRef make_ref_from_existing(LaneBuffer* buf) {
    return LaneBufferRef(buf);
}

// ---------------------------------------------------------------------------
// StringLaneBuffer — CPU-backed string lane storage.
//
// Strings are copied into runtime-owned storage during set() so operators
// do not need to keep source pointers alive after process_*() returns.
// ---------------------------------------------------------------------------

struct StringLaneBuffer {
    std::vector<std::string> owned;       // runtime-owned string copies
    std::vector<const char*> ptrs;        // c_str() pointers (rebuilt after set/commit)
    uint32_t committed_length = 0;
    uint32_t lane_set_id = 0;

    explicit StringLaneBuffer(uint32_t capacity = 0)
        : owned(capacity), ptrs(capacity, nullptr) {}

    // Returns 1 on success, 0 if length exceeds capacity.
    uint8_t resize(uint32_t length) {
        if (length > static_cast<uint32_t>(owned.size())) return 0;
        return 1;
    }

    void set(uint32_t index, const char* value) {
        if (index < static_cast<uint32_t>(owned.size())) {
            owned[index] = value ? value : "";
            ptrs[index] = owned[index].c_str();
        }
    }

    void commit(uint32_t length) {
        committed_length = (length <= static_cast<uint32_t>(owned.size()))
                               ? length
                               : static_cast<uint32_t>(owned.size());
    }

    void reset() { committed_length = 0; }
};

} // namespace vivid
