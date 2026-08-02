#pragma once

// Shared WGSL snippets and helper functions for GPU operators.
// Operators concatenate these with their own fragment shaders.

#include "operator_api/gpu_operator.h"
#include <cstdio>
#include <string>
#include <vector>

namespace vivid::gpu {

// Fullscreen triangle vertex shader — covers the entire viewport with a single
// oversized triangle.  No vertex buffer needed; just draw 3 vertices.
// Provides `FullscreenOutput` with position and UV in the canonical image convention: uv (0,0) is the
// TOP-LEFT of both the sampled texture and the framebuffer, so a fullscreen pass that samples an input
// at `uv` is IDENTITY — it does not vertically flip. This makes orientation independent of how many
// op-passes a texture flows through (Switch/Composite/CRT/... no longer each invert Y). `flipY=true`
// opts into an explicit vertical flip for the rare op that wants one.
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
    // Framebuffer/clip-space y is +up, but texture uv.y is +down — invert so the top of the screen
    // maps to the top of the texture (uv.y = 0): an identity passthrough, not a flip.
    out.uv = vec2f(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
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

// Like create_shader(), but wraps compilation in an error scope.
// wgpu-native returns a non-null "error" handle on WGSL failure, not nullptr;
// the scope catches this before the invalid handle can reach pipeline creation.
// Populates error_out with the validation message on failure; leaves it unchanged on success.
inline WGPUShaderModule create_shader_checked(WGPUDevice device, const char* frag_src,
                                               const char* label,
                                               std::string& error_out) {
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    WGPUShaderModule sm = create_shader(device, frag_src, label);
    {
        WGPUPopErrorScopeCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowSpontaneous;
        cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                          WGPUStringView msg, void* ud1, void*) {
            if (type != WGPUErrorType_NoError) {
                auto* err = static_cast<std::string*>(ud1);
                *err = msg.data ? std::string(msg.data, msg.length) : "unknown WGSL error";
                std::fprintf(stderr, "[vivid] WGSL error: %s\n", err->c_str());
            }
        };
        cb.userdata1 = &error_out;
        wgpuDevicePopErrorScope(device, cb);
    }
    return sm;
}

// ---------------------------------------------------------------------------
// Helper: create a fullscreen render pipeline with N color targets (MRT).
// `formats[i]` is the WGPUTextureFormat of color attachment i; the fragment
// shader must emit one @location(i) output per target. count==1 is the common
// single-target case.
// ---------------------------------------------------------------------------
inline WGPURenderPipeline create_pipeline_mrt(WGPUDevice device, WGPUShaderModule shader,
                                              WGPUPipelineLayout layout,
                                              const WGPUTextureFormat* formats,
                                              uint32_t count,
                                              const char* label) {
    std::vector<WGPUColorTargetState> color_targets(count, WGPUColorTargetState{});
    for (uint32_t i = 0; i < count; ++i) {
        color_targets[i].format = formats[i];
        color_targets[i].writeMask = WGPUColorWriteMask_All;
    }

    WGPUFragmentState fragment{};
    fragment.module = shader;
    fragment.entryPoint = vivid_sv("fs_main");
    fragment.targetCount = count;
    fragment.targets = color_targets.data();

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
// Helper: create a fullscreen render pipeline (vs_main + fs_main), single target.
// ---------------------------------------------------------------------------
inline WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUShaderModule shader,
                                           WGPUPipelineLayout layout,
                                           WGPUTextureFormat format,
                                           const char* label) {
    return create_pipeline_mrt(device, shader, layout, &format, 1, label);
}

// ---------------------------------------------------------------------------
// Helper: run a fullscreen render pass with N color attachments (MRT).
// `targets[i]`/`clears[i]` describe color attachment i (all cleared then stored).
// All attachments must share size and sample count. count==1 is the common case.
// ---------------------------------------------------------------------------
inline void run_pass_mrt(WGPUCommandEncoder encoder, WGPURenderPipeline pipeline,
                         WGPUBindGroup bind_group,
                         const WGPUTextureView* targets, uint32_t count,
                         const WGPUColor* clears, const char* label) {
    if (count == 0 || !targets) return;
    for (uint32_t i = 0; i < count; ++i) if (!targets[i]) return;

    std::vector<WGPURenderPassColorAttachment> color_atts(count, WGPURenderPassColorAttachment{});
    for (uint32_t i = 0; i < count; ++i) {
        color_atts[i].view = targets[i];
        color_atts[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_atts[i].loadOp = WGPULoadOp_Clear;
        color_atts[i].storeOp = WGPUStoreOp_Store;
        color_atts[i].clearValue = clears ? clears[i] : WGPUColor{0, 0, 0, 1};
    }

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv(label);
    rp_desc.colorAttachmentCount = count;
    rp_desc.colorAttachments = color_atts.data();

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// ---------------------------------------------------------------------------
// Helper: run a fullscreen render pass (clear + draw 3 vertices), single target.
// ---------------------------------------------------------------------------
inline void run_pass(WGPUCommandEncoder encoder, WGPURenderPipeline pipeline,
                     WGPUBindGroup bind_group, WGPUTextureView target,
                     const char* label,
                     WGPUColor clear = WGPUColor{0, 0, 0, 1}) {
    run_pass_mrt(encoder, pipeline, bind_group, &target, 1, &clear, label);
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
//
// Use this (+ create_standard_bind_group below) for SIMPLE fragment-shader
// operators that fit the uniform + sampler + N-texture shape. Operators with
// compute passes, multi-pass pipelines, storage buffers, or non-standard
// binding orders (e.g. bloom's dual-texture pass, particles_2d's compute,
// fluid's multi-pass) should hand-roll their own layout — that is expected, not
// duplication.
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
// Helper: 1×1 placeholder texture for a disconnected texture input (audit
// 06-R2-F1). Returns the owned texture + a matching 2D view. The fill is
// transparent black by default, or opaque black (RGB=0, A=max) when
// `opaque_black` is set. Sized per format — RGBA8Unorm (4 B) and RGBA16Float
// (8 B, the operator output format) are the cases Vivid operators use.
// ---------------------------------------------------------------------------
struct FallbackTexture {
    WGPUTexture     texture = nullptr;
    WGPUTextureView view    = nullptr;
};

inline FallbackTexture create_fallback_texture(
    WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat format, bool opaque_black = false) {

    WGPUTextureDescriptor td{};
    td.label         = vivid_sv("Fallback Texture");
    td.size          = { 1, 1, 1 };
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    td.dimension     = WGPUTextureDimension_2D;
    td.format        = format;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture tex  = wgpuDeviceCreateTexture(device, &td);
    WGPUTextureView view = create_texture_view(tex, format);

    // One pixel, sized + filled per format.
    uint8_t px[8] = {};
    uint32_t bytes_per_pixel = 8;  // RGBA16Float default
    if (format == WGPUTextureFormat_RGBA8Unorm) {
        bytes_per_pixel = 4;
        if (opaque_black) px[3] = 255;            // {0,0,0,255}
    } else if (opaque_black) {
        px[6] = 0x00; px[7] = 0x3C;               // RGBA16Float alpha = half-float 1.0 (0x3C00, LE)
    }

    WGPUTexelCopyTextureInfo dst{};
    dst.texture  = tex;
    dst.mipLevel = 0;
    dst.origin   = { 0, 0, 0 };
    dst.aspect   = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = bytes_per_pixel;
    layout.rowsPerImage = 1;
    WGPUExtent3D extent = { 1, 1, 1 };
    wgpuQueueWriteTexture(queue, &dst, px, bytes_per_pixel, &layout, &extent);

    return { tex, view };
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

// ---------------------------------------------------------------------------
// Compute shader helpers
// ---------------------------------------------------------------------------

// Compile a WGSL compute shader (no fullscreen vertex preamble).
inline WGPUShaderModule create_compute_shader(WGPUDevice device, const char* src,
                                               const char* label) {
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = vivid_sv(src);

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl_src.chain;
    desc.label = vivid_sv(label);
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// Create a compute pipeline.
inline WGPUComputePipeline create_compute_pipeline(WGPUDevice device,
                                                     WGPUShaderModule shader,
                                                     WGPUPipelineLayout layout,
                                                     const char* label) {
    WGPUComputePipelineDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.layout = layout;
    desc.compute.module = shader;
    desc.compute.entryPoint = vivid_sv("cs_main");
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

// Create a Storage | CopyDst | CopySrc buffer.
inline WGPUBuffer create_storage_buffer(WGPUDevice device, uint64_t size,
                                         const char* label) {
    WGPUBufferDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = size;
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    return wgpuDeviceCreateBuffer(device, &desc);
}

// Create a MapRead | CopyDst buffer (for GPU → CPU readback).
inline WGPUBuffer create_readback_buffer(WGPUDevice device, uint64_t size,
                                          const char* label) {
    WGPUBufferDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = size;
    desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(device, &desc);
}

// Record a compute dispatch into a command encoder.
inline void dispatch_compute(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline,
                              WGPUBindGroup bind_group, uint32_t workgroups_x,
                              const char* label) {
    WGPUComputePassDescriptor cp_desc{};
    cp_desc.label = vivid_sv(label);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &cp_desc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups_x, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

} // namespace vivid::gpu
