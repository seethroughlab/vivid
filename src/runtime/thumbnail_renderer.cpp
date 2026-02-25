#include "runtime/thumbnail_renderer.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace vivid {

static WGPUStringView to_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
}

// Same blit shader as FullscreenBlit — fullscreen triangle fills the viewport
static const char* kThumbFragment = R"(
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

static std::string make_thumb_shader() {
    return std::string(vivid::gpu::FULLSCREEN_VERTEX_WGSL) + kThumbFragment;
}

bool ThumbnailRenderer::init(WGPUDevice device, WGPUTextureFormat surface_format) {
    device_ = device;

    std::string shader_src = make_thumb_shader();
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(shader_src.c_str());

    WGPUShaderModuleDescriptor shader_desc{};
    shader_desc.nextInChain = &wgsl_src.chain;
    shader_desc.label = to_sv("Thumbnail Shader");
    shader_ = wgpuDeviceCreateShaderModule(device_, &shader_desc);
    if (!shader_) {
        std::fprintf(stderr, "[vivid] ThumbnailRenderer: failed to create shader\n");
        return false;
    }

    // Sampler (bilinear, clamp)
    WGPUSamplerDescriptor sampler_desc{};
    sampler_desc.label = to_sv("Thumb Sampler");
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
    bgl_desc.label = to_sv("Thumb BGL");
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl_desc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Thumb Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bind_layout_;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pl_desc);

    // Render pipeline (same as FullscreenBlit but we'll use loadOp=Load)
    WGPUColorTargetState color_target{};
    color_target.format = surface_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = shader_;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Thumb Pipeline");
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
        std::fprintf(stderr, "[vivid] ThumbnailRenderer: failed to create pipeline\n");
        return false;
    }

    std::fprintf(stderr, "[vivid] ThumbnailRenderer initialized\n");
    return true;
}

void ThumbnailRenderer::begin(WGPUCommandEncoder encoder, WGPUTextureView surface,
                               uint32_t surface_w, uint32_t surface_h) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = surface;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Load;   // preserve existing content
    color_att.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Thumb Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    pass_ = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass_, pipeline_);
}

void ThumbnailRenderer::draw(WGPUTextureView source, float x, float y, float w, float h) {
    if (!pass_) return;

    WGPUBindGroup bg = get_bind_group(source);
    wgpuRenderPassEncoderSetViewport(pass_, x, y, w, h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetBindGroup(pass_, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass_, 3, 1, 0, 0);
}

void ThumbnailRenderer::end() {
    if (pass_) {
        wgpuRenderPassEncoderEnd(pass_);
        wgpuRenderPassEncoderRelease(pass_);
        pass_ = nullptr;
    }
}

WGPUBindGroup ThumbnailRenderer::get_bind_group(WGPUTextureView source) {
    auto it = bind_cache_.find(source);
    if (it != bind_cache_.end()) return it->second;

    WGPUBindGroupEntry bg_entries[2]{};
    bg_entries[0].binding = 0;
    bg_entries[0].sampler = sampler_;
    bg_entries[1].binding = 1;
    bg_entries[1].textureView = source;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = to_sv("Thumb BG");
    bg_desc.layout = bind_layout_;
    bg_desc.entryCount = 2;
    bg_desc.entries = bg_entries;

    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device_, &bg_desc);
    bind_cache_[source] = bg;
    return bg;
}

void ThumbnailRenderer::shutdown() {
    for (auto& [view, bg] : bind_cache_) {
        wgpuBindGroupRelease(bg);
    }
    bind_cache_.clear();

    if (pipeline_)    { wgpuRenderPipelineRelease(pipeline_);      pipeline_    = nullptr; }
    if (bind_layout_) { wgpuBindGroupLayoutRelease(bind_layout_);  bind_layout_ = nullptr; }
    if (sampler_)     { wgpuSamplerRelease(sampler_);              sampler_     = nullptr; }
    if (pipe_layout_) { wgpuPipelineLayoutRelease(pipe_layout_);   pipe_layout_ = nullptr; }
    if (shader_)      { wgpuShaderModuleRelease(shader_);          shader_      = nullptr; }
    pass_ = nullptr;
    device_ = nullptr;
}

} // namespace vivid
