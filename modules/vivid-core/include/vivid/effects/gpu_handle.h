#pragma once

/**
 * @file gpu_handle.h
 * @brief RAII wrappers for WebGPU handles
 *
 * Move-only smart handles that automatically release GPU resources on destruction.
 * Eliminates manual cleanup() boilerplate and prevents resource leaks.
 *
 * @par Example
 * @code
 * class MyEffect : public TextureOperator {
 *     GpuHandle<WGPURenderPipeline> m_pipeline;
 *     GpuHandle<WGPUBindGroupLayout> m_layout;
 *     GpuHandle<WGPUBuffer> m_buffer;
 *
 *     void init(Context& ctx) override {
 *         m_pipeline.reset(createPipeline(...));
 *         m_layout.reset(createLayout(...));
 *         m_buffer.reset(createBuffer(...));
 *     }
 *
 *     // No cleanup() override needed - handles auto-release!
 * };
 * @endcode
 */

#include <webgpu/webgpu.h>
#include <utility>

namespace vivid {

// =============================================================================
// Release Traits
// =============================================================================

/**
 * @brief Type traits for WebGPU handle release functions
 *
 * Specializations provide the correct wgpu*Release() function for each type.
 */
template<typename T>
struct WGPUReleaseTrait;

template<>
struct WGPUReleaseTrait<WGPUTexture> {
    static void release(WGPUTexture h) { if (h) wgpuTextureRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUTextureView> {
    static void release(WGPUTextureView h) { if (h) wgpuTextureViewRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUBuffer> {
    static void release(WGPUBuffer h) { if (h) wgpuBufferRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPURenderPipeline> {
    static void release(WGPURenderPipeline h) { if (h) wgpuRenderPipelineRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUComputePipeline> {
    static void release(WGPUComputePipeline h) { if (h) wgpuComputePipelineRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUBindGroup> {
    static void release(WGPUBindGroup h) { if (h) wgpuBindGroupRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUBindGroupLayout> {
    static void release(WGPUBindGroupLayout h) { if (h) wgpuBindGroupLayoutRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUSampler> {
    static void release(WGPUSampler h) { if (h) wgpuSamplerRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUShaderModule> {
    static void release(WGPUShaderModule h) { if (h) wgpuShaderModuleRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUPipelineLayout> {
    static void release(WGPUPipelineLayout h) { if (h) wgpuPipelineLayoutRelease(h); }
};

template<>
struct WGPUReleaseTrait<WGPUQuerySet> {
    static void release(WGPUQuerySet h) { if (h) wgpuQuerySetRelease(h); }
};

// =============================================================================
// RAII Handle Wrapper
// =============================================================================

/**
 * @brief RAII wrapper for WebGPU handles
 *
 * Move-only smart handle that automatically releases the wrapped GPU resource
 * when destroyed. Eliminates manual cleanup code and prevents leaks.
 *
 * @tparam T The WebGPU handle type (e.g., WGPUTexture, WGPUBuffer)
 *
 * @par Design Notes
 * - Move-only (no copying) to maintain unique ownership
 * - Implicit conversion to raw handle for compatibility with WebGPU APIs
 * - reset() releases current handle and optionally takes a new one
 * - release() transfers ownership out (use with caution)
 */
template<typename T>
class GpuHandle {
public:
    /// @brief Default constructor - null handle
    GpuHandle() = default;

    /// @brief Construct from raw handle, taking ownership
    explicit GpuHandle(T handle) : m_handle(handle) {}

    /// @brief Destructor - releases the handle
    ~GpuHandle() { reset(); }

    // Move semantics
    GpuHandle(GpuHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    GpuHandle& operator=(GpuHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    // Non-copyable
    GpuHandle(const GpuHandle&) = delete;
    GpuHandle& operator=(const GpuHandle&) = delete;

    // -------------------------------------------------------------------------
    /// @name Accessors
    /// @{

    /// @brief Get raw handle
    T get() const { return m_handle; }

    /// @brief Get pointer to handle (for output parameters)
    T* ptr() { return &m_handle; }

    /// @brief Implicit conversion to raw handle
    operator T() const { return m_handle; }

    /// @brief Check if handle is valid
    explicit operator bool() const { return m_handle != nullptr; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modifiers
    /// @{

    /**
     * @brief Release current handle and optionally take a new one
     * @param handle New handle to manage (default: nullptr)
     */
    void reset(T handle = nullptr) {
        WGPUReleaseTrait<T>::release(m_handle);
        m_handle = handle;
    }

    /**
     * @brief Transfer ownership out
     * @return The raw handle (caller takes ownership)
     *
     * After calling, this handle is null and no release will occur.
     */
    T release() {
        T h = m_handle;
        m_handle = nullptr;
        return h;
    }

    /// @}

private:
    T m_handle = nullptr;
};

// =============================================================================
// Convenience Type Aliases
// =============================================================================

using TextureHandle = GpuHandle<WGPUTexture>;
using TextureViewHandle = GpuHandle<WGPUTextureView>;
using BufferHandle = GpuHandle<WGPUBuffer>;
using RenderPipelineHandle = GpuHandle<WGPURenderPipeline>;
using ComputePipelineHandle = GpuHandle<WGPUComputePipeline>;
using BindGroupHandle = GpuHandle<WGPUBindGroup>;
using BindGroupLayoutHandle = GpuHandle<WGPUBindGroupLayout>;
using SamplerHandle = GpuHandle<WGPUSampler>;
using ShaderModuleHandle = GpuHandle<WGPUShaderModule>;
using PipelineLayoutHandle = GpuHandle<WGPUPipelineLayout>;

} // namespace vivid
