#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "ui/renderer_2d.h"
#include "common/gpu_util.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace vivid::ui {

using vivid::to_sv;

static const char* kTextShaderWGSL = R"(
struct Uniforms {
    screen_size: vec2f,
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var atlas_sampler: sampler;
@group(0) @binding(2) var atlas_texture: texture_2d<f32>;

struct VertexInput {
    @location(0) pos: vec2f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    // Convert pixel coords to NDC: (0,0) top-left, (w,h) bottom-right -> (-1,1) to (1,-1)
    let ndc = vec2f(
        in.pos.x / uniforms.screen_size.x * 2.0 - 1.0,
        1.0 - in.pos.y / uniforms.screen_size.y * 2.0
    );
    out.position = vec4f(ndc, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let alpha = textureSample(atlas_texture, atlas_sampler, in.uv).r;
    return vec4f(in.color.rgb, in.color.a * alpha);
}
)";

bool Renderer2D::init(WGPUDevice device, WGPUTextureFormat surface_format,
                         const char* font_path, float font_size, float dpi_scale) {
    device_ = device;
    font_size_ = font_size;
    dpi_scale_ = dpi_scale;

    // --- Load font file ---
    FILE* f = std::fopen(font_path, "rb");
    if (!f) {
        std::fprintf(stderr, "[vivid] Renderer2D: failed to open font %s\n", font_path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long file_size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> font_data(file_size);
    std::fread(font_data.data(), 1, file_size, f);
    std::fclose(f);

    // --- Bake glyph atlas using stb_truetype ---
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_data.data(), 0)) {
        std::fprintf(stderr, "[vivid] Renderer2D: failed to init font\n");
        return false;
    }

    // Rasterize at physical pixel size for sharp glyphs on HiDPI displays
    float raster_size = font_size * dpi_scale;
    float scale = stbtt_ScaleForPixelHeight(&font, raster_size);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    // Store line_height in logical coordinates
    line_height_ = (ascent - descent + line_gap) * scale / dpi_scale;

    // Bake ASCII 32-126 into atlas
    std::vector<unsigned char> atlas(kAtlasWidth * kAtlasHeight, 0);

    // Reserve top-left 2x2 pixel as solid white for rect drawing
    atlas[0] = 255;
    atlas[1] = 255;
    atlas[kAtlasWidth] = 255;
    atlas[kAtlasWidth + 1] = 255;

    uint32_t pen_x = 4, pen_y = 0;
    uint32_t row_height = 0;

    for (int c = 32; c <= 126; ++c) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, c, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0;
        int gh = y1 - y0;

        if (pen_x + gw + 1 >= kAtlasWidth) {
            pen_x = 0;
            pen_y += row_height + 1;
            row_height = 0;
        }

        if (pen_y + gh >= kAtlasHeight) {
            std::fprintf(stderr, "[vivid] Renderer2D: atlas overflow at char %d\n", c);
            break;
        }

        stbtt_MakeCodepointBitmap(&font, atlas.data() + pen_y * kAtlasWidth + pen_x,
                                   gw, gh, kAtlasWidth, scale, scale, c);

        auto& gi = glyphs_[c];
        gi.u0 = static_cast<float>(pen_x) / kAtlasWidth;
        gi.v0 = static_cast<float>(pen_y) / kAtlasHeight;
        gi.u1 = static_cast<float>(pen_x + gw) / kAtlasWidth;
        gi.v1 = static_cast<float>(pen_y + gh) / kAtlasHeight;
        // Store glyph metrics in logical coordinates (divide by dpi_scale)
        gi.x0 = static_cast<float>(x0) / dpi_scale;
        gi.y0 = static_cast<float>(y0) / dpi_scale;
        gi.x1 = static_cast<float>(x1) / dpi_scale;
        gi.y1 = static_cast<float>(y1) / dpi_scale;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, c, &advance, &lsb);
        gi.advance = advance * scale / dpi_scale;

        pen_x += gw + 1;
        if (static_cast<uint32_t>(gh) > row_height) row_height = gh;
    }

    // --- Create atlas texture (R8Unorm) ---
    WGPUTextureDescriptor tex_desc{};
    tex_desc.label = to_sv("Glyph Atlas");
    tex_desc.size = { kAtlasWidth, kAtlasHeight, 1 };
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.format = WGPUTextureFormat_R8Unorm;
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    atlas_tex_ = wgpuDeviceCreateTexture(device, &tex_desc);

    // Upload atlas data
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    WGPUTexelCopyTextureInfo dest{};
    dest.texture = atlas_tex_;
    dest.mipLevel = 0;
    dest.origin = { 0, 0, 0 };
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = kAtlasWidth;
    layout.rowsPerImage = kAtlasHeight;

    WGPUExtent3D extent = { kAtlasWidth, kAtlasHeight, 1 };
    wgpuQueueWriteTexture(queue, &dest, atlas.data(), atlas.size(), &layout, &extent);

    WGPUTextureViewDescriptor view_desc{};
    view_desc.label = to_sv("Glyph Atlas View");
    view_desc.format = WGPUTextureFormat_R8Unorm;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = 1;
    view_desc.baseArrayLayer = 0;
    view_desc.arrayLayerCount = 1;
    view_desc.aspect = WGPUTextureAspect_All;
    atlas_view_ = wgpuTextureCreateView(atlas_tex_, &view_desc);

    // --- Sampler (nearest for crisp pixel text) ---
    WGPUSamplerDescriptor samp_desc{};
    samp_desc.label = to_sv("Text Sampler");
    samp_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    samp_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    samp_desc.magFilter = WGPUFilterMode_Linear;
    samp_desc.minFilter = WGPUFilterMode_Linear;
    samp_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samp_desc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samp_desc);

    // --- Double-buffered vertex buffers ---
    for (int i = 0; i < 2; ++i) {
        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = to_sv(i == 0 ? "Text VB 0" : "Text VB 1");
        buf_desc.size = kMaxVertices * sizeof(TextVertex);
        buf_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        buf_desc.mappedAtCreation = false;
        vertex_bufs_[i] = wgpuDeviceCreateBuffer(device, &buf_desc);
    }

    // --- Shader module ---
    WGPUShaderSourceWGSL wgsl_src{};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = to_sv(kTextShaderWGSL);

    WGPUShaderModuleDescriptor shader_desc{};
    shader_desc.nextInChain = &wgsl_src.chain;
    shader_desc.label = to_sv("Text Shader");
    shader_ = wgpuDeviceCreateShaderModule(device, &shader_desc);

    // --- Bind group layout: uniform(0), sampler(1), texture(2) ---
    WGPUBindGroupLayoutEntry entries[3]{};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = 8; // vec2f

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[2].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = to_sv("Text Bind Group Layout");
    bgl_desc.entryCount = 3;
    bgl_desc.entries = entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);

    // --- Pipeline layout ---
    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = to_sv("Text Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bind_layout_;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(device, &pl_desc);

    // --- Vertex buffer layout ---
    WGPUVertexAttribute attrs[3]{};
    attrs[0].format = WGPUVertexFormat_Float32x2; // pos
    attrs[0].offset = offsetof(TextVertex, x);
    attrs[0].shaderLocation = 0;

    attrs[1].format = WGPUVertexFormat_Float32x2; // uv
    attrs[1].offset = offsetof(TextVertex, u);
    attrs[1].shaderLocation = 1;

    attrs[2].format = WGPUVertexFormat_Float32x4; // color
    attrs[2].offset = offsetof(TextVertex, r);
    attrs[2].shaderLocation = 2;

    WGPUVertexBufferLayout vbl{};
    vbl.arrayStride = sizeof(TextVertex);
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 3;
    vbl.attributes = attrs;

    // --- Render pipeline with alpha blending ---
    WGPUBlendState blend{};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target{};
    color_target.format = surface_format;
    color_target.writeMask = WGPUColorWriteMask_All;
    color_target.blend = &blend;

    WGPUFragmentState fragment{};
    fragment.module = shader_;
    fragment.entryPoint = to_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc{};
    rp_desc.label = to_sv("Text Pipeline");
    rp_desc.layout = pipe_layout_;
    rp_desc.vertex.module = shader_;
    rp_desc.vertex.entryPoint = to_sv("vs_main");
    rp_desc.vertex.bufferCount = 1;
    rp_desc.vertex.buffers = &vbl;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = 0xFFFFFFFF;
    rp_desc.fragment = &fragment;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &rp_desc);
    if (!pipeline_) {
        std::fprintf(stderr, "[vivid] Renderer2D: failed to create pipeline\n");
        return false;
    }

    // Cache the solid-white UV (center of the 2x2 block at atlas origin)
    solid_u_ = 0.5f / kAtlasWidth;
    solid_v_ = 0.5f / kAtlasHeight;

    std::fprintf(stderr, "[vivid] Renderer2D initialized (%.0fpt @ %.1fx, raster %.0fpx, atlas %ux%u)\n",
        font_size, dpi_scale, raster_size, kAtlasWidth, kAtlasHeight);
    return true;
}

void Renderer2D::push_quad(float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              float r, float g, float b, float a) {
    if (vertices_.size() + 6 > kMaxVertices) { ++overflow_count_; return; }
    // Two triangles: TL, TR, BL, BL, TR, BR
    vertices_.push_back({x0, y0, u0, v0, r, g, b, a});
    vertices_.push_back({x1, y0, u1, v0, r, g, b, a});
    vertices_.push_back({x0, y1, u0, v1, r, g, b, a});
    vertices_.push_back({x0, y1, u0, v1, r, g, b, a});
    vertices_.push_back({x1, y0, u1, v0, r, g, b, a});
    vertices_.push_back({x1, y1, u1, v1, r, g, b, a});
}

void Renderer2D::push_tri(float x0, float y0, float x1, float y1, float x2, float y2,
                             float r, float g, float b, float a) {
    if (vertices_.size() + 3 > kMaxVertices) { ++overflow_count_; return; }
    vertices_.push_back({x0, y0, solid_u_, solid_v_, r, g, b, a});
    vertices_.push_back({x1, y1, solid_u_, solid_v_, r, g, b, a});
    vertices_.push_back({x2, y2, solid_u_, solid_v_, r, g, b, a});
}

void Renderer2D::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                      float r, float g, float b, float a) {
    if (radius <= 0.0f) { draw_rect(x, y, w, h, r, g, b, a); return; }
    // Clamp radius to half the smaller dimension
    float max_r = std::min(w, h) * 0.5f;
    if (radius > max_r) radius = max_r;

    // 3 body rectangles forming a cross shape
    // Center band (full width, excluding corner rows)
    push_quad(x, y + radius, x + w, y + h - radius,
              0.0f, 0.0f, 1.0f / kAtlasWidth, 1.0f / kAtlasHeight, r, g, b, a);
    // Top band (between corners)
    push_quad(x + radius, y, x + w - radius, y + radius,
              0.0f, 0.0f, 1.0f / kAtlasWidth, 1.0f / kAtlasHeight, r, g, b, a);
    // Bottom band (between corners)
    push_quad(x + radius, y + h - radius, x + w - radius, y + h,
              0.0f, 0.0f, 1.0f / kAtlasWidth, 1.0f / kAtlasHeight, r, g, b, a);

    // 4 corner fans (8 segments each)
    constexpr int kCornerSegs = 8;
    constexpr float kPi = 3.14159265358979323846f;
    float cx[4] = { x + radius, x + w - radius, x + w - radius, x + radius };
    float cy[4] = { y + radius, y + radius, y + h - radius, y + h - radius };
    float start_angle[4] = { kPi, 1.5f * kPi, 0.0f, 0.5f * kPi };

    for (int c = 0; c < 4; ++c) {
        float angle_step = (kPi * 0.5f) / kCornerSegs;
        for (int s = 0; s < kCornerSegs; ++s) {
            float a0 = start_angle[c] + s * angle_step;
            float a1 = a0 + angle_step;
            float px0 = cx[c] + radius * std::cos(a0);
            float py0 = cy[c] + radius * std::sin(a0);
            float px1 = cx[c] + radius * std::cos(a1);
            float py1 = cy[c] + radius * std::sin(a1);
            push_tri(cx[c], cy[c], px0, py0, px1, py1, r, g, b, a);
        }
    }
}

void Renderer2D::draw_text(float x, float y, const char* text,
                              float r, float g, float b, float a, float scale) {
    float baseline = y + font_size_ * 0.8f * scale;
    float pen = x;
    for (const char* p = text; *p; ++p) {
        unsigned char c = *p;
        if (c < 32 || c > 126) continue;
        const auto& gi = glyphs_[c];
        float gx0 = pen + gi.x0 * scale;
        float gy0 = baseline + gi.y0 * scale;
        float gx1 = pen + gi.x1 * scale;
        float gy1 = baseline + gi.y1 * scale;
        push_quad(gx0, gy0, gx1, gy1, gi.u0, gi.v0, gi.u1, gi.v1, r, g, b, a);
        pen += gi.advance * scale;
    }
}

void Renderer2D::draw_rect(float x, float y, float w, float h,
                              float r, float g, float b, float a) {
    // UV points to the solid white 2x2 block at top-left of atlas
    float su0 = 0.0f, sv0 = 0.0f;
    float su1 = 1.0f / kAtlasWidth, sv1 = 1.0f / kAtlasHeight;
    push_quad(x, y, x + w, y + h, su0, sv0, su1, sv1, r, g, b, a);
}

void Renderer2D::draw_line(float x1, float y1, float x2, float y2, float thickness,
                              float r, float g, float b, float a) {
    if (vertices_.size() + 6 > kMaxVertices) { ++overflow_count_; return; }
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    // Perpendicular offset for thickness
    float nx = -dy / len * thickness * 0.5f;
    float ny =  dx / len * thickness * 0.5f;
    // Solid white UV from atlas corner
    float su = 0.5f / kAtlasWidth, sv = 0.5f / kAtlasHeight;
    // 6 vertices: two triangles forming a rotated quad
    vertices_.push_back({x1 + nx, y1 + ny, su, sv, r, g, b, a});
    vertices_.push_back({x1 - nx, y1 - ny, su, sv, r, g, b, a});
    vertices_.push_back({x2 + nx, y2 + ny, su, sv, r, g, b, a});
    vertices_.push_back({x2 + nx, y2 + ny, su, sv, r, g, b, a});
    vertices_.push_back({x1 - nx, y1 - ny, su, sv, r, g, b, a});
    vertices_.push_back({x2 - nx, y2 - ny, su, sv, r, g, b, a});
}

void Renderer2D::finalize_batch() {
    uint32_t batch_start = 0;
    if (!batches_.empty()) {
        auto& last = batches_.back();
        batch_start = last.start + last.count;
    }
    uint32_t verts_now = static_cast<uint32_t>(vertices_.size());
    if (verts_now > batch_start) {
        DrawBatch b;
        b.start = batch_start;
        b.count = verts_now - batch_start;
        if (!clip_stack_.empty()) {
            b.has_scissor = true;
            b.sx = clip_stack_.back().sx;
            b.sy = clip_stack_.back().sy;
            b.sw = clip_stack_.back().sw;
            b.sh = clip_stack_.back().sh;
        }
        batches_.push_back(b);
    }
}

void Renderer2D::push_clip_rect(float x, float y, float w, float h) {
    finalize_batch();
    DrawBatch clip;
    clip.has_scissor = true;
    clip.sx = x * dpi_scale_;
    clip.sy = y * dpi_scale_;
    clip.sw = w * dpi_scale_;
    clip.sh = h * dpi_scale_;
    clip_stack_.push_back(clip);
}

void Renderer2D::pop_clip_rect() {
    finalize_batch();
    if (!clip_stack_.empty()) clip_stack_.pop_back();
}

float Renderer2D::text_width(const char* text, float scale) const {
    float w = 0;
    for (const char* p = text; *p; ++p) {
        unsigned char c = *p;
        if (c >= 32 && c <= 126) w += glyphs_[c].advance * scale;
    }
    return w;
}

void Renderer2D::flush(WGPUCommandEncoder encoder, WGPUTextureView surface_view,
                          uint32_t surface_width, uint32_t surface_height) {
    if (vertices_.empty()) {
        batches_.clear();
        clip_stack_.clear();
        return;
    }

    // Finalize any remaining vertices into the last batch
    finalize_batch();

    WGPUQueue queue = wgpuDeviceGetQueue(device_);

    // Double-buffered vertex upload: alternate buffers each flush so the
    // GPU never reads from a buffer that was just written. Works around a
    // wgpu-native bug where wgpuQueueWriteBuffer data may not be visible
    // to the GPU in time, causing early quads to render as invisible.
    size_t data_size = vertices_.size() * sizeof(TextVertex);
    WGPUBuffer vb = vertex_bufs_[buf_idx_];
    buf_idx_ ^= 1;
    wgpuQueueWriteBuffer(queue, vb, 0, vertices_.data(), data_size);

    // Create uniform buffer with screen size
    float uniforms[2] = { (float)surface_width, (float)surface_height };
    WGPUBufferDescriptor ub_desc{};
    ub_desc.label = to_sv("Text Uniforms");
    ub_desc.size = 8;
    ub_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniform_buf = wgpuDeviceCreateBuffer(device_, &ub_desc);
    wgpuQueueWriteBuffer(queue, uniform_buf, 0, uniforms, 8);

    // Create bind group
    WGPUBindGroupEntry bg_entries[3]{};
    bg_entries[0].binding = 0;
    bg_entries[0].buffer = uniform_buf;
    bg_entries[0].offset = 0;
    bg_entries[0].size = 8;

    bg_entries[1].binding = 1;
    bg_entries[1].sampler = sampler_;

    bg_entries[2].binding = 2;
    bg_entries[2].textureView = atlas_view_;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = to_sv("Text Bind Group");
    bg_desc.layout = bind_layout_;
    bg_desc.entryCount = 3;
    bg_desc.entries = bg_entries;
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device_, &bg_desc);

    // Render pass with loadOp=Load to composite on top of existing content
    WGPURenderPassColorAttachment color_att{};
    color_att.view = surface_view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Load;
    color_att.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Text Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, data_size);

    // SetScissorRect requires physical pixels; surface_width/height are logical.
    uint32_t phys_w = static_cast<uint32_t>(surface_width * dpi_scale_);
    uint32_t phys_h = static_cast<uint32_t>(surface_height * dpi_scale_);

    // Draw batches with per-batch scissor rects
    for (const auto& batch : batches_) {
        if (batch.count == 0) continue;
        if (batch.has_scissor) {
            // batch.sx/sy/sw/sh are already in physical pixels (scaled in push_clip_rect)
            uint32_t sx = static_cast<uint32_t>(std::max(0.0f, batch.sx));
            uint32_t sy = static_cast<uint32_t>(std::max(0.0f, batch.sy));
            uint32_t sw = static_cast<uint32_t>(std::max(0.0f, batch.sw));
            uint32_t sh = static_cast<uint32_t>(std::max(0.0f, batch.sh));
            // Clamp to surface bounds
            if (sx + sw > phys_w) sw = phys_w > sx ? phys_w - sx : 0;
            if (sy + sh > phys_h) sh = phys_h > sy ? phys_h - sy : 0;
            wgpuRenderPassEncoderSetScissorRect(pass, sx, sy, sw, sh);
        } else {
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, phys_w, phys_h);
        }
        wgpuRenderPassEncoderDraw(pass, batch.count, 1, batch.start, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    wgpuBindGroupRelease(bind_group);
    wgpuBufferRelease(uniform_buf);

    if (overflow_count_ > 0) {
        std::fprintf(stderr, "[vivid] Renderer2D: dropped %u quads (vertex buffer full, %u/%u verts used)\n",
                     overflow_count_, static_cast<uint32_t>(vertices_.size()), kMaxVertices);
        overflow_count_ = 0;
    }

    vertices_.clear();
    batches_.clear();
    clip_stack_.clear();
}

void Renderer2D::shutdown() {
    vertices_.clear();
    for (auto& vb : vertex_bufs_) {
        if (vb) { wgpuBufferRelease(vb); vb = nullptr; }
    }
    if (pipeline_)    { wgpuRenderPipelineRelease(pipeline_);     pipeline_    = nullptr; }
    if (bind_layout_) { wgpuBindGroupLayoutRelease(bind_layout_); bind_layout_ = nullptr; }
    if (sampler_)     { wgpuSamplerRelease(sampler_);             sampler_     = nullptr; }
    if (atlas_view_)  { wgpuTextureViewRelease(atlas_view_);      atlas_view_  = nullptr; }
    if (atlas_tex_)   { wgpuTextureRelease(atlas_tex_);           atlas_tex_   = nullptr; }
    if (pipe_layout_) { wgpuPipelineLayoutRelease(pipe_layout_);  pipe_layout_ = nullptr; }
    if (shader_)      { wgpuShaderModuleRelease(shader_);         shader_      = nullptr; }
    device_ = nullptr;
}

} // namespace vivid::ui
