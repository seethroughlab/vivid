#pragma once

// Shared WGSL snippets and helper functions for GPU operators.
// Operators concatenate these with their own fragment shaders.

#include "operator_api/gpu_operator.h"
#include <string>

namespace vivid::gpu {

// Fullscreen triangle vertex shader — covers the entire viewport with a single
// oversized triangle.  No vertex buffer needed; just draw 3 vertices.
// Provides `FullscreenOutput` with position and UV (top-left origin, Y-flipped).
inline constexpr const char* FULLSCREEN_VERTEX_WGSL = R"(
struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32, flipY: bool) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    if (flipY) {
        out.uv.y = 1.0 - out.uv.y;
    }
    return out;
}
)";

// Common WGSL math constants.
inline constexpr const char* WGSL_CONSTANTS = R"(
const PI:    f32 = 3.14159265358979323846;
const TAU:   f32 = 6.28318530717958647692;
const E:     f32 = 2.71828182845904523536;
const PHI:   f32 = 1.61803398874989484820;
const SQRT2: f32 = 1.41421356237309504880;
)";

// ---------------------------------------------------------------------------
// Helper: compile a fragment shader with the fullscreen vertex preamble
// ---------------------------------------------------------------------------
inline WGPUShaderModule create_shader(WGPUDevice device, const char* frag_src,
                                       const char* label) {
    std::string src = std::string(FULLSCREEN_VERTEX_WGSL) + frag_src;
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = vivid_sv(src.c_str());

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl_src.chain;
    desc.label = vivid_sv(label);
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// ---------------------------------------------------------------------------
// Helper: create a fullscreen render pipeline (vs_main + fs_main)
// ---------------------------------------------------------------------------
inline WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUShaderModule shader,
                                           WGPUPipelineLayout layout,
                                           WGPUTextureFormat format,
                                           const char* label) {
    WGPUColorTargetState color_target{};
    color_target.format = format;
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

// ---------------------------------------------------------------------------
// Helper: run a fullscreen render pass (clear + draw 3 vertices)
// ---------------------------------------------------------------------------
inline void run_pass(WGPUCommandEncoder encoder, WGPURenderPipeline pipeline,
                     WGPUBindGroup bind_group, WGPUTextureView target,
                     const char* label,
                     WGPUColor clear = WGPUColor{0, 0, 0, 1}) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = target;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = clear;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv(label);
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// ---------------------------------------------------------------------------
// Helper: create a Uniform | CopyDst buffer
// ---------------------------------------------------------------------------
inline WGPUBuffer create_uniform_buffer(WGPUDevice device, uint64_t size,
                                         const char* label) {
    WGPUBufferDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = size;
    desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(device, &desc);
}

// ---------------------------------------------------------------------------
// Helper: create a ClampToEdge / Linear sampler
// ---------------------------------------------------------------------------
inline WGPUSampler create_linear_sampler(WGPUDevice device, const char* label) {
    WGPUSamplerDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.addressModeU = WGPUAddressMode_ClampToEdge;
    desc.addressModeV = WGPUAddressMode_ClampToEdge;
    desc.addressModeW = WGPUAddressMode_ClampToEdge;
    desc.magFilter = WGPUFilterMode_Linear;
    desc.minFilter = WGPUFilterMode_Linear;
    desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    desc.maxAnisotropy = 1;
    return wgpuDeviceCreateSampler(device, &desc);
}

} // namespace vivid::gpu
