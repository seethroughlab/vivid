#pragma once

// Shared WGSL snippets and helper functions for GPU operators.
// Operators concatenate these with their own fragment shaders.

#include "operator_api/gpu_operator.h"
#include <string>
#include <vector>

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
    if (!target) return;
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

// ---------------------------------------------------------------------------
// Helper: create a standard bind group layout
//   uniform(0) + sampler(1) + N float textures(2..N+1)
//   Uniform is visible to Fragment by default; pass extra_uniform_visibility
//   for additional stages (e.g. WGPUShaderStage_Vertex).
// ---------------------------------------------------------------------------
inline WGPUBindGroupLayout create_standard_bind_layout(
    WGPUDevice device, uint32_t texture_count, const char* label,
    uint64_t uniform_min_size = 0,
    WGPUShaderStage extra_uniform_visibility = WGPUShaderStage_None) {

    uint32_t entry_count = 2 + texture_count;
    std::vector<WGPUBindGroupLayoutEntry> entries(entry_count, WGPUBindGroupLayoutEntry{});

    entries[0].binding    = 0;
    entries[0].visibility = WGPUShaderStage_Fragment | extra_uniform_visibility;
    entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = uniform_min_size;

    entries[1].binding    = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    for (uint32_t i = 0; i < texture_count; ++i) {
        entries[2 + i].binding    = 2 + i;
        entries[2 + i].visibility = WGPUShaderStage_Fragment;
        entries[2 + i].texture.sampleType    = WGPUTextureSampleType_Float;
        entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[2 + i].texture.multisampled  = false;
    }

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label      = vivid_sv(label);
    bgl_desc.entryCount = entry_count;
    bgl_desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);
}

// ---------------------------------------------------------------------------
// Helper: create a standard bind group
//   uniform(0) + sampler(1) + N texture views(2..N+1)
// ---------------------------------------------------------------------------
inline WGPUBindGroup create_standard_bind_group(
    WGPUDevice device, WGPUBindGroupLayout layout,
    WGPUBuffer uniform_buf, uint64_t uniform_size,
    WGPUSampler sampler,
    const WGPUTextureView* texture_views, uint32_t texture_count,
    const char* label) {

    uint32_t entry_count = 2 + texture_count;
    std::vector<WGPUBindGroupEntry> entries(entry_count, WGPUBindGroupEntry{});

    entries[0].binding = 0;
    entries[0].buffer  = uniform_buf;
    entries[0].size    = uniform_size;

    entries[1].binding = 1;
    entries[1].sampler = sampler;

    for (uint32_t i = 0; i < texture_count; ++i) {
        entries[2 + i].binding     = 2 + i;
        entries[2 + i].textureView = texture_views[i];
    }

    WGPUBindGroupDescriptor desc{};
    desc.label      = vivid_sv(label);
    desc.layout     = layout;
    desc.entryCount = entry_count;
    desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroup(device, &desc);
}

// ---------------------------------------------------------------------------
// Helper: create a 2D simulation texture
//   Default format RGBA16Float, default usage RenderAttachment | TextureBinding.
// ---------------------------------------------------------------------------
inline WGPUTexture create_state_texture(
    WGPUDevice device, uint32_t width, uint32_t height,
    const char* label,
    WGPUTextureFormat format = WGPUTextureFormat_RGBA16Float,
    WGPUTextureUsage extra_usage = static_cast<WGPUTextureUsage>(0)) {

    WGPUTextureDescriptor td{};
    td.label         = vivid_sv(label);
    td.size          = { width, height, 1 };
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    td.dimension     = WGPUTextureDimension_2D;
    td.format        = format;
    td.usage         = WGPUTextureUsage_RenderAttachment |
                       WGPUTextureUsage_TextureBinding | extra_usage;
    return wgpuDeviceCreateTexture(device, &td);
}

// ---------------------------------------------------------------------------
// Helper: create a 2D texture view matching a texture's format
// ---------------------------------------------------------------------------
inline WGPUTextureView create_texture_view(
    WGPUTexture texture, WGPUTextureFormat format,
    const char* label = nullptr) {

    WGPUTextureViewDescriptor vd{};
    if (label) vd.label = vivid_sv(label);
    vd.format          = format;
    vd.dimension       = WGPUTextureViewDimension_2D;
    vd.mipLevelCount   = 1;
    vd.arrayLayerCount = 1;
    vd.aspect          = WGPUTextureAspect_All;
    return wgpuTextureCreateView(texture, &vd);
}

// ---------------------------------------------------------------------------
// Helper: PCG-style deterministic hash → float in [0, 1]
// ---------------------------------------------------------------------------
inline float hash_float(uint32_t input) {
    uint32_t state = input * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    uint32_t result = (word >> 22u) ^ word;
    return static_cast<float>(result) / 4294967295.0f;
}

// ---------------------------------------------------------------------------
// Helper: IEEE 754 float32 → float16 conversion
// ---------------------------------------------------------------------------
inline uint16_t float_to_half(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = bits & 0x7FFFFF;

    if (exp > 15) return static_cast<uint16_t>(sign | 0x7C00);
    if (exp < -14) return static_cast<uint16_t>(sign);

    return static_cast<uint16_t>(sign | ((exp + 15) << 10) | (mantissa >> 13));
}

} // namespace vivid::gpu
