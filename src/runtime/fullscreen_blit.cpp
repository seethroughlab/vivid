#include "runtime/fullscreen_blit.h"
#include "common/gpu_util.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace vivid {

// Fallback clear color: #16191D as raw unorm (used when no source texture covers a region)
static constexpr WGPUColor kFallbackClear = { 0.0863, 0.0980, 0.1137, 1.0 };

// Blit fragment shader (specific to blitting — uses shared vertex code)
static const char* kBlitFragment = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@group(0) @binding(0) var textureSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTexture, textureSampler, input.uv);
}
)";

static std::string make_blit_shader() {
    return std::string(vivid::gpu::FULLSCREEN_VERTEX_WGSL) + kBlitFragment;
}

bool FullscreenBlit::init(WGPUDevice device, WGPUTextureFormat target_format) {
    device_ = device;
    queue_  = wgpuDeviceGetQueue(device);
    target_format_ = target_format;

    // Shader module (shared fullscreen vertex + blit fragment)
    std::string shader_src = make_blit_shader();
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(shader_src.c_str());

    WGPUShaderModuleDescriptor shader_desc{};
    shader_desc.nextInChain = &wgsl_src.chain;
    shader_desc.label = to_sv("Blit Shader");
    shader_ = wgpuDeviceCreateShaderModule(device_, &shader_desc);
    if (!shader_) {
        std::fprintf(stderr, "[vivid] FullscreenBlit: failed to create shader module\n");
        return false;
    }

    // Sampler (linear, clamp-to-edge)
    WGPUSamplerDescriptor sampler_desc{};
    sampler_desc.nextInChain = nullptr;
    sampler_desc.label = to_sv("Blit Sampler");
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sampler_desc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device_, &sampler_desc);

    // Bind group layout: sampler (0) + texture (1)
    WGPUBindGroupLayoutEntry entries[2]{};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = to_sv("Blit Bind Group Layout");
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl_desc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Blit Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bind_layout_;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pl_desc);

    // Render pipeline
    WGPUColorTargetState color_target{};
    color_target.format = target_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = shader_;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Blit Pipeline");
    rp_desc.layout = pipe_layout_;
    rp_desc.vertex.module = shader_;
    rp_desc.vertex.entryPoint = to_sv("vs_main");
    rp_desc.vertex.bufferCount = 0;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rp_desc);
    if (!pipeline_) {
        std::fprintf(stderr, "[vivid] FullscreenBlit: failed to create render pipeline\n");
        return false;
    }

    std::fprintf(stderr, "[vivid] FullscreenBlit initialized\n");
    return true;
}

void FullscreenBlit::blit(WGPUCommandEncoder encoder,
                          WGPUTextureView source, WGPUTextureView dest) {
    // Recreate bind group only when source view changes
    if (source != cached_source_) {
        if (cached_bind_group_)
            wgpuBindGroupRelease(cached_bind_group_);

        WGPUBindGroupEntry bg_entries[2]{};
        bg_entries[0].binding = 0;
        bg_entries[0].sampler = sampler_;
        bg_entries[1].binding = 1;
        bg_entries[1].textureView = source;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = to_sv("Blit Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 2;
        bg_desc.entries = bg_entries;
        cached_bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);
        cached_source_ = source;
    }

    // Render pass on dest
    WGPURenderPassColorAttachment color_att{};
    color_att.view = dest;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = kFallbackClear;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Blit Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, cached_bind_group_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// -----------------------------------------------------------------------
// Fit-mode blit (scale/offset uniforms for Fit/Fill/Stretch)
// -----------------------------------------------------------------------

static const char* kFitBlitFragment = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct FitUniforms {
    scale: vec2f,
    offset: vec2f,
    ui_visible: f32,
    _pad: f32,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@group(0) @binding(0) var textureSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var<uniform> fit: FitUniforms;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = (input.uv - fit.offset) / fit.scale;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        let cell = vec2u(input.position.xy / 16.0);
        let checker = f32((cell.x + cell.y) % 2u);
        let c = mix(0.08, 0.14, checker);
        return vec4f(c, c, c, 1.0);
    }
    return textureSample(inputTexture, textureSampler, uv);
}
)";

static std::string make_fit_blit_shader() {
    return std::string(vivid::gpu::FULLSCREEN_VERTEX_WGSL) + kFitBlitFragment;
}

struct FitUniforms {
    float scale[2];
    float offset[2];
    float ui_visible;
    float _pad;
};

bool FullscreenBlit::init_fit_pipeline() {
    if (fit_inited_) return fit_pipeline_ != nullptr;
    fit_inited_ = true;

    std::string shader_src = make_fit_blit_shader();
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(shader_src.c_str());

    WGPUShaderModuleDescriptor shader_desc{};
    shader_desc.nextInChain = &wgsl_src.chain;
    shader_desc.label = to_sv("Fit Blit Shader");
    fit_shader_ = wgpuDeviceCreateShaderModule(device_, &shader_desc);
    if (!fit_shader_) return false;

    // Uniform buffer
    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = to_sv("Fit Uniforms");
    buf_desc.size = sizeof(FitUniforms);
    buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    fit_uniform_buf_ = wgpuDeviceCreateBuffer(device_, &buf_desc);

    // Bind group layout: sampler(0) + texture(1) + uniform(2)
    WGPUBindGroupLayoutEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].buffer.type = WGPUBufferBindingType_Uniform;
    entries[2].buffer.minBindingSize = sizeof(FitUniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = to_sv("Fit Blit BGL");
    bgl_desc.entryCount = 3;
    bgl_desc.entries = entries;
    fit_bind_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Fit Blit Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &fit_bind_layout_;
    fit_pipe_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pl_desc);

    WGPUColorTargetState color_target{};
    color_target.format = target_format_;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = fit_shader_;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Fit Blit Pipeline");
    rp_desc.layout = fit_pipe_layout_;
    rp_desc.vertex.module = fit_shader_;
    rp_desc.vertex.entryPoint = to_sv("vs_main");
    rp_desc.vertex.bufferCount = 0;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;

    fit_pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rp_desc);
    if (!fit_pipeline_) {
        std::fprintf(stderr, "[vivid] FullscreenBlit: failed to create fit pipeline\n");
        return false;
    }
    return true;
}

void FullscreenBlit::blit_fit(WGPUCommandEncoder encoder,
                               WGPUTextureView source, WGPUTextureView dest,
                               uint32_t src_w, uint32_t src_h,
                               uint32_t dst_w, uint32_t dst_h,
                               FitMode fit_mode, bool ui_visible) {
    if (!init_fit_pipeline()) {
        // Fall back to regular stretch blit
        blit(encoder, source, dest);
        return;
    }

    // Compute scale and offset based on fit mode
    FitUniforms u{};
    float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
    float dst_aspect = static_cast<float>(dst_w) / static_cast<float>(dst_h);

    if (fit_mode == FitMode::Stretch) {
        // Stretch: 1:1 UV mapping
        u.scale[0] = 1.0f;
        u.scale[1] = 1.0f;
        u.offset[0] = 0.0f;
        u.offset[1] = 0.0f;
    } else if (fit_mode == FitMode::Fit) {
        // Fit: scale uniformly so entire source fits, letterbox remaining
        if (src_aspect > dst_aspect) {
            // Source wider → letterbox top/bottom
            u.scale[0] = 1.0f;
            u.scale[1] = dst_aspect / src_aspect;
        } else {
            // Source taller → letterbox left/right
            u.scale[0] = src_aspect / dst_aspect;
            u.scale[1] = 1.0f;
        }
        u.offset[0] = (1.0f - u.scale[0]) * 0.5f;
        u.offset[1] = (1.0f - u.scale[1]) * 0.5f;
    } else {
        // Fill: scale uniformly so source covers dest, crop overflow
        if (src_aspect > dst_aspect) {
            u.scale[0] = src_aspect / dst_aspect;
            u.scale[1] = 1.0f;
        } else {
            u.scale[0] = 1.0f;
            u.scale[1] = dst_aspect / src_aspect;
        }
        u.offset[0] = (1.0f - u.scale[0]) * 0.5f;
        u.offset[1] = (1.0f - u.scale[1]) * 0.5f;
    }

    u.ui_visible = ui_visible ? 1.0f : 0.0f;
    wgpuQueueWriteBuffer(queue_, fit_uniform_buf_, 0, &u, sizeof(u));

    // Recreate bind group only when source view changes
    if (source != fit_cached_source_) {
        if (fit_cached_bind_group_)
            wgpuBindGroupRelease(fit_cached_bind_group_);

        WGPUBindGroupEntry bg_entries[3]{};
        bg_entries[0].binding = 0;
        bg_entries[0].sampler = sampler_;
        bg_entries[1].binding = 1;
        bg_entries[1].textureView = source;
        bg_entries[2].binding = 2;
        bg_entries[2].buffer = fit_uniform_buf_;
        bg_entries[2].offset = 0;
        bg_entries[2].size = sizeof(FitUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = to_sv("Fit Blit Bind Group");
        bg_desc.layout = fit_bind_layout_;
        bg_desc.entryCount = 3;
        bg_desc.entries = bg_entries;
        fit_cached_bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);
        fit_cached_source_ = source;
    }

    // Render pass on dest
    WGPURenderPassColorAttachment color_att{};
    color_att.view = dest;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = kFallbackClear;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Fit Blit Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, fit_pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, fit_cached_bind_group_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void FullscreenBlit::shutdown() {
    if (cached_bind_group_) { wgpuBindGroupRelease(cached_bind_group_); cached_bind_group_ = nullptr; }
    cached_source_ = nullptr;
    if (pipeline_)    { wgpuRenderPipelineRelease(pipeline_);      pipeline_    = nullptr; }
    if (bind_layout_) { wgpuBindGroupLayoutRelease(bind_layout_);  bind_layout_ = nullptr; }
    if (sampler_)     { wgpuSamplerRelease(sampler_);              sampler_     = nullptr; }
    if (pipe_layout_) { wgpuPipelineLayoutRelease(pipe_layout_);   pipe_layout_ = nullptr; }
    if (shader_)      { wgpuShaderModuleRelease(shader_);          shader_      = nullptr; }

    // Fit pipeline cleanup
    if (fit_cached_bind_group_) { wgpuBindGroupRelease(fit_cached_bind_group_); fit_cached_bind_group_ = nullptr; }
    fit_cached_source_ = nullptr;
    if (fit_pipeline_)    { wgpuRenderPipelineRelease(fit_pipeline_);      fit_pipeline_    = nullptr; }
    if (fit_bind_layout_) { wgpuBindGroupLayoutRelease(fit_bind_layout_);  fit_bind_layout_ = nullptr; }
    if (fit_pipe_layout_) { wgpuPipelineLayoutRelease(fit_pipe_layout_);   fit_pipe_layout_ = nullptr; }
    if (fit_shader_)      { wgpuShaderModuleRelease(fit_shader_);          fit_shader_      = nullptr; }
    if (fit_uniform_buf_) { wgpuBufferRelease(fit_uniform_buf_);           fit_uniform_buf_ = nullptr; }
    fit_inited_ = false;

    if (queue_) { wgpuQueueRelease(queue_); queue_ = nullptr; }
    device_ = nullptr;
}

} // namespace vivid
