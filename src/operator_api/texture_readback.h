#pragma once

// GPU texture → CPU buffer readback utility.
// Reads a WGPUTextureView to a CPU float buffer at a target resolution.
// Double-buffered to avoid GPU stalls.

#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstring>
#include <vector>

namespace vivid::gpu {

class TextureReadback {
public:
    // Prepare readback resources. Call once with a valid device.
    void init(WGPUDevice device, uint32_t target_w, uint32_t target_h) {
        device_   = device;
        target_w_ = target_w;
        target_h_ = target_h;

        // Staging texture at target resolution (render target for downscale)
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Readback Staging");
        td.size          = { target_w, target_h, 1 };
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = WGPUTextureFormat_RGBA8Unorm;
        td.usage         = WGPUTextureUsage_RenderAttachment |
                           WGPUTextureUsage_CopySrc;
        staging_tex_ = wgpuDeviceCreateTexture(device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        staging_view_ = wgpuTextureCreateView(staging_tex_, &vd);

        // Readback buffers (double-buffered)
        uint32_t aligned_bpr = align_bpr(target_w * 4);
        uint64_t buf_size    = static_cast<uint64_t>(aligned_bpr) * target_h;

        WGPUBufferDescriptor bd{};
        bd.label = vivid_sv("Readback Buffer 0");
        bd.size  = buf_size;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        read_buf_[0] = wgpuDeviceCreateBuffer(device, &bd);

        bd.label = vivid_sv("Readback Buffer 1");
        read_buf_[1] = wgpuDeviceCreateBuffer(device, &bd);

        // Downscale shader + pipeline
        init_downscale_pipeline(device);

        // Allocate CPU result buffer (RGB float, 3 channels)
        cpu_data_.resize(static_cast<size_t>(target_w) * target_h * 3, 0.0f);
    }

    // Queue a readback. Call from process_gpu(). Results available next frame.
    void readback(WGPUCommandEncoder encoder, WGPUQueue queue,
                  WGPUTextureView input, uint32_t src_w, uint32_t src_h) {
        if (!staging_tex_ || !input) return;

        // Rebuild bind group with input texture
        vivid::gpu::release(bind_group_);
        WGPUBindGroupEntry entries[2]{};
        entries[0].binding = 0; entries[0].sampler     = sampler_;
        entries[1].binding = 1; entries[1].textureView = input;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label      = vivid_sv("Readback BG");
        bg_desc.layout     = bind_layout_;
        bg_desc.entryCount = 2;
        bg_desc.entries    = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);

        // Render pass: downscale input → staging texture
        vivid::gpu::run_pass(encoder, pipeline_, bind_group_,
                             staging_view_, "Readback Downscale");

        // Copy staging texture → readback buffer.
        // Skip if the write target is still busy (pending map or mapped).
        int write_idx = 1 - read_idx_;
        if (buf_busy_[write_idx]) return;
        uint32_t aligned_bpr = align_bpr(target_w_ * 4);

        WGPUTexelCopyTextureInfo src{};
        src.texture  = staging_tex_;
        src.mipLevel = 0;
        src.origin   = {0, 0, 0};
        src.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo dst{};
        dst.buffer             = read_buf_[write_idx];
        dst.layout.offset      = 0;
        dst.layout.bytesPerRow = aligned_bpr;
        dst.layout.rowsPerImage = target_h_;

        WGPUExtent3D extent = { target_w_, target_h_, 1 };
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

        // Map the previously written buffer for CPU read
        if (!mapping_pending_) {
            mapping_pending_ = true;
            map_target_idx_ = read_idx_;

            buf_busy_[read_idx_] = true;  // mark busy before async map

            WGPUBufferMapCallbackInfo map_cb{};
            map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
            map_cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView,
                                 void* ud1, void*) {
                auto* self = static_cast<TextureReadback*>(ud1);
                if (status == WGPUMapAsyncStatus_Success) {
                    self->on_map_complete(self->map_target_idx_);
                }
                // Buffer is unmapped in on_map_complete; mark not busy.
                self->buf_busy_[self->map_target_idx_] = false;
                self->mapping_pending_ = false;
            };
            map_cb.userdata1 = this;

            wgpuBufferMapAsync(read_buf_[map_target_idx_], WGPUMapMode_Read, 0,
                static_cast<uint64_t>(aligned_bpr) * target_h_, map_cb);
        }

        // Swap buffer index
        read_idx_ = write_idx;
    }

    const float* data() const { return ready_ ? cpu_data_.data() : nullptr; }
    bool ready() const { return ready_; }
    uint32_t width() const { return target_w_; }
    uint32_t height() const { return target_h_; }

    void release() {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(staging_tex_);
        vivid::gpu::release(staging_view_);
        for (int i = 0; i < 2; ++i) vivid::gpu::release(read_buf_[i]);
    }

    ~TextureReadback() { release(); }

    // Non-copyable, non-movable (GPU resources)
    TextureReadback() = default;
    TextureReadback(const TextureReadback&) = delete;
    TextureReadback& operator=(const TextureReadback&) = delete;

private:
    WGPUDevice device_ = nullptr;
    uint32_t target_w_ = 0, target_h_ = 0;

    // Staging texture (downscaled)
    WGPUTexture     staging_tex_  = nullptr;
    WGPUTextureView staging_view_ = nullptr;

    // Double-buffered readback
    WGPUBuffer read_buf_[2] = {nullptr, nullptr};
    int        read_idx_    = 0;
    int        map_target_idx_ = 0;
    bool       mapping_pending_ = false;
    bool       buf_busy_[2] = {false, false};  // true from mapAsync until unmap completes
    bool       ready_ = false;

    // Downscale pipeline
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;

    // CPU result
    std::vector<float> cpu_data_;

    static constexpr uint32_t align_bpr(uint32_t bpr) {
        return (bpr + 255) & ~255u;
    }

    void on_map_complete(int idx) {
        uint32_t aligned_bpr = align_bpr(target_w_ * 4);
        const void* mapped = wgpuBufferGetConstMappedRange(read_buf_[idx], 0,
            static_cast<uint64_t>(aligned_bpr) * target_h_);
        if (mapped) {
            const uint8_t* src = static_cast<const uint8_t*>(mapped);
            for (uint32_t y = 0; y < target_h_; ++y) {
                const uint8_t* row = src + y * aligned_bpr;
                for (uint32_t x = 0; x < target_w_; ++x) {
                    size_t dst_i = (static_cast<size_t>(y) * target_w_ + x) * 3;
                    cpu_data_[dst_i + 0] = row[x * 4 + 0] / 255.0f; // R
                    cpu_data_[dst_i + 1] = row[x * 4 + 1] / 255.0f; // G
                    cpu_data_[dst_i + 2] = row[x * 4 + 2] / 255.0f; // B
                }
            }
            ready_ = true;
        }
        wgpuBufferUnmap(read_buf_[idx]);
    }

    void init_downscale_pipeline(WGPUDevice device) {
        // Shader: sample input with bilinear filtering → RGBA8
        static const char* kDownscaleFragment = R"(
@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var inputTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> FullscreenOutput {
    return fullscreenTriangle(vertexIndex, true);
}

@fragment
fn fs_main(input: FullscreenOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    return vec4f(color.rgb, 1.0);
}
)";
        shader_ = vivid::gpu::create_shader(device, kDownscaleFragment, "Readback Downscale Shader");
        sampler_ = vivid::gpu::create_linear_sampler(device, "Readback Sampler");

        // Bind group layout: sampler(0) + texture(1)
        WGPUBindGroupLayoutEntry entries[2]{};
        entries[0].binding    = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[1].binding    = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType    = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("Readback BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries    = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label               = vivid_sv("Readback PL");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts     = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(device, shader_, pipe_layout_,
                                                 WGPUTextureFormat_RGBA8Unorm,
                                                 "Readback Pipeline");
    }
};

} // namespace vivid::gpu
