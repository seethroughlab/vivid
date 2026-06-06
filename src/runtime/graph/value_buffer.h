#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/graph/lane_types.h"  // ValueEnvelope, VividValueType/Multiplicity

// Forward-declare WebGPU buffer handle (avoids pulling webgpu.h into audio paths).
struct WGPUBufferImpl;
typedef WGPUBufferImpl* WGPUBuffer;

namespace vivid {

// GPU backing state for value buffers (mirrors the lane path; lane-value
// clean-break Phase 7). CpuOnly until a GPU consumer promotes the buffer.
enum class ValueGpuBacking : uint8_t {
    CpuOnly = 0,  // no GPU buffer allocated
    CpuDirty,     // CPU has newer data, GPU buffer exists but stale
    GpuDirty,     // GPU has newer data (future: GPU compute producers)
    Synced,       // CPU and GPU hold identical data
};

// ---------------------------------------------------------------------------
// ValueBuffer — unified storage for one value (scalar or many) of a CPU payload
// type (lane-value clean-break, Phase 3). Subsumes the three separate CPU
// storage paths: LaneBuffer (float), StringLaneBuffer (string), and the custom
// byte snapshot — into one payload-tagged buffer carrying the value envelope.
//
// Texture / audio-block / GPU payloads stay handle-based for now; envelope
// .storage_kind records the intended policy for Phases 4-5. ValueBuffer is the
// successor to LaneBuffer and follows the same RT-safety contract: pre-allocated
// at build (audio stopped); during execution ensure() returns false rather than
// allocating when a fixed-capacity (audio) buffer would overflow. Refcounting is
// intrusive + lock-free; release() never frees (the arena reclaims on the frame
// thread). Additive: not yet consumed by execution.
// ---------------------------------------------------------------------------

struct ValueBuffer {
    ValueEnvelope             envelope;          // type/multiplicity/count/identity/storage
    std::vector<float>        floats;            // FLOAT / AUDIO (interleaved samples)
    std::vector<std::string>  strings;           // STRING
    std::vector<uint8_t>      bytes;             // CUSTOM (by value)
    uint32_t                  committed_count = 0;
    uint32_t                  lane_set_id = 0;   // provenance (carried through propagation)
    std::atomic<uint32_t>     ref_count{0};
    bool                      pool_owned = false;
    // When true, ensure() may grow even if pool-owned. Set only for frame-thread
    // arenas; audio-thread arenas leave it false to preserve no-alloc RT safety.
    bool                      allow_grow = false;

    // GPU storage-buffer backing (FLOAT/AUDIO payloads). CpuOnly by default;
    // promoted by value_buffer_ensure_gpu() on the frame thread.
    ValueGpuBacking           gpu_backing = ValueGpuBacking::CpuOnly;
    WGPUBuffer                gpu_buffer = nullptr;
    uint32_t                  gpu_buffer_capacity = 0;  // allocated size in floats

    explicit ValueBuffer(VividValueType vt = VIVID_VALUE_FLOAT, uint32_t capacity = 0) {
        envelope.value_type = vt;
        switch (vt) {
            case VIVID_VALUE_STRING: strings.assign(capacity, std::string()); break;
            case VIVID_VALUE_CUSTOM: bytes.assign(capacity, 0);               break;
            default:                 floats.assign(capacity, 0.0f);           break; // FLOAT/AUDIO
        }
    }

    // Non-copyable (atomic member), movable for container use.
    ValueBuffer(const ValueBuffer&) = delete;
    ValueBuffer& operator=(const ValueBuffer&) = delete;
    ~ValueBuffer() { if (gpu_buffer) release_gpu(); }

    ValueBuffer(ValueBuffer&& o) noexcept
        : envelope(o.envelope)
        , floats(std::move(o.floats))
        , strings(std::move(o.strings))
        , bytes(std::move(o.bytes))
        , committed_count(o.committed_count)
        , lane_set_id(o.lane_set_id)
        , ref_count(o.ref_count.load(std::memory_order_relaxed))
        , pool_owned(o.pool_owned)
        , allow_grow(o.allow_grow)
        , gpu_backing(o.gpu_backing)
        , gpu_buffer(o.gpu_buffer)
        , gpu_buffer_capacity(o.gpu_buffer_capacity) {
        o.committed_count = 0;
        o.ref_count.store(0, std::memory_order_relaxed);
        o.gpu_buffer = nullptr;
        o.gpu_backing = ValueGpuBacking::CpuOnly;
        o.gpu_buffer_capacity = 0;
    }
    ValueBuffer& operator=(ValueBuffer&& o) noexcept {
        if (this != &o) {
            if (gpu_buffer) release_gpu();
            envelope = o.envelope;
            floats = std::move(o.floats);
            strings = std::move(o.strings);
            bytes = std::move(o.bytes);
            committed_count = o.committed_count;
            lane_set_id = o.lane_set_id;
            ref_count.store(o.ref_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            pool_owned = o.pool_owned;
            allow_grow = o.allow_grow;
            gpu_backing = o.gpu_backing;
            gpu_buffer = o.gpu_buffer;
            gpu_buffer_capacity = o.gpu_buffer_capacity;
            o.committed_count = 0;
            o.ref_count.store(0, std::memory_order_relaxed);
            o.gpu_buffer = nullptr;
            o.gpu_backing = ValueGpuBacking::CpuOnly;
            o.gpu_buffer_capacity = 0;
        }
        return *this;
    }

    void retain() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    void release() { ref_count.fetch_sub(1, std::memory_order_acq_rel); }

    // Element capacity of the active payload's backing storage.
    uint32_t capacity() const {
        switch (envelope.value_type) {
            case VIVID_VALUE_STRING: return static_cast<uint32_t>(strings.size());
            case VIVID_VALUE_CUSTOM: return static_cast<uint32_t>(bytes.size());
            default:                 return static_cast<uint32_t>(floats.size());
        }
    }

    // Ensure the active payload can hold `count` elements. Grows on the frame
    // thread (or non-pool buffers); for a fixed-capacity pool-owned (audio)
    // buffer that would overflow, returns false WITHOUT allocating (RT-safe).
    bool ensure(uint32_t count) {
        if (count <= capacity()) return true;
        if (pool_owned && !allow_grow) return false;  // audio-safe: no alloc
        switch (envelope.value_type) {
            case VIVID_VALUE_STRING: strings.resize(count);        break;
            case VIVID_VALUE_CUSTOM: bytes.resize(count, 0);       break;
            default:                 floats.resize(count, 0.0f);   break;
        }
        return true;
    }

    // Typed writable accessors (call ensure() first). Return nullptr on type
    // mismatch.
    float*   floats_ptr() { return (envelope.value_type == VIVID_VALUE_FLOAT ||
                                    envelope.value_type == VIVID_VALUE_AUDIO)
                                       ? floats.data() : nullptr; }
    uint8_t* bytes_ptr()  { return envelope.value_type == VIVID_VALUE_CUSTOM ? bytes.data() : nullptr; }
    void set_string(uint32_t i, const char* v) {
        if (envelope.value_type == VIVID_VALUE_STRING && i < strings.size())
            strings[i] = v ? v : "";
    }

    void commit(uint32_t count) {
        committed_count = (count <= capacity()) ? count : capacity();
        envelope.value_count  = committed_count;
        envelope.multiplicity = (committed_count > 1) ? VIVID_MULTIPLICITY_MANY
                                                       : VIVID_MULTIPLICITY_SCALAR;
        if (gpu_buffer) gpu_backing = ValueGpuBacking::CpuDirty;
    }

    void reset() {
        committed_count = 0;
        if (gpu_buffer) release_gpu();
    }

    // Release GPU storage buffer. Safe to call even if none exists. Defined in
    // value_buffer_gpu.cpp to keep webgpu.h out of audio-path headers.
    void release_gpu();
};

// ---------------------------------------------------------------------------
// ValueRef — RAII intrusive reference to a ValueBuffer (successor to
// LaneBufferRef). copy = retain, destroy = release; release never frees.
// ---------------------------------------------------------------------------

struct ValueRef {
    ValueBuffer* buf = nullptr;

    ValueRef() = default;
    explicit ValueRef(ValueBuffer* b) : buf(b) { if (buf) buf->retain(); }
    ~ValueRef() { if (buf) buf->release(); }

    ValueRef(const ValueRef& o) : buf(o.buf) { if (buf) buf->retain(); }
    ValueRef& operator=(const ValueRef& o) {
        if (this != &o) {
            if (buf) buf->release();
            buf = o.buf;
            if (buf) buf->retain();
        }
        return *this;
    }
    ValueRef(ValueRef&& o) noexcept : buf(o.buf) { o.buf = nullptr; }
    ValueRef& operator=(ValueRef&& o) noexcept {
        if (this != &o) {
            if (buf) buf->release();
            buf = o.buf;
            o.buf = nullptr;
        }
        return *this;
    }

    explicit operator bool() const { return buf && buf->committed_count > 0; }
    uint32_t count() const { return buf ? buf->committed_count : 0; }
    const float* floats() const { return buf ? buf->floats.data() : nullptr; }
    bool empty() const { return !buf || buf->committed_count == 0; }
};

// Create a ref to a node-local (non-pool) ValueBuffer. The buffer must outlive
// all refs — guaranteed for CompiledNode::out_value_bufs during execution.
inline ValueRef make_ref_from_existing(ValueBuffer* buf) {
    return ValueRef(buf);
}

} // namespace vivid
