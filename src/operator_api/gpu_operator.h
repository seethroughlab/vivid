#pragma once
#include "operator_api/types.h"
#include <webgpu/webgpu.h>
#include <cstring>

namespace vivid::gpu { struct VividSceneFragment; }

struct VividGpuState {
    WGPUDevice         device;
    WGPUQueue          queue;
    WGPUCommandEncoder command_encoder;
    WGPUTexture        output_texture;
    WGPUTextureView    output_texture_view;
    uint32_t           output_width;
    uint32_t           output_height;
    WGPUTextureFormat  output_format;
    WGPUTextureView    output_depth_view = nullptr;  // Phase 6e: R32Float depth output

    // Texture inputs (one per GPU_TEXTURE input port, nullptr if disconnected)
    WGPUTextureView*   input_texture_views;
    uint32_t           input_texture_count;

    // Raw texture handles for inputs (parallel to input_texture_views).
    // Needed for copy/readback operations. May be nullptr if not resolved.
    WGPUTexture*       input_textures;
    uint32_t*          input_texture_widths;
    uint32_t*          input_texture_heights;

    // Path to operators/ source tree (for WGSL filter hot-reload)
    const char*        operators_src_dir;

    // Scene fragment I/O (3D operators)
    vivid::gpu::VividSceneFragment*  output_scene      = nullptr;  // operator sets during process()
    vivid::gpu::VividSceneFragment** input_scenes       = nullptr;  // resolved from upstream
    uint32_t                         input_scene_count  = 0;
};

static inline VividGpuState* vivid_gpu(const VividProcessContext* ctx) {
    return static_cast<VividGpuState*>(ctx->gpu);
}

static inline WGPUStringView vivid_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
}

namespace vivid::gpu {
    inline void release(WGPURenderPipeline& p) { if (p) { wgpuRenderPipelineRelease(p); p = nullptr; } }
    inline void release(WGPUBindGroupLayout& l) { if (l) { wgpuBindGroupLayoutRelease(l); l = nullptr; } }
    inline void release(WGPUBindGroup& g) { if (g) { wgpuBindGroupRelease(g); g = nullptr; } }
    inline void release(WGPUBuffer& b) { if (b) { wgpuBufferRelease(b); b = nullptr; } }
    inline void release(WGPUShaderModule& m) { if (m) { wgpuShaderModuleRelease(m); m = nullptr; } }
    inline void release(WGPUSampler& s) { if (s) { wgpuSamplerRelease(s); s = nullptr; } }
    inline void release(WGPUTexture& t) { if (t) { wgpuTextureRelease(t); t = nullptr; } }
    inline void release(WGPUTextureView& v) { if (v) { wgpuTextureViewRelease(v); v = nullptr; } }
    inline void release(WGPUComputePipeline& p) { if (p) { wgpuComputePipelineRelease(p); p = nullptr; } }
    inline void release(WGPUPipelineLayout& l) { if (l) { wgpuPipelineLayoutRelease(l); l = nullptr; } }

    // -----------------------------------------------------------------------
    // GpuHandle<T> — RAII wrapper for WebGPU resources (move-only)
    // -----------------------------------------------------------------------
    template<typename T, void(*ReleaseFn)(T)>
    class GpuHandle {
    public:
        GpuHandle() = default;
        explicit GpuHandle(T raw) : handle_(raw) {}
        ~GpuHandle() { reset(); }

        // Move only
        GpuHandle(GpuHandle&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
        GpuHandle& operator=(GpuHandle&& o) noexcept {
            if (this != &o) { reset(); handle_ = o.handle_; o.handle_ = nullptr; }
            return *this;
        }
        GpuHandle(const GpuHandle&) = delete;
        GpuHandle& operator=(const GpuHandle&) = delete;

        void reset(T raw = nullptr) {
            if (handle_) ReleaseFn(handle_);
            handle_ = raw;
        }

        T get() const { return handle_; }
        T operator*() const { return handle_; }
        explicit operator bool() const { return handle_ != nullptr; }

    private:
        T handle_ = nullptr;
    };

    // Type aliases for common GPU resources
    using PipelineHandle   = GpuHandle<WGPURenderPipeline,  wgpuRenderPipelineRelease>;
    using ShaderHandle     = GpuHandle<WGPUShaderModule,    wgpuShaderModuleRelease>;
    using BufferHandle     = GpuHandle<WGPUBuffer,          wgpuBufferRelease>;
    using BindGroupHandle  = GpuHandle<WGPUBindGroup,       wgpuBindGroupRelease>;
    using BindLayoutHandle = GpuHandle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
    using SamplerHandle    = GpuHandle<WGPUSampler,         wgpuSamplerRelease>;
    using TextureHandle    = GpuHandle<WGPUTexture,         wgpuTextureRelease>;
    using TexViewHandle    = GpuHandle<WGPUTextureView,     wgpuTextureViewRelease>;
    using PipeLayoutHandle = GpuHandle<WGPUPipelineLayout,  wgpuPipelineLayoutRelease>;
}
