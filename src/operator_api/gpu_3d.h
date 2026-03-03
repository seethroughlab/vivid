#pragma once

// 3D rendering helpers for WebGPU — depth buffers, vertex buffers,
// 3D pipelines, render passes, and WebGPU-correct projection matrices.
// Header-only, follows conventions of gpu_common.h.

#include "operator_api/gpu_common.h"
#include "linmath.h"
#include <cmath>
#include <cstdint>

namespace vivid::gpu {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Depth32Float: guaranteed 32-bit precision, predictable for later
// depth-buffer sampling (SSAO/DoF), no unwanted stencil bits.
inline constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth32Float;

// Pipeline variant feature flags (Phase 6a — pipeline cache key)
enum PipelineFeatureFlags : uint32_t {
    kPipelineDefault       = 0,
    kPipelineInstanced     = 1 << 0,
    kPipelineBillboard     = 1 << 1,
    kPipelineTextured      = 1 << 2,  // Phase 6c
    kPipelineShadowCaster  = 1 << 3,  // Phase 6d
    kPipelineIBL           = 1 << 4,  // Phase 6f
};

// ---------------------------------------------------------------------------
// Depth buffer helpers
// ---------------------------------------------------------------------------

inline WGPUTexture create_depth_texture(WGPUDevice device, uint32_t width,
                                         uint32_t height, const char* label) {
    WGPUTextureDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = { width, height, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = kDepthFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    return wgpuDeviceCreateTexture(device, &desc);
}

inline WGPUTextureView create_depth_view(WGPUTexture depth_texture,
                                          const char* label) {
    WGPUTextureViewDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.format = kDepthFormat;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_DepthOnly;
    return wgpuTextureCreateView(depth_texture, &desc);
}

// Shadow map depth texture: same as depth texture but adds TextureBinding for
// sampling in fragment shader during the main pass.
inline WGPUTexture create_shadow_map_texture(WGPUDevice device, uint32_t width,
                                              uint32_t height, const char* label) {
    WGPUTextureDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = { width, height, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = kDepthFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    return wgpuDeviceCreateTexture(device, &desc);
}

// ---------------------------------------------------------------------------
// Vertex / index buffer helpers
// ---------------------------------------------------------------------------

// Create a GPU vertex buffer. data may be nullptr for uninitialized.
inline WGPUBuffer create_vertex_buffer(WGPUDevice device, WGPUQueue queue,
                                        const void* data, uint64_t size,
                                        const char* label) {
    WGPUBufferDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = size;
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    desc.mappedAtCreation = false;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(device, &desc);
    if (data && buf) {
        wgpuQueueWriteBuffer(queue, buf, 0, data, size);
    }
    return buf;
}

// Create a GPU index buffer (uint32). data may be nullptr for uninitialized.
inline WGPUBuffer create_index_buffer(WGPUDevice device, WGPUQueue queue,
                                       const uint32_t* data, uint64_t count,
                                       const char* label) {
    uint64_t size = count * sizeof(uint32_t);
    WGPUBufferDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = size;
    desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    desc.mappedAtCreation = false;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(device, &desc);
    if (data && buf) {
        wgpuQueueWriteBuffer(queue, buf, 0, data, size);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Standard vertex format (48 bytes stride)
// ---------------------------------------------------------------------------

struct Vertex3D {
    float position[3];
    float normal[3];
    float tangent[4];   // xyz=tangent direction, w=handedness (±1 for bitangent)
    float uv[2];
};

// Returns a WGPUVertexBufferLayout describing Vertex3D.
// The returned layout references a static attributes array (program-lifetime).
inline WGPUVertexBufferLayout vertex3d_layout() {
    static WGPUVertexAttribute attrs[] = {
        { WGPUVertexFormat_Float32x3, 0,                           0 }, // position
        { WGPUVertexFormat_Float32x3, 3 * sizeof(float),           1 }, // normal
        { WGPUVertexFormat_Float32x4, 6 * sizeof(float),           2 }, // tangent
        { WGPUVertexFormat_Float32x2, 10 * sizeof(float),          3 }, // uv
    };
    WGPUVertexBufferLayout layout{};
    layout.arrayStride = sizeof(Vertex3D);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = 4;
    layout.attributes = attrs;
    return layout;
}

// ---------------------------------------------------------------------------
// 3D render pipeline
// ---------------------------------------------------------------------------

struct Pipeline3DDesc {
    WGPUShaderModule      shader;
    WGPUPipelineLayout    layout;
    WGPUTextureFormat     color_format;
    const WGPUVertexBufferLayout* vertex_layouts;
    uint32_t              vertex_layout_count;
    WGPUCullMode          cull_mode     = WGPUCullMode_Back;
    WGPUFrontFace         front_face    = WGPUFrontFace_CCW;
    WGPUPrimitiveTopology topology      = WGPUPrimitiveTopology_TriangleList;
    bool                  depth_write   = true;
    WGPUCompareFunction   depth_compare = WGPUCompareFunction_Less;
    const WGPUBlendState* blend         = nullptr; // nullptr = opaque
    const char*           vs_entry      = "vs_main";
    const char*           fs_entry      = "fs_main";
    const char*           label         = "3D Pipeline";
};

inline WGPURenderPipeline create_3d_pipeline(WGPUDevice device,
                                              const Pipeline3DDesc& desc) {
    // Color target
    WGPUColorTargetState color_target{};
    color_target.format = desc.color_format;
    color_target.writeMask = WGPUColorWriteMask_All;
    color_target.blend = desc.blend;

    // Fragment state
    WGPUFragmentState fragment{};
    fragment.module = desc.shader;
    fragment.entryPoint = vivid_sv(desc.fs_entry);
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    // Depth stencil — always enabled for 3D pipelines.
    // Stencil ops explicitly set to Keep/Always to avoid Undefined=0 trap.
    WGPUDepthStencilState depth_stencil{};
    depth_stencil.format = kDepthFormat;
    depth_stencil.depthWriteEnabled = desc.depth_write ? WGPUOptionalBool_True
                                                       : WGPUOptionalBool_False;
    depth_stencil.depthCompare = desc.depth_compare;
    depth_stencil.stencilFront.compare     = WGPUCompareFunction_Always;
    depth_stencil.stencilFront.failOp      = WGPUStencilOperation_Keep;
    depth_stencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depth_stencil.stencilFront.passOp      = WGPUStencilOperation_Keep;
    depth_stencil.stencilBack              = depth_stencil.stencilFront;
    depth_stencil.stencilReadMask          = 0xFF;
    depth_stencil.stencilWriteMask         = 0xFF;

    // Pipeline descriptor
    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = vivid_sv(desc.label);
    rp_desc.layout = desc.layout;
    rp_desc.vertex.module = desc.shader;
    rp_desc.vertex.entryPoint = vivid_sv(desc.vs_entry);
    rp_desc.vertex.bufferCount = desc.vertex_layout_count;
    rp_desc.vertex.buffers = desc.vertex_layouts;
    rp_desc.primitive.topology = desc.topology;
    rp_desc.primitive.frontFace = desc.front_face;
    rp_desc.primitive.cullMode = desc.cull_mode;
    rp_desc.depthStencil = &depth_stencil;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;

    return wgpuDeviceCreateRenderPipeline(device, &rp_desc);
}

// ---------------------------------------------------------------------------
// 3D render pass
// ---------------------------------------------------------------------------

// Begin a 3D render pass with color + depth attachments.
// Returns the pass encoder for multi-draw scenarios.
inline WGPURenderPassEncoder begin_3d_pass(
        WGPUCommandEncoder encoder,
        WGPUTextureView color_target,
        WGPUTextureView depth_target,
        const char* label,
        WGPUColor clear_color = {0, 0, 0, 1},
        float clear_depth = 1.0f) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = color_target;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = clear_color;

    // Depth32Float has no stencil — stencil load/store must stay Undefined (0).
    // Zero-init the struct and only set depth fields.
    WGPURenderPassDepthStencilAttachment depth_att{};
    depth_att.view = depth_target;
    depth_att.depthLoadOp = WGPULoadOp_Clear;
    depth_att.depthStoreOp = WGPUStoreOp_Store;
    depth_att.depthClearValue = clear_depth;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv(label);
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;
    rp_desc.depthStencilAttachment = &depth_att;

    return wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
}

// Complete single-draw pass: begin, bind, draw indexed, end.
inline void run_3d_pass(WGPUCommandEncoder encoder,
                         WGPURenderPipeline pipeline,
                         WGPUBindGroup bind_group,
                         WGPUBuffer vertex_buffer, uint64_t vertex_buf_size,
                         WGPUBuffer index_buffer, uint32_t index_count,
                         WGPUTextureView color_target,
                         WGPUTextureView depth_target,
                         const char* label,
                         WGPUColor clear_color = {0, 0, 0, 1}) {
    WGPURenderPassEncoder pass = begin_3d_pass(
        encoder, color_target, depth_target, label, clear_color);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer, 0, vertex_buf_size);
    wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer,
                                         WGPUIndexFormat_Uint32, 0,
                                         static_cast<uint64_t>(index_count) * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass, index_count, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// ---------------------------------------------------------------------------
// Projection matrices — WebGPU NDC: Z in [0, 1]
// ---------------------------------------------------------------------------

// Perspective projection with WebGPU Z mapping (near→0, far→1).
// y_fov is in radians.
inline void perspective_wgpu(mat4x4 m, float y_fov, float aspect,
                              float near, float far) {
    float const a = 1.f / std::tan(y_fov / 2.f);

    m[0][0] = a / aspect;
    m[0][1] = 0.f;
    m[0][2] = 0.f;
    m[0][3] = 0.f;

    m[1][0] = 0.f;
    m[1][1] = a;
    m[1][2] = 0.f;
    m[1][3] = 0.f;

    m[2][0] = 0.f;
    m[2][1] = 0.f;
    m[2][2] = far / (near - far);
    m[2][3] = -1.f;

    m[3][0] = 0.f;
    m[3][1] = 0.f;
    m[3][2] = (near * far) / (near - far);
    m[3][3] = 0.f;
}

// Orthographic projection with WebGPU Z mapping (near→0, far→1).
inline void ortho_wgpu(mat4x4 m, float left, float right,
                        float bottom, float top,
                        float near, float far) {
    m[0][0] = 2.f / (right - left);
    m[0][1] = 0.f;
    m[0][2] = 0.f;
    m[0][3] = 0.f;

    m[1][0] = 0.f;
    m[1][1] = 2.f / (top - bottom);
    m[1][2] = 0.f;
    m[1][3] = 0.f;

    m[2][0] = 0.f;
    m[2][1] = 0.f;
    m[2][2] = 1.f / (near - far);
    m[2][3] = 0.f;

    m[3][0] = -(right + left) / (right - left);
    m[3][1] = -(top + bottom) / (top - bottom);
    m[3][2] = near / (near - far);
    m[3][3] = 1.f;
}

// ---------------------------------------------------------------------------
// Normal matrix: transpose(inverse(model))
// ---------------------------------------------------------------------------

// Computes the normal matrix for correct normal transformation under
// non-uniform scale. Cleans 4th row/col to identity for WGSL mat4x4f.
inline void normal_matrix(mat4x4 out, mat4x4 const model) {
    mat4x4 inv;
    mat4x4_invert(inv, model);
    mat4x4_transpose(out, inv);
    // Clean up 4th row/col to identity
    out[0][3] = 0.f; out[1][3] = 0.f; out[2][3] = 0.f;
    out[3][0] = 0.f; out[3][1] = 0.f; out[3][2] = 0.f;
    out[3][3] = 1.f;
}

// ---------------------------------------------------------------------------
// Scene fragment — data type that flows through 3D wires
// ---------------------------------------------------------------------------

struct VividSceneFragment {
    // Geometry (Vertex3D format)
    WGPUBuffer vertex_buffer   = nullptr;
    uint64_t   vertex_buf_size = 0;
    WGPUBuffer index_buffer    = nullptr;
    uint32_t   index_count     = 0;

    // Transform
    mat4x4     model_matrix;    // world-space transform

    // Material
    float      color[4] = {1,1,1,1};
    float      roughness  = 0.5f;   // 0=mirror, 1=matte
    float      metallic   = 0.0f;   // 0=dielectric, 1=metal
    float      emission   = 0.0f;   // additive emission multiplier
    bool       unlit      = false;  // skip lighting, output color directly

    // Pipeline override (nullptr = Render3D's default)
    WGPURenderPipeline pipeline       = nullptr;
    WGPUBindGroup      material_binds = nullptr;

    // Composition (Phase 3+: SceneMerge)
    VividSceneFragment** children    = nullptr;
    uint32_t             child_count = 0;

    bool depth_write = true;

    // Light data (Phase 3: Light3D operator)
    // When fragment_type == LIGHT, this fragment carries light info instead of geometry.
    enum FragmentType : uint32_t { GEOMETRY = 0, LIGHT = 1, SDF = 2, ENVIRONMENT = 3 };
    FragmentType fragment_type   = GEOMETRY;
    float light_type             = 0.0f;   // 0=directional, 1=point
    float light_color[3]         = {1,1,1};
    float light_intensity        = 1.0f;
    float light_radius           = 10.0f;  // attenuation radius (point lights)

    // Instancing (Phase 4)
    WGPUBuffer  instance_buffer   = nullptr;  // storage buffer, per-instance data
    uint32_t    instance_count     = 0;        // 0 = not instanced
    bool        billboard          = false;    // true = camera-facing billboard instancing (particles)
    bool        cast_shadow        = true;     // Phase 6d: false for particles/billboards

    // Custom camera UBO (Phase 5b: SDF3D — Render3D writes camera data here for custom pipelines)
    WGPUBuffer custom_camera_ubo = nullptr;

    // CPU vertex cache (Phase 4: allows Deformer to read source geometry)
    const Vertex3D* cpu_vertices     = nullptr;  // non-owning ptr to CPU vertex data
    uint32_t        cpu_vertex_count = 0;

    // CPU index cache (Phase 4: allows Boolean3D to read source topology)
    const uint32_t* cpu_indices      = nullptr;  // non-owning ptr to CPU index data
    uint32_t        cpu_index_count  = 0;

    // Pipeline variant selection (Phase 6a)
    uint32_t pipeline_flags  = 0;         // PipelineFeatureFlags bitfield
    float    shading_mode    = 0.0f;      // 0=default, 1=toon (Phase 6b)
    float    toon_levels     = 4.0f;      // toon quantization bands (Phase 6b)

    // PBR texture bind group (Phase 6c: sampler + albedo/normal/roughness_metallic/emission)
    WGPUBindGroup material_texture_binds = nullptr;

    // IBL environment data (Phase 6f: FragmentType::ENVIRONMENT)
    WGPUTextureView ibl_irradiance    = nullptr;  // irradiance cubemap (Cube dim)
    WGPUTextureView ibl_prefiltered   = nullptr;  // pre-filtered specular cubemap (Cube dim)
    WGPUTextureView ibl_brdf_lut      = nullptr;  // BRDF LUT (2D, RG16Float)
    WGPUSampler     ibl_sampler       = nullptr;  // linear, clamp-to-edge
    float           ibl_intensity     = 1.0f;     // environment intensity multiplier
};

struct InstanceData3D {
    float position[3];  // world-space offset
    float scale;        // uniform scale
    float color[4];     // per-instance RGBA
};
static_assert(sizeof(InstanceData3D) == 32, "InstanceData3D must be 32 bytes");

struct CustomCamera3D {
    float inverse_vp[16];   // mat4x4: screen UV → world ray
    float vp[16];           // mat4x4: world pos → clip (for frag_depth)
    float camera_pos[3];
    float near_plane;
    float far_plane;
    float _pad;
    float resolution[2];   // output width, height
};
static_assert(sizeof(CustomCamera3D) == 160, "CustomCamera3D must be 160 bytes");

inline void scene_fragment_identity(VividSceneFragment& f) {
    mat4x4_identity(f.model_matrix);
}

// ---------------------------------------------------------------------------
// PBR texture bind group layout (Phase 6c)
// ---------------------------------------------------------------------------

// Shared bind group layout: sampler + 4 textures (albedo, normal, roughness/metallic, emission)
inline WGPUBindGroupLayout create_pbr_texture_bind_layout(WGPUDevice device) {
    WGPUBindGroupLayoutEntry entries[5]{};

    // Binding 0: sampler
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    // Bindings 1-4: texture_2d<f32>
    for (int i = 1; i <= 4; ++i) {
        entries[i].binding = static_cast<uint32_t>(i);
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = vivid_sv("PBR Texture BGL");
    desc.entryCount = 5;
    desc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &desc);
}

// Repeat-wrap, linear-filter sampler for PBR textures
inline WGPUSampler create_repeat_sampler(WGPUDevice device, const char* label) {
    WGPUSamplerDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.addressModeU = WGPUAddressMode_Repeat;
    desc.addressModeV = WGPUAddressMode_Repeat;
    desc.addressModeW = WGPUAddressMode_Repeat;
    desc.magFilter    = WGPUFilterMode_Linear;
    desc.minFilter    = WGPUFilterMode_Linear;
    desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    desc.maxAnisotropy = 1;
    return wgpuDeviceCreateSampler(device, &desc);
}

// ---------------------------------------------------------------------------
// WGSL Cook-Torrance BRDF functions (Phase 6c)
// ---------------------------------------------------------------------------

inline constexpr const char* PBR_BRDF_WGSL = R"(
const PI: f32 = 3.14159265358979323846;

fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a  = roughness * roughness;
    let a2 = a * a;
    let d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    let g1 = NdotV / (NdotV * (1.0 - k) + k);
    let g2 = NdotL / (NdotL * (1.0 - k) + k);
    return g1 * g2;
}

fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}
)";

// ---------------------------------------------------------------------------
// WGSL preamble for 3D vertex shaders
// ---------------------------------------------------------------------------

inline constexpr const char* VERTEX_3D_WGSL = R"(
struct Vertex3DOutput {
    @builtin(position) position: vec4f,
    @location(0) world_pos: vec3f,
    @location(1) normal:    vec3f,
    @location(2) tangent:   vec4f,
    @location(3) uv:        vec2f,
}

struct Camera3D {
    mvp:            mat4x4f,
    model:          mat4x4f,
    normal_matrix:  mat4x4f,
    camera_pos:     vec3f,
    _pad:           f32,
}

fn transform3d(camera: Camera3D, pos: vec3f, normal: vec3f, tangent: vec4f, uv: vec2f) -> Vertex3DOutput {
    var out: Vertex3DOutput;
    let world = camera.model * vec4f(pos, 1.0);
    out.position  = camera.mvp * vec4f(pos, 1.0);
    out.world_pos = world.xyz;
    out.normal    = normalize((camera.normal_matrix * vec4f(normal, 0.0)).xyz);
    out.tangent   = vec4f(normalize((camera.normal_matrix * vec4f(tangent.xyz, 0.0)).xyz), tangent.w);
    out.uv        = uv;
    return out;
}
)";

// ---------------------------------------------------------------------------
// WGSL preamble for lighting (multi-light Blinn-Phong)
// ---------------------------------------------------------------------------

inline constexpr const char* LIGHTS_3D_WGSL = R"(
struct Light {
    position_and_type: vec4f,          // xyz=position/direction, w=type (0=dir, 1=point)
    direction_and_intensity: vec4f,    // xyz=direction, w=intensity
    color_and_radius: vec4f,           // xyz=color, w=radius
}

struct LightsUniform {
    lights: array<Light, 4>,           // 48*4 = 192 bytes
    light_count: u32,                  // active count (0-4)
    ambient_r: f32,
    ambient_g: f32,
    ambient_b: f32,
}
)";

// ---------------------------------------------------------------------------
// WGSL preamble for shadow mapping (Phase 6d)
// ---------------------------------------------------------------------------

inline constexpr const char* SHADOW_3D_WGSL = R"(
struct ShadowData {
    light_vp: array<mat4x4f, 4>,
    shadow_bias: f32,
    shadow_count_dir: u32,
    _pad0: f32,
    _pad1: f32,
}

fn vogel_disk_offset(index: u32, count: u32) -> vec2f {
    let golden_angle = 2.399963229728653;
    let r = sqrt(f32(index) + 0.5) / sqrt(f32(count));
    let theta = f32(index) * golden_angle;
    return vec2f(cos(theta), sin(theta)) * r;
}

fn sample_shadow_dir(world_pos: vec3f, light_idx: u32, shadow: ShadowData,
                     shadow_map: texture_depth_2d, shadow_samp: sampler_comparison) -> f32 {
    let light_clip = shadow.light_vp[light_idx] * vec4f(world_pos, 1.0);
    let ndc = light_clip.xyz / light_clip.w;

    // Out of shadow map bounds → not in shadow
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    let uv = vec2f(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    let depth = ndc.z - shadow.shadow_bias;
    let dims = textureDimensions(shadow_map);
    let px = vec2i(vec2f(f32(dims.x), f32(dims.y)) * uv);

    // 5-sample Vogel disk PCF via textureLoad + manual comparison
    // (workaround: textureSampleCompare returns incorrect results on
    //  some wgpu-native / Metal backends)
    var accum: f32 = 0.0;
    let sample_count: u32 = 5u;
    for (var s: u32 = 0u; s < sample_count; s++) {
        let offset = vec2i(vogel_disk_offset(s, sample_count) * 1.5);
        let sp = clamp(px + offset, vec2i(0), vec2i(dims) - vec2i(1));
        let stored = textureLoad(shadow_map, sp, 0);
        accum += select(0.0, 1.0, depth < stored);
    }
    return accum / f32(sample_count);
}
)";

// ---------------------------------------------------------------------------
// WGSL preamble for custom camera (SDF3D and other custom pipeline operators)
// ---------------------------------------------------------------------------

inline constexpr const char* CUSTOM_CAMERA_3D_WGSL = R"(
struct CustomCamera3D {
    inverse_vp: mat4x4f,
    vp:         mat4x4f,
    camera_pos: vec3f,
    near_plane: f32,
    far_plane:  f32,
    _pad:       f32,
    resolution: vec2f,
}
)";

// ---------------------------------------------------------------------------
// Raw WGSL shader helper (no fullscreen vertex preamble — for compute/3D)
// ---------------------------------------------------------------------------

inline WGPUShaderModule create_wgsl_shader(WGPUDevice device, const char* src, const char* label) {
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = vivid_sv(src);
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl_src.chain;
    desc.label = vivid_sv(label);
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// ---------------------------------------------------------------------------
// Cubemap texture helpers (Phase 6f)
// ---------------------------------------------------------------------------

// Create a 2D-array texture with 6 layers (suitable for cube view).
inline WGPUTexture create_cubemap_texture(WGPUDevice device, uint32_t face_size,
                                           uint32_t mip_levels, WGPUTextureFormat format,
                                           const char* label) {
    WGPUTextureDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.size = { face_size, face_size, 6 };
    desc.mipLevelCount = mip_levels;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
               | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return wgpuDeviceCreateTexture(device, &desc);
}

// Create a Cube view over all 6 layers + all mip levels.
inline WGPUTextureView create_cubemap_view(WGPUTexture tex, WGPUTextureFormat format,
                                            uint32_t mip_levels, const char* label) {
    WGPUTextureViewDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_Cube;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = mip_levels;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = 6;
    return wgpuTextureCreateView(tex, &desc);
}

// Create a 2D view of a single face+mip (for rendering into).
inline WGPUTextureView create_cubemap_face_view(WGPUTexture tex, WGPUTextureFormat format,
                                                 uint32_t face, uint32_t mip,
                                                 const char* label) {
    WGPUTextureViewDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = mip;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = face;
    desc.arrayLayerCount = 1;
    return wgpuTextureCreateView(tex, &desc);
}

// Clamp-to-edge, linear-filter sampler (for IBL cubemaps / BRDF LUT).
inline WGPUSampler create_clamp_linear_sampler(WGPUDevice device, const char* label) {
    WGPUSamplerDescriptor desc{};
    desc.label = vivid_sv(label);
    desc.addressModeU = WGPUAddressMode_ClampToEdge;
    desc.addressModeV = WGPUAddressMode_ClampToEdge;
    desc.addressModeW = WGPUAddressMode_ClampToEdge;
    desc.magFilter    = WGPUFilterMode_Linear;
    desc.minFilter    = WGPUFilterMode_Linear;
    desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    desc.maxAnisotropy = 1;
    return wgpuDeviceCreateSampler(device, &desc);
}

} // namespace vivid::gpu
