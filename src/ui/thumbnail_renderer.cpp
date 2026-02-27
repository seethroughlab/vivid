#include "ui/thumbnail_renderer.h"
#include "common/gpu_util.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

namespace vivid::ui {

using vivid::to_sv;

// Vertex shader positions an oversized triangle at the thumbnail's pixel rect
// by converting pixel coords to NDC via a uniform buffer.
// Fragment shader samples the source texture.
static const char* kThumbShader = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct ThumbRect {
    rect: vec4f,       // x, y, w, h in pixels
    surface: vec2f,    // surface width, height
    _pad: vec2f,
};

@group(0) @binding(0) var textureSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(1) @binding(0) var<uniform> thumb: ThumbRect;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var uvs = array<vec2f, 3>(
        vec2f(0.0, 0.0),
        vec2f(2.0, 0.0),
        vec2f(0.0, 2.0)
    );
    let uv = uvs[vertexIndex];
    let px = thumb.rect.x + uv.x * thumb.rect.z;
    let py = thumb.rect.y + uv.y * thumb.rect.w;
    let ndc = vec2f(
        px / thumb.surface.x * 2.0 - 1.0,
        1.0 - py / thumb.surface.y * 2.0
    );
    var out: VertexOutput;
    out.position = vec4f(ndc, 0.0, 1.0);
    out.uv = uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTexture, textureSampler, input.uv);
}
)";

bool ThumbnailRenderer::init(WGPUDevice device, WGPUQueue queue,
                              WGPUTextureFormat surface_format) {
    device_ = device;
    queue_ = queue;

    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(kThumbShader);

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

    // Group 0: sampler + texture
    WGPUBindGroupLayoutEntry tex_entries[2]{};
    tex_entries[0].binding = 0;
    tex_entries[0].visibility = WGPUShaderStage_Fragment;
    tex_entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    tex_entries[1].binding = 1;
    tex_entries[1].visibility = WGPUShaderStage_Fragment;
    tex_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    tex_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    tex_entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = to_sv("Thumb BGL");
    bgl_desc.entryCount = 2;
    bgl_desc.entries = tex_entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl_desc);

    // Group 1: rect uniform (dynamic offset)
    WGPUBindGroupLayoutEntry rect_entry{};
    rect_entry.binding = 0;
    rect_entry.visibility = WGPUShaderStage_Vertex;
    rect_entry.buffer.type = WGPUBufferBindingType_Uniform;
    rect_entry.buffer.hasDynamicOffset = true;
    rect_entry.buffer.minBindingSize = 32;  // vec4f + vec2f + vec2f = 32 bytes

    WGPUBindGroupLayoutDescriptor rect_bgl_desc{};
    rect_bgl_desc.label = to_sv("Thumb Rect BGL");
    rect_bgl_desc.entryCount = 1;
    rect_bgl_desc.entries = &rect_entry;
    rect_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &rect_bgl_desc);

    // Rect uniform buffer (kMaxThumbs * kRectStride)
    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = to_sv("Thumb Rect UBO");
    buf_desc.size = kMaxThumbs * kRectStride;
    buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    rect_buf_ = wgpuDeviceCreateBuffer(device_, &buf_desc);

    // Bind group for rect uniform (dynamic offset)
    WGPUBindGroupEntry rect_bg_entry{};
    rect_bg_entry.binding = 0;
    rect_bg_entry.buffer = rect_buf_;
    rect_bg_entry.offset = 0;
    rect_bg_entry.size = 32;

    WGPUBindGroupDescriptor rect_bg_desc{};
    rect_bg_desc.label = to_sv("Thumb Rect BG");
    rect_bg_desc.layout = rect_layout_;
    rect_bg_desc.entryCount = 1;
    rect_bg_desc.entries = &rect_bg_entry;
    rect_bind_group_ = wgpuDeviceCreateBindGroup(device_, &rect_bg_desc);

    // Pipeline layout with 2 bind group layouts
    WGPUBindGroupLayout layouts[2] = { bind_layout_, rect_layout_ };
    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Thumb Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 2;
    pl_desc.bindGroupLayouts = layouts;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pl_desc);

    // Render pipeline
    WGPUBlendState blend{};
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target{};
    color_target.format = surface_format;
    color_target.blend = &blend;
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
    pending_encoder_ = encoder;
    pending_surface_ = surface;
    surface_w_ = surface_w;
    surface_h_ = surface_h;
    pending_.clear();
}

void ThumbnailRenderer::draw(WGPUTextureView source, float x, float y, float w, float h,
                              uint32_t scissor_x, uint32_t scissor_y,
                              uint32_t scissor_w, uint32_t scissor_h) {
    if (!pending_encoder_) return;
    if (pending_.size() >= kMaxThumbs) return;
    pending_.push_back({ source, x, y, w, h, scissor_x, scissor_y, scissor_w, scissor_h });
}

void ThumbnailRenderer::end() {
    if (!pending_encoder_) return;

    if (!pending_.empty()) {
        // Write all rect uniforms in one batch
        // Each entry is 32 bytes of data at kRectStride-byte intervals
        struct RectUniform {
            float rect[4];     // x, y, w, h
            float surface[2];  // surface_w, surface_h
            float _pad[2];
        };
        static_assert(sizeof(RectUniform) == 32, "RectUniform must be 32 bytes");

        // Build staging buffer
        std::vector<uint8_t> staging(pending_.size() * kRectStride, 0);
        for (size_t i = 0; i < pending_.size(); i++) {
            RectUniform ru;
            ru.rect[0] = pending_[i].x;
            ru.rect[1] = pending_[i].y;
            ru.rect[2] = pending_[i].w;
            ru.rect[3] = pending_[i].h;
            ru.surface[0] = static_cast<float>(surface_w_);
            ru.surface[1] = static_cast<float>(surface_h_);
            ru._pad[0] = 0.0f;
            ru._pad[1] = 0.0f;
            std::memcpy(staging.data() + i * kRectStride, &ru, sizeof(ru));
        }
        wgpuQueueWriteBuffer(queue_, rect_buf_, 0, staging.data(), staging.size());

        // Create render pass
        WGPURenderPassColorAttachment color_att{};
        color_att.view = pending_surface_;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp = WGPULoadOp_Load;
        color_att.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = to_sv("Thumb Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(pending_encoder_, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetViewport(pass, 0, 0,
                                          static_cast<float>(surface_w_),
                                          static_cast<float>(surface_h_),
                                          0.0f, 1.0f);

        for (size_t i = 0; i < pending_.size(); i++) {
            const auto& d = pending_[i];
            WGPUBindGroup tex_bg = get_bind_group(d.source);
            uint32_t dyn_offset = static_cast<uint32_t>(i * kRectStride);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, tex_bg, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, rect_bind_group_, 1, &dyn_offset);
            wgpuRenderPassEncoderSetScissorRect(pass, d.sc_x, d.sc_y, d.sc_w, d.sc_h);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    pending_encoder_ = nullptr;
    pending_surface_ = nullptr;
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
    pending_.clear();

    if (rect_bind_group_) { wgpuBindGroupRelease(rect_bind_group_);        rect_bind_group_ = nullptr; }
    if (rect_buf_)        { wgpuBufferRelease(rect_buf_);                  rect_buf_        = nullptr; }
    if (rect_layout_)     { wgpuBindGroupLayoutRelease(rect_layout_);      rect_layout_     = nullptr; }
    if (pipeline_)        { wgpuRenderPipelineRelease(pipeline_);          pipeline_         = nullptr; }
    if (bind_layout_)     { wgpuBindGroupLayoutRelease(bind_layout_);      bind_layout_      = nullptr; }
    if (sampler_)         { wgpuSamplerRelease(sampler_);                  sampler_           = nullptr; }
    if (pipe_layout_)     { wgpuPipelineLayoutRelease(pipe_layout_);       pipe_layout_       = nullptr; }
    if (shader_)          { wgpuShaderModuleRelease(shader_);              shader_            = nullptr; }
    pending_encoder_ = nullptr;
    pending_surface_ = nullptr;
    queue_ = nullptr;
    device_ = nullptr;
}

} // namespace vivid::ui
