#pragma once

#include "operator_api/types.h"
#include "operator_api/gpu_common.h"
#include <webgpu/webgpu.h>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VividThumbnailContext {
    double    time;
    double    delta_time;
    uint64_t  frame;

    const float*       param_values;
    uint32_t           param_count;
    const float*       output_values;
    uint32_t           output_count;
    const char* const* string_param_values;
    uint32_t           string_param_count;
    const char* const* file_param_values;
    uint32_t           file_param_count;

    WGPUDevice         device;
    WGPUQueue          queue;
    WGPUCommandEncoder command_encoder;

    WGPUTexture        thumbnail_texture;
    WGPUTextureView    thumbnail_texture_view;
    uint32_t           thumbnail_width;
    uint32_t           thumbnail_height;
    WGPUTextureFormat  thumbnail_format;

    WGPUTexture        source_output_texture;
    WGPUTextureView    source_output_texture_view;
    uint32_t           source_output_width;
    uint32_t           source_output_height;
    WGPUTextureFormat  source_output_format;

    WGPUTextureView*   input_texture_views;
    uint32_t           input_texture_count;

    uint32_t           thumbnail_logical_width;   // graph-space dimensions (e.g. 140x88)
    uint32_t           thumbnail_logical_height;  // use for draw API coordinates

    uint8_t            operator_errored;
    const char*        operator_error_msg;

    VividDrawAPI       draw;  // 2D draw API (optional — check draw.opaque before use)

    // Which input ports are actually connected (bit i = input port ordinal i has
    // an upstream wire), resolved by the host from the compiled graph's edge list.
    // This is the authoritative connectivity signal — the audio/frame contexts
    // intentionally do not carry it (disconnected ports still get zero buffers).
    // Bits beyond 32 ports are not represented; operators with more inputs should
    // treat that as a non-issue (no current operator exceeds 32 input ports).
    uint32_t           connected_input_mask;
} VividThumbnailContext;

#ifdef __cplusplus
}
#endif

static inline void vivid_report_thumbnail_error(const VividThumbnailContext* ctx,
                                                    const char* msg) {
    auto* mut = const_cast<VividThumbnailContext*>(ctx);
    mut->operator_errored = 1;
    mut->operator_error_msg = msg;
}

namespace vivid::thumbnail {

inline WGPUShaderModule create_shader(WGPUDevice device, const char* frag_src, const char* label) {
    return vivid::gpu::create_shader(device, frag_src, label);
}

inline WGPUBuffer create_uniform_buffer(WGPUDevice device, uint64_t size, const char* label) {
    return vivid::gpu::create_uniform_buffer(device, size, label);
}

inline WGPUSampler create_linear_sampler(WGPUDevice device, const char* label) {
    return vivid::gpu::create_linear_sampler(device, label);
}

inline WGPUBindGroupLayout create_uniform_bind_layout(WGPUDevice device,
                                                      uint64_t uniform_size,
                                                      const char* label) {
    WGPUBindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = uniform_size;

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.entryCount = 1;
    desc.entries = &entry;
    return wgpuDeviceCreateBindGroupLayout(device, &desc);
}

inline WGPUBindGroup create_uniform_bind_group(WGPUDevice device,
                                               WGPUBindGroupLayout layout,
                                               WGPUBuffer uniform_buf,
                                               uint64_t uniform_size,
                                               const char* label) {
    WGPUBindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = uniform_buf;
    entry.offset = 0;
    entry.size = uniform_size;

    WGPUBindGroupDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return wgpuDeviceCreateBindGroup(device, &desc);
}

inline WGPUPipelineLayout create_pipeline_layout(WGPUDevice device,
                                                 WGPUBindGroupLayout layout,
                                                 const char* label) {
    WGPUPipelineLayoutDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &layout;
    return wgpuDeviceCreatePipelineLayout(device, &desc);
}

inline WGPURenderPipeline create_pipeline(WGPUDevice device,
                                          WGPUShaderModule shader,
                                          WGPUPipelineLayout layout,
                                          WGPUTextureFormat format,
                                          const char* label) {
    WGPUBlendState blend{};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target{};
    color_target.format = format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = shader;
    fragment.entryPoint = vivid_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = vivid_sv(label);
    rp_desc.layout = layout;
    rp_desc.vertex.module = shader;
    rp_desc.vertex.entryPoint = vivid_sv("vs_main");
    rp_desc.vertex.bufferCount = 0;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;
    return wgpuDeviceCreateRenderPipeline(device, &rp_desc);
}

inline void run_pass(const VividThumbnailContext* ctx,
                     WGPURenderPipeline pipeline,
                     WGPUBindGroup bind_group,
                     const char* label,
                     WGPUColor clear = WGPUColor{0.0, 0.0, 0.0, 0.0}) {
    if (!ctx) {
        return;
    }
    if (ctx->operator_errored) {
        return;
    }
    if (!ctx->thumbnail_texture_view) {
        vivid_report_thumbnail_error(ctx, "thumbnail render skipped: null thumbnail target");
        return;
    }
    if (!pipeline || !bind_group) {
        vivid_report_thumbnail_error(ctx, "thumbnail render skipped: null pipeline or bind group");
        return;
    }

    WGPURenderPassColorAttachment color_att{};
    color_att.view = ctx->thumbnail_texture_view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = clear;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv(label);
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(ctx->command_encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

} // namespace vivid::thumbnail
