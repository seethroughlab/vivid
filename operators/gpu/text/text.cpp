#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#endif

// =============================================================================
// Text WGSL Fragment Shader
// =============================================================================

static const char* kTextFragment = R"(

struct Uniforms {
    fg_r: f32,
    fg_g: f32,
    fg_b: f32,
    _pad0: f32,
    bg_r: f32,
    bg_g: f32,
    bg_b: f32,
    bg_a: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var glyph_sampler: sampler;
@group(0) @binding(2) var glyph_texture: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let alpha = textureSample(glyph_texture, glyph_sampler, input.uv).r;
    let fg = vec3f(uniforms.fg_r, uniforms.fg_g, uniforms.fg_b);
    let bg = vec4f(uniforms.bg_r, uniforms.bg_g, uniforms.bg_b, uniforms.bg_a);
    return vec4f(mix(bg.rgb, fg, alpha), mix(bg.a, 1.0, alpha));
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms
// =============================================================================

struct TextUniforms {
    float fg_r, fg_g, fg_b;
    float _pad0;
    float bg_r, bg_g, bg_b, bg_a;
};
/**
 * @brief Renders simple static text with configurable font, color, and position.
 *
 * Rasterizes text using TrueType fonts via stb_truetype. For animated
 * or per-character effects, use RichText instead.
 *
 * @see RichText
 */
struct Text : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Text";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::TextValue> text {"text", ""};
    vivid::Param<float> size {"size", 0.4f, 0.05f, 1.0f};
    vivid::Param<float> r    {"r",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> g    {"g",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> b    {"b",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_r {"bg_r", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_g {"bg_g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_b {"bg_b", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_a {"bg_a", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> x    {"x",    0.0f, -1.0f, 1.0f};
    vivid::Param<float> y    {"y",    0.0f, -1.0f, 1.0f};

    Text() {
        vivid::semantic_tag(r, "color_rgba");
        vivid::semantic_shape(r, "scalar");

        vivid::semantic_tag(g, "color_rgba");
        vivid::semantic_shape(g, "scalar");

        vivid::semantic_tag(b, "color_rgba");
        vivid::semantic_shape(b, "scalar");

        vivid::semantic_tag(bg_r, "color_rgba");
        vivid::semantic_shape(bg_r, "scalar");

        vivid::semantic_tag(bg_g, "color_rgba");
        vivid::semantic_shape(bg_g, "scalar");

        vivid::semantic_tag(bg_b, "color_rgba");
        vivid::semantic_shape(bg_b, "scalar");

        vivid::semantic_tag(bg_a, "color_rgba");
        vivid::semantic_shape(bg_a, "scalar");

        vivid::semantic_tag(x, "position_xy");
        vivid::semantic_shape(x, "scalar");

        vivid::semantic_tag(y, "position_xy");
        vivid::semantic_shape(y, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(r, VIVID_DISPLAY_COLOR);
        display_hint(g, VIVID_DISPLAY_COLOR);
        display_hint(b, VIVID_DISPLAY_COLOR);
        display_hint(bg_r, VIVID_DISPLAY_COLOR);
        display_hint(bg_g, VIVID_DISPLAY_COLOR);
        display_hint(bg_b, VIVID_DISPLAY_COLOR);
        display_hint(x, VIVID_DISPLAY_XY_PAD);
        display_hint(y, VIVID_DISPLAY_XY_PAD);

        out.push_back(&text);
        out.push_back(&size);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&bg_r);
        out.push_back(&bg_g);
        out.push_back(&bg_b);
        out.push_back(&bg_a);
        out.push_back(&x);
        out.push_back(&y);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[text] lazy_init FAILED\n");
                return;
            }
        }

        // Read text from param (scheduler updates it if wired)
        std::string current_text = text.str_value;

        // Check if glyph texture needs re-bake
        uint32_t w = ctx->output_width;
        uint32_t h = ctx->output_height;
        if (current_text != last_text_ || size.value != last_size_ ||
            w != last_w_ || h != last_h_) {
            bake_glyphs(ctx, w, h, current_text);
            last_text_ = current_text;
            last_size_ = size.value;
            last_w_ = w;
            last_h_ = h;
        }

        // Update uniforms
        TextUniforms u{};
        u.fg_r = r.value;
        u.fg_g = g.value;
        u.fg_b = b.value;
        u.bg_r = bg_r.value;
        u.bg_g = bg_g.value;
        u.bg_b = bg_b.value;
        u.bg_a = bg_a.value;

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Text Pass");
    }

    ~Text() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(glyph_tex_);
        vivid::gpu::release(glyph_view_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUTexture         glyph_tex_   = nullptr;
    WGPUTextureView     glyph_view_  = nullptr;
    WGPUDevice          device_      = nullptr;
    WGPUQueue           queue_       = nullptr;

    // Font data (loaded once)
    std::vector<unsigned char> font_data_;
    stbtt_fontinfo font_info_{};
    bool font_loaded_ = false;

    // Change-detection state
    std::string last_text_;
    float       last_size_ = -1.0f;
    uint32_t    last_w_ = 0;
    uint32_t    last_h_ = 0;

    static std::string resolve_font_path() {
        const char* name = "JetBrainsMono-Regular.ttf";
        // Try CWD first
        if (FILE* f = std::fopen(name, "rb")) { std::fclose(f); return name; }
#ifdef __APPLE__
        // Resolve relative to this dylib's location
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&resolve_font_path), &info) && info.dli_fname) {
            std::string dir(info.dli_fname);
            auto slash = dir.rfind('/');
            if (slash != std::string::npos) {
                dir.resize(slash + 1);
                std::string p = dir + name;
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
                // Also try ../Resources/ (app bundle)
                p = dir + "../Resources/" + name;
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
            }
        }
#endif
        return name; // fallback
    }

    bool load_font() {
        if (font_loaded_) return true;

        std::string font_path = resolve_font_path();
        FILE* f = std::fopen(font_path.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "[text] cannot open font: %s\n", font_path.c_str());
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        long file_size = std::ftell(f);
        if (file_size <= 0) { std::fclose(f); return false; }
        std::fseek(f, 0, SEEK_SET);
        font_data_.resize(file_size);
        size_t rd = std::fread(font_data_.data(), 1, file_size, f);
        std::fclose(f);
        if (static_cast<long>(rd) != file_size) return false;

        if (!stbtt_InitFont(&font_info_, font_data_.data(), 0)) {
            std::fprintf(stderr, "[text] stbtt_InitFont failed\n");
            return false;
        }
        font_loaded_ = true;
        return true;
    }

    void bake_glyphs(const VividGpuContext* gpu, uint32_t w, uint32_t h, const std::string& str) {
        if (!load_font()) return;

        // Compute pixel height from size param (fraction of output height)
        float pixel_height = size.value * static_cast<float>(h);
        if (pixel_height < 4.0f) pixel_height = 4.0f;
        float scale = stbtt_ScaleForPixelHeight(&font_info_, pixel_height);

        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info_, &ascent, &descent, &line_gap);
        float scaled_ascent = ascent * scale;

        // First pass: measure total width
        float total_width = 0;
        for (size_t i = 0; i < str.size(); ++i) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font_info_, str[i], &advance, &lsb);
            total_width += advance * scale;
            if (i + 1 < str.size()) {
                total_width += stbtt_GetCodepointKernAdvance(&font_info_, str[i], str[i+1]) * scale;
            }
        }

        // Compute centering position (with x/y offset applied)
        float cx = (static_cast<float>(w) - total_width) * 0.5f + x.value * static_cast<float>(w) * 0.5f;
        float cy = (static_cast<float>(h) - pixel_height) * 0.5f + scaled_ascent - y.value * static_cast<float>(h) * 0.5f;

        // WebGPU requires bytesPerRow aligned to 256
        uint32_t row_stride = (w + 255) & ~255u;

        // Rasterize into staging buffer (padded rows)
        std::vector<unsigned char> staging(row_stride * h, 0);

        float pen_x = cx;
        for (size_t i = 0; i < str.size(); ++i) {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font_info_, str[i], scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0;
            int gh = y1 - y0;

            int px = static_cast<int>(pen_x) + x0;
            int py = static_cast<int>(cy) + y0;

            // Render glyph only if it fits within bounds
            if (gw > 0 && gh > 0) {
                // Clip to staging buffer
                int clip_x0 = px < 0 ? -px : 0;
                int clip_y0 = py < 0 ? -py : 0;
                int clip_x1 = (px + gw > static_cast<int>(w)) ? static_cast<int>(w) - px : gw;
                int clip_y1 = (py + gh > static_cast<int>(h)) ? static_cast<int>(h) - py : gh;

                if (clip_x0 < clip_x1 && clip_y0 < clip_y1) {
                    // Render full glyph to temp buffer, then copy clipped region
                    std::vector<unsigned char> glyph_buf(gw * gh, 0);
                    stbtt_MakeCodepointBitmap(&font_info_, glyph_buf.data(),
                                               gw, gh, gw, scale, scale, str[i]);

                    for (int gy = clip_y0; gy < clip_y1; ++gy) {
                        for (int gx = clip_x0; gx < clip_x1; ++gx) {
                            int dst_x = px + gx;
                            int dst_y = py + gy;
                            unsigned char val = glyph_buf[gy * gw + gx];
                            // Additive blend for overlapping glyphs
                            unsigned char& dst = staging[dst_y * row_stride + dst_x];
                            int sum = static_cast<int>(dst) + static_cast<int>(val);
                            dst = static_cast<unsigned char>(sum > 255 ? 255 : sum);
                        }
                    }
                }
            }

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font_info_, str[i], &advance, &lsb);
            pen_x += advance * scale;
            if (i + 1 < str.size()) {
                pen_x += stbtt_GetCodepointKernAdvance(&font_info_, str[i], str[i+1]) * scale;
            }
        }

        // Upload to GPU texture
        if (glyph_tex_ && (last_w_ != w || last_h_ != h)) {
            // Resolution changed — recreate texture and rebuild bind group
            vivid::gpu::release(glyph_tex_);
            vivid::gpu::release(glyph_view_);
            glyph_tex_ = nullptr;
            glyph_view_ = nullptr;
        }

        if (!glyph_tex_) {
            WGPUTextureDescriptor tex_desc{};
            tex_desc.label = vivid_sv("Text Glyph Texture");
            tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            tex_desc.dimension = WGPUTextureDimension_2D;
            tex_desc.size = {w, h, 1};
            tex_desc.format = WGPUTextureFormat_R8Unorm;
            tex_desc.mipLevelCount = 1;
            tex_desc.sampleCount = 1;
            glyph_tex_ = wgpuDeviceCreateTexture(device_, &tex_desc);

            WGPUTextureViewDescriptor view_desc{};
            view_desc.label = vivid_sv("Text Glyph View");
            view_desc.format = WGPUTextureFormat_R8Unorm;
            view_desc.dimension = WGPUTextureViewDimension_2D;
            view_desc.mipLevelCount = 1;
            view_desc.arrayLayerCount = 1;
            glyph_view_ = wgpuTextureCreateView(glyph_tex_, &view_desc);

            // Rebuild bind group with new texture view
            rebuild_bind_group();
        }

        // Write staging data to texture (row_stride is 256-aligned)
        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = row_stride;
        layout.rowsPerImage = h;

        WGPUTexelCopyTextureInfo dest{};
        dest.texture = glyph_tex_;
        dest.mipLevel = 0;
        dest.origin = {0, 0, 0};

        WGPUExtent3D extent = {w, h, 1};
        wgpuQueueWriteTexture(queue_, &dest, staging.data(), staging.size(), &layout, &extent);
    }

    void rebuild_bind_group() {
        vivid::gpu::release(bind_group_);

        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_;
        entries[0].offset  = 0;
        entries[0].size    = sizeof(TextUniforms);

        entries[1].binding = 1;
        entries[1].sampler = sampler_;

        entries[2].binding = 2;
        entries[2].textureView = glyph_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Text Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 3;
        bg_desc.entries = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        device_ = gpu->device;
        queue_  = gpu->queue;

        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kTextFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Text Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(TextUniforms), "Text Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Text Sampler");

        // Bind group layout: uniform (0), sampler (1), texture (2)
        WGPUBindGroupLayoutEntry bgl_entries[3]{};

        bgl_entries[0].binding = 0;
        bgl_entries[0].visibility = WGPUShaderStage_Fragment;
        bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entries[0].buffer.minBindingSize = sizeof(TextUniforms);

        bgl_entries[1].binding = 1;
        bgl_entries[1].visibility = WGPUShaderStage_Fragment;
        bgl_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        bgl_entries[2].binding = 2;
        bgl_entries[2].visibility = WGPUShaderStage_Fragment;
        bgl_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        bgl_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Text BGL");
        bgl_desc.entryCount = 3;
        bgl_desc.entries = bgl_entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Text Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Create a 1x1 placeholder glyph texture so the bind group is valid
        // before bake_glyphs() runs
        WGPUTextureDescriptor tex_desc{};
        tex_desc.label = vivid_sv("Text Glyph Placeholder");
        tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        tex_desc.dimension = WGPUTextureDimension_2D;
        tex_desc.size = {1, 1, 1};
        tex_desc.format = WGPUTextureFormat_R8Unorm;
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount = 1;
        glyph_tex_ = wgpuDeviceCreateTexture(gpu->device, &tex_desc);

        WGPUTextureViewDescriptor view_desc{};
        view_desc.label = vivid_sv("Text Glyph Placeholder View");
        view_desc.format = WGPUTextureFormat_R8Unorm;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.mipLevelCount = 1;
        view_desc.arrayLayerCount = 1;
        glyph_view_ = wgpuTextureCreateView(glyph_tex_, &view_desc);

        // Build initial bind group
        rebuild_bind_group();

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                 gpu->output_format, "Text Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(Text)
