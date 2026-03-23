#pragma once
#include "operator_api/types.h"
#include "operator_api/gpu_types.h"
#include <webgpu/webgpu.h>
#include <cstring>

// ---------------------------------------------------------------------------
// VividGpuContext — typed context for GPU operators (main thread, ~60 Hz)
//
// Replaces the old VividGpuState + VividProcessContext pair.
// Contains both common per-tick fields and GPU-specific resources.
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

struct VividGpuContext {
    // ---- Common per-tick fields (same as VividProcessContext) ----------------
    double    time;
    double    delta_time;
    uint64_t  frame;
    float*    param_values;
    float*    input_values;
    float*    output_values;

    // ---- GPU-specific resources ---------------------------------------------
    WGPUDevice         device;
    WGPUQueue          queue;
    WGPUCommandEncoder command_encoder;
    WGPUTexture        output_texture;
    WGPUTextureView    output_texture_view;
    uint32_t           output_width;
    uint32_t           output_height;
    WGPUTextureFormat  output_format;
    // Auxiliary texture outputs (2nd, 3rd... GPU_TEXTURE output ports), scheduler-allocated.
    WGPUTextureView*   aux_output_texture_views;
    uint32_t           aux_output_texture_count;

    // Texture inputs (one per GPU_TEXTURE input port, nullptr if disconnected)
    WGPUTextureView*   input_texture_views;
    uint32_t           input_texture_count;

    // Raw texture handles for inputs (parallel to input_texture_views).
    WGPUTexture*       input_textures;
    uint32_t*          input_texture_widths;
    uint32_t*          input_texture_heights;

    // Path to operators/ source tree (for WGSL filter hot-reload)
    const char*        operators_src_dir;

    // Custom-transport I/O — opaque ports with VIVID_PORT_TRANSPORT_CUSTOM_VALUE/REF.
    // Operators cast from void* using their known type. Type safety is enforced at
    // wire validation time via type_name on VividPortDescriptor.
    void**    custom_outputs;
    uint32_t  custom_output_count;
    void**    custom_inputs;
    uint32_t  custom_input_count;

    // ---- Cross-domain inputs from control -----------------------------------
    VividSpreadPort*   input_spreads;
    VividSpreadPort*   output_spreads;
    const char**       input_string_values;
    const char**       output_string_values;
    VividStringSpreadPort* input_string_spreads;
    VividStringSpreadPort* output_string_spreads;
    const char**       file_param_values;
    uint32_t           file_param_count;

    // Input events
    const VividInputState* input;
    const VividSharedHandleService* shared_handles;

    // ---- Operator write-back ------------------------------------------------
    uint32_t           preferred_tex_width;
    uint32_t           preferred_tex_height;
    uint8_t            operator_errored;               // written by operator on shader/init failure
    const char*        operator_error_msg;             // must point to long-lived storage
};

#ifdef __cplusplus
}
#endif

// Request a texture resize for the next frame. The preferred_tex_* fields are
// write-back fields (operator → runtime), explicitly mutable through const ctx.
static inline void vivid_request_output_size(const VividGpuContext* ctx,
                                              uint32_t w, uint32_t h) {
    const_cast<VividGpuContext*>(ctx)->preferred_tex_width  = w;
    const_cast<VividGpuContext*>(ctx)->preferred_tex_height = h;
}

// Report a GPU shader/init error. msg must point to storage that outlives the
// process_gpu() call (e.g. a std::string member of the operator struct).
static inline void vivid_report_gpu_error(const VividGpuContext* ctx, const char* msg) {
    const_cast<VividGpuContext*>(ctx)->operator_errored   = 1;
    const_cast<VividGpuContext*>(ctx)->operator_error_msg = msg;
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
