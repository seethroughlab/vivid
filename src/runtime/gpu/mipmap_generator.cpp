#include "runtime/gpu/mipmap_generator.h"
#include "common/gpu_util.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

namespace vivid {

static const char* kMipFragment = R"(
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
    return textureSampleLevel(inputTexture, textureSampler, input.uv, 0.0);
}
)";

static std::string make_shader() {
    return std::string(vivid::gpu::FULLSCREEN_VERTEX_WGSL) + kMipFragment;
}

bool MipmapGenerator::init(WGPUDevice device, WGPUTextureFormat target_format) {
    device_ = device;
    target_format_ = target_format;

    std::string shader_src = make_shader();
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(shader_src.c_str());

    WGPUShaderModuleDescriptor shader_desc{};
    shader_desc.nextInChain = &wgsl_src.chain;
    shader_desc.label = to_sv("Mipmap Shader");
    shader_ = wgpuDeviceCreateShaderModule(device_, &shader_desc);
    if (!shader_) {
        std::fprintf(stderr, "[vivid] MipmapGenerator: failed to create shader\n");
        return false;
    }

    WGPUSamplerDescriptor sampler_desc{};
    sampler_desc.label = to_sv("Mipmap Sampler");
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sampler_desc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device_, &sampler_desc);

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
    bgl_desc.label = to_sv("Mipmap BGL");
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Mipmap Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bind_layout_;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pl_desc);

    WGPUColorTargetState color_target{};
    color_target.format = target_format_;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = shader_;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Mipmap Pipeline");
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
        std::fprintf(stderr, "[vivid] MipmapGenerator: failed to create pipeline\n");
        shutdown();
        return false;
    }

    std::fprintf(stderr, "[vivid] MipmapGenerator initialized\n");
    return true;
}

void MipmapGenerator::generate(WGPUCommandEncoder encoder,
                               const std::vector<WGPUTextureView>& render_views,
                               const std::vector<WGPUTextureView>& sample_views) {
    if (!pipeline_) return;
    if (render_views.size() != sample_views.size()) return;
    if (render_views.size() < 2) return;

    for (size_t i = 1; i < render_views.size(); ++i) {
        WGPUTextureView source = sample_views[i - 1];
        WGPUTextureView dest = render_views[i];
        if (!source || !dest) continue;

        WGPUBindGroup bg = get_or_create_bind_group(source);
        if (!bg) continue;

        WGPURenderPassColorAttachment color_att{};
        color_att.view = dest;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 0.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = to_sv("Mipmap Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
}

WGPUBindGroup MipmapGenerator::get_or_create_bind_group(WGPUTextureView sample_view) {
    auto it = bind_cache_.find(sample_view);
    if (it != bind_cache_.end()) return it->second;

    WGPUBindGroupEntry bg_entries[2]{};
    bg_entries[0].binding = 0;
    bg_entries[0].sampler = sampler_;
    bg_entries[1].binding = 1;
    bg_entries[1].textureView = sample_view;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = to_sv("Mipmap BG");
    bg_desc.layout = bind_layout_;
    bg_desc.entryCount = 2;
    bg_desc.entries = bg_entries;

    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device_, &bg_desc);
    if (bg) bind_cache_[sample_view] = bg;
    return bg;
}

void MipmapGenerator::forget(WGPUTextureView sample_view) {
    auto it = bind_cache_.find(sample_view);
    if (it == bind_cache_.end()) return;
    if (it->second) wgpuBindGroupRelease(it->second);
    bind_cache_.erase(it);
}

void MipmapGenerator::shutdown() {
    for (auto& [view, bg] : bind_cache_) {
        if (bg) wgpuBindGroupRelease(bg);
    }
    bind_cache_.clear();

    if (pipeline_)    { wgpuRenderPipelineRelease(pipeline_);      pipeline_    = nullptr; }
    if (bind_layout_) { wgpuBindGroupLayoutRelease(bind_layout_);  bind_layout_ = nullptr; }
    if (pipe_layout_) { wgpuPipelineLayoutRelease(pipe_layout_);   pipe_layout_ = nullptr; }
    if (sampler_)     { wgpuSamplerRelease(sampler_);              sampler_     = nullptr; }
    if (shader_)      { wgpuShaderModuleRelease(shader_);          shader_      = nullptr; }
    device_ = nullptr;
}

} // namespace vivid
