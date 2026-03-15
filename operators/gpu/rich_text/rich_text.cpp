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
// Rich Text WGSL Fragment Shader — animated per-character text
// =============================================================================

static const char* kRichTextFragment = R"(

struct Uniforms {
    fg_r: f32,
    fg_g: f32,
    fg_b: f32,
    time: f32,
    bg_r: f32,
    bg_g: f32,
    bg_b: f32,
    bg_a: f32,
    anim_mode: f32,
    anim_speed: f32,
    anim_amount: f32,
    resolution_x: f32,
    resolution_y: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var glyph_sampler: sampler;
@group(0) @binding(2) var glyph_texture: texture_2d<f32>;
@group(0) @binding(3) var char_index_texture: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

// Pseudo-random hash for scatter animation
fn hash(n: f32) -> f32 {
    return fract(sin(n * 127.1) * 43758.5453);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let mode = i32(uniforms.anim_mode);
    let spd = uniforms.anim_speed;
    let amt = uniforms.anim_amount;
    let t = uniforms.time;

    // Get character index at this pixel (0..1 range, 0 = no character)
    let idx = textureSample(char_index_texture, glyph_sampler, input.uv).r;

    var uv = input.uv;

    // Apply per-character animation to UV
    if (mode == 1) {
        // Wave: vertical sine offset
        let offset_y = sin(t * spd + idx * TAU * 2.0) * amt * 0.05;
        uv.y = uv.y + offset_y;
    } else if (mode == 3) {
        // Scatter: characters fly in from pseudo-random directions
        let progress = clamp(t * spd * 0.1 - idx, 0.0, 1.0);
        let smooth_p = progress * progress * (3.0 - 2.0 * progress);
        let rand_x = (hash(idx * 17.0) - 0.5) * 2.0;
        let rand_y = (hash(idx * 31.0) - 0.5) * 2.0;
        let fly_dist = amt * (1.0 - smooth_p);
        uv.x = uv.x + rand_x * fly_dist * 0.2;
        uv.y = uv.y + rand_y * fly_dist * 0.2;
    }

    let alpha = textureSample(glyph_texture, glyph_sampler, uv).r;
    let fg = vec3f(uniforms.fg_r, uniforms.fg_g, uniforms.fg_b);
    let bg = vec4f(uniforms.bg_r, uniforms.bg_g, uniforms.bg_b, uniforms.bg_a);

    var final_alpha = alpha;

    if (mode == 2) {
        // Typewriter: progressive reveal
        let reveal = smoothstep(idx - 0.02, idx, t * spd * 0.1);
        final_alpha = alpha * reveal;
    } else if (mode == 4) {
        // Fade: progressive alpha
        let fade = clamp(t * spd * 0.1 - idx + 0.5, 0.0, 1.0);
        final_alpha = alpha * fade;
    }

    return vec4f(mix(bg.rgb, fg, final_alpha), mix(bg.a, 1.0, final_alpha));
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms
// =============================================================================

struct RichTextUniforms {
    float fg_r, fg_g, fg_b;
    float time;
    float bg_r, bg_g, bg_b, bg_a;
    float anim_mode, anim_speed, anim_amount;
    float resolution_x, resolution_y;
    float _pad0, _pad1, _pad2;
};

// =============================================================================
// Rich Text Operator
// =============================================================================

struct RichText : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Rich Text";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> text {"text", ""};
    vivid::Param<vivid::FilePath>  font {"font", ""};
    vivid::Param<float> size         {"size",         0.15f, 0.01f, 1.0f};
    vivid::Param<int>   alignment    {"alignment",    1, {"Left", "Center", "Right"}};
    vivid::Param<float> line_height  {"line_height",  1.2f, 0.5f, 4.0f};
    vivid::Param<float> char_spacing {"char_spacing", 0.0f, -0.2f, 1.0f};
    vivid::Param<int>   anim_mode    {"anim_mode",    0, {"None", "Wave", "Typewriter", "Scatter", "Fade"}};
    vivid::Param<float> anim_speed   {"anim_speed",   1.0f, 0.0f, 10.0f};
    vivid::Param<float> anim_amount  {"anim_amount",  0.5f, 0.0f, 2.0f};
    vivid::Param<float> r    {"r",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> g    {"g",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> b    {"b",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_r {"bg_r", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_g {"bg_g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_b {"bg_b", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_a {"bg_a", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> x    {"x",    0.0f, -1.0f, 1.0f};
    vivid::Param<float> y    {"y",    0.0f, -1.0f, 1.0f};

    RichText() {
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
        out.push_back(&font);
        out.push_back(&size);
        out.push_back(&alignment);
        out.push_back(&line_height);
        out.push_back(&char_spacing);
        out.push_back(&anim_mode);
        out.push_back(&anim_speed);
        out.push_back(&anim_amount);
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
                std::fprintf(stderr, "[rich_text] lazy_init FAILED\n");
                return;
            }
        }

        std::string current_text = text.str_value;
        std::string current_font = font.str_value;
        uint32_t w = ctx->output_width;
        uint32_t h = ctx->output_height;

        // Check if glyph texture needs re-bake
        if (current_text != last_text_ || current_font != last_font_ ||
            size.value != last_size_ || alignment.int_value() != last_alignment_ ||
            line_height.value != last_line_height_ ||
            char_spacing.value != last_char_spacing_ ||
            w != last_w_ || h != last_h_) {

            // Reload font if changed
            if (current_font != last_font_) {
                font_loaded_ = false;
                font_data_.clear();
            }

            bake_glyphs(ctx, w, h, current_text);
            last_text_ = current_text;
            last_font_ = current_font;
            last_size_ = size.value;
            last_alignment_ = alignment.int_value();
            last_line_height_ = line_height.value;
            last_char_spacing_ = char_spacing.value;
            last_w_ = w;
            last_h_ = h;
        }

        // Update uniforms (animation params are uniform-only — cheap)
        RichTextUniforms u{};
        u.fg_r = r.value;
        u.fg_g = g.value;
        u.fg_b = b.value;
        u.time = static_cast<float>(ctx->time);
        u.bg_r = bg_r.value;
        u.bg_g = bg_g.value;
        u.bg_b = bg_b.value;
        u.bg_a = bg_a.value;
        u.anim_mode = static_cast<float>(anim_mode.int_value());
        u.anim_speed = anim_speed.value;
        u.anim_amount = anim_amount.value;
        u.resolution_x = static_cast<float>(w);
        u.resolution_y = static_cast<float>(h);

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Rich Text Pass");
    }

    ~RichText() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(glyph_tex_);
        vivid::gpu::release(glyph_view_);
        vivid::gpu::release(char_idx_tex_);
        vivid::gpu::release(char_idx_view_);
    }

private:
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroup       bind_group_    = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUBuffer          uniform_buf_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUTexture         glyph_tex_     = nullptr;
    WGPUTextureView     glyph_view_    = nullptr;
    WGPUTexture         char_idx_tex_  = nullptr;
    WGPUTextureView     char_idx_view_ = nullptr;
    WGPUDevice          device_        = nullptr;
    WGPUQueue           queue_         = nullptr;

    // Font data
    std::vector<unsigned char> font_data_;
    stbtt_fontinfo font_info_{};
    bool font_loaded_ = false;

    // Change-detection state
    std::string last_text_;
    std::string last_font_;
    float       last_size_ = -1.0f;
    int         last_alignment_ = -1;
    float       last_line_height_ = -1.0f;
    float       last_char_spacing_ = -999.0f;
    uint32_t    last_w_ = 0;
    uint32_t    last_h_ = 0;

    static std::string resolve_font_path(const std::string& custom_path) {
        // Try custom font first
        if (!custom_path.empty()) {
            if (FILE* f = std::fopen(custom_path.c_str(), "rb")) {
                std::fclose(f);
                return custom_path;
            }
        }

        // Fallback to JetBrainsMono
        const char* name = "JetBrainsMono-Regular.ttf";
        if (FILE* f = std::fopen(name, "rb")) { std::fclose(f); return name; }
#ifdef __APPLE__
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&resolve_font_path), &info) && info.dli_fname) {
            std::string dir(info.dli_fname);
            auto slash = dir.rfind('/');
            if (slash != std::string::npos) {
                dir.resize(slash + 1);
                std::string p = dir + name;
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
                p = dir + "../Resources/" + name;
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
            }
        }
#endif
        return name;
    }

    bool load_font() {
        if (font_loaded_) return true;

        std::string font_path = resolve_font_path(font.str_value);
        FILE* f = std::fopen(font_path.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "[rich_text] cannot open font: %s\n", font_path.c_str());
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
            std::fprintf(stderr, "[rich_text] stbtt_InitFont failed\n");
            return false;
        }
        font_loaded_ = true;
        return true;
    }

    // Split text on newlines
    static std::vector<std::string> split_lines(const std::string& s) {
        std::vector<std::string> lines;
        std::string current;
        for (char ch : s) {
            if (ch == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current += ch;
            }
        }
        lines.push_back(current);
        return lines;
    }

    // Measure width of a line in pixels
    float measure_line(const std::string& line, float stb_scale) {
        float w = 0;
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info_, &ascent, &descent, &line_gap);
        float em_width = ascent * stb_scale; // approximate em

        for (size_t i = 0; i < line.size(); ++i) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font_info_, line[i], &advance, &lsb);
            w += advance * stb_scale;
            // Extra tracking
            w += char_spacing.value * em_width;
            if (i + 1 < line.size()) {
                w += stbtt_GetCodepointKernAdvance(&font_info_, line[i], line[i+1]) * stb_scale;
            }
        }
        return w;
    }

    void bake_glyphs(const VividGpuContext* gpu, uint32_t w, uint32_t h,
                     const std::string& str) {
        if (!load_font()) return;

        float pixel_height = size.value * static_cast<float>(h);
        if (pixel_height < 4.0f) pixel_height = 4.0f;
        float stb_scale = stbtt_ScaleForPixelHeight(&font_info_, pixel_height);

        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info_, &ascent, &descent, &line_gap);
        float scaled_ascent = ascent * stb_scale;
        float em_width = ascent * stb_scale;
        float line_spacing = pixel_height * line_height.value;

        // Split text into lines
        auto lines = split_lines(str);

        // Count total characters (excluding newlines) for index normalization
        int total_chars = 0;
        for (const auto& line : lines)
            total_chars += static_cast<int>(line.size());
        if (total_chars == 0) total_chars = 1;

        // Compute per-line widths and x offsets based on alignment
        std::vector<float> line_widths(lines.size());
        std::vector<float> line_x_offsets(lines.size());
        for (size_t li = 0; li < lines.size(); ++li) {
            line_widths[li] = measure_line(lines[li], stb_scale);
            switch (alignment.int_value()) {
                case 0: // Left
                    line_x_offsets[li] = 0;
                    break;
                case 1: // Center
                    line_x_offsets[li] = (static_cast<float>(w) - line_widths[li]) * 0.5f;
                    break;
                case 2: // Right
                    line_x_offsets[li] = static_cast<float>(w) - line_widths[li];
                    break;
            }
            // Apply x/y position offset
            line_x_offsets[li] += x.value * static_cast<float>(w) * 0.5f;
        }

        // Vertical centering: total text block height
        float block_height = static_cast<float>(lines.size()) * line_spacing;
        float base_y = (static_cast<float>(h) - block_height) * 0.5f + scaled_ascent
                      - y.value * static_cast<float>(h) * 0.5f;

        // WebGPU requires bytesPerRow aligned to 256
        uint32_t row_stride = (w + 255) & ~255u;

        // Glyph staging buffer (R8)
        std::vector<unsigned char> glyph_staging(row_stride * h, 0);
        // Character index staging buffer (R32Float) — stores idx/total for each pixel
        // bytesPerRow for R32Float must also be 256-aligned
        uint32_t idx_bytes_per_row = w * 4;
        uint32_t idx_row_stride = (idx_bytes_per_row + 255) & ~255u;
        uint32_t idx_pixels_per_row = idx_row_stride / 4;
        std::vector<float> idx_staging(idx_pixels_per_row * h, 0.0f);

        int char_counter = 0;

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            float pen_x = line_x_offsets[li];
            float pen_y = base_y + static_cast<float>(li) * line_spacing;

            for (size_t ci = 0; ci < line.size(); ++ci) {
                float char_idx_normalized = static_cast<float>(char_counter) /
                                           static_cast<float>(total_chars);
                char_counter++;

                int x0, y0, x1, y1;
                stbtt_GetCodepointBitmapBox(&font_info_, line[ci], stb_scale, stb_scale,
                                           &x0, &y0, &x1, &y1);
                int gw = x1 - x0;
                int gh = y1 - y0;

                int px = static_cast<int>(pen_x) + x0;
                int py = static_cast<int>(pen_y) + y0;

                if (gw > 0 && gh > 0) {
                    int clip_x0 = px < 0 ? -px : 0;
                    int clip_y0 = py < 0 ? -py : 0;
                    int clip_x1 = (px + gw > static_cast<int>(w)) ? static_cast<int>(w) - px : gw;
                    int clip_y1 = (py + gh > static_cast<int>(h)) ? static_cast<int>(h) - py : gh;

                    if (clip_x0 < clip_x1 && clip_y0 < clip_y1) {
                        std::vector<unsigned char> glyph_buf(gw * gh, 0);
                        stbtt_MakeCodepointBitmap(&font_info_, glyph_buf.data(),
                                                   gw, gh, gw, stb_scale, stb_scale, line[ci]);

                        for (int gy = clip_y0; gy < clip_y1; ++gy) {
                            for (int gx = clip_x0; gx < clip_x1; ++gx) {
                                int dst_x = px + gx;
                                int dst_y = py + gy;
                                unsigned char val = glyph_buf[gy * gw + gx];

                                // Glyph texture (additive blend)
                                unsigned char& dst = glyph_staging[dst_y * row_stride + dst_x];
                                int sum = static_cast<int>(dst) + static_cast<int>(val);
                                dst = static_cast<unsigned char>(sum > 255 ? 255 : sum);

                                // Character index texture — write index if this pixel has coverage
                                if (val > 0) {
                                    idx_staging[dst_y * idx_pixels_per_row + dst_x] =
                                        char_idx_normalized;
                                }
                            }
                        }
                    }
                }

                int advance, lsb;
                stbtt_GetCodepointHMetrics(&font_info_, line[ci], &advance, &lsb);
                pen_x += advance * stb_scale;
                pen_x += char_spacing.value * em_width;
                if (ci + 1 < line.size()) {
                    pen_x += stbtt_GetCodepointKernAdvance(&font_info_, line[ci], line[ci+1]) * stb_scale;
                }
            }
        }

        // Upload glyph texture
        bool size_changed = (last_w_ != w || last_h_ != h);
        if (glyph_tex_ && size_changed) {
            vivid::gpu::release(glyph_tex_);
            vivid::gpu::release(glyph_view_);
            glyph_tex_ = nullptr;
            glyph_view_ = nullptr;
        }
        if (char_idx_tex_ && size_changed) {
            vivid::gpu::release(char_idx_tex_);
            vivid::gpu::release(char_idx_view_);
            char_idx_tex_ = nullptr;
            char_idx_view_ = nullptr;
        }

        if (!glyph_tex_) {
            WGPUTextureDescriptor tex_desc{};
            tex_desc.label = vivid_sv("Rich Text Glyph Texture");
            tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            tex_desc.dimension = WGPUTextureDimension_2D;
            tex_desc.size = {w, h, 1};
            tex_desc.format = WGPUTextureFormat_R8Unorm;
            tex_desc.mipLevelCount = 1;
            tex_desc.sampleCount = 1;
            glyph_tex_ = wgpuDeviceCreateTexture(device_, &tex_desc);

            WGPUTextureViewDescriptor view_desc{};
            view_desc.label = vivid_sv("Rich Text Glyph View");
            view_desc.format = WGPUTextureFormat_R8Unorm;
            view_desc.dimension = WGPUTextureViewDimension_2D;
            view_desc.mipLevelCount = 1;
            view_desc.arrayLayerCount = 1;
            glyph_view_ = wgpuTextureCreateView(glyph_tex_, &view_desc);
        }

        if (!char_idx_tex_) {
            WGPUTextureDescriptor tex_desc{};
            tex_desc.label = vivid_sv("Rich Text CharIdx Texture");
            tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            tex_desc.dimension = WGPUTextureDimension_2D;
            tex_desc.size = {w, h, 1};
            tex_desc.format = WGPUTextureFormat_R32Float;
            tex_desc.mipLevelCount = 1;
            tex_desc.sampleCount = 1;
            char_idx_tex_ = wgpuDeviceCreateTexture(device_, &tex_desc);

            WGPUTextureViewDescriptor view_desc{};
            view_desc.label = vivid_sv("Rich Text CharIdx View");
            view_desc.format = WGPUTextureFormat_R32Float;
            view_desc.dimension = WGPUTextureViewDimension_2D;
            view_desc.mipLevelCount = 1;
            view_desc.arrayLayerCount = 1;
            char_idx_view_ = wgpuTextureCreateView(char_idx_tex_, &view_desc);

            rebuild_bind_group();
        }

        // Write glyph staging data
        {
            WGPUTexelCopyBufferLayout layout{};
            layout.offset = 0;
            layout.bytesPerRow = row_stride;
            layout.rowsPerImage = h;

            WGPUTexelCopyTextureInfo dest{};
            dest.texture = glyph_tex_;
            dest.mipLevel = 0;
            dest.origin = {0, 0, 0};

            WGPUExtent3D extent = {w, h, 1};
            wgpuQueueWriteTexture(queue_, &dest, glyph_staging.data(),
                                  glyph_staging.size(), &layout, &extent);
        }

        // Write character index staging data
        {
            WGPUTexelCopyBufferLayout layout{};
            layout.offset = 0;
            layout.bytesPerRow = idx_row_stride;
            layout.rowsPerImage = h;

            WGPUTexelCopyTextureInfo dest{};
            dest.texture = char_idx_tex_;
            dest.mipLevel = 0;
            dest.origin = {0, 0, 0};

            WGPUExtent3D extent = {w, h, 1};
            wgpuQueueWriteTexture(queue_, &dest, idx_staging.data(),
                                  idx_staging.size() * sizeof(float), &layout, &extent);
        }
    }

    void rebuild_bind_group() {
        vivid::gpu::release(bind_group_);

        WGPUBindGroupEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_;
        entries[0].offset  = 0;
        entries[0].size    = sizeof(RichTextUniforms);

        entries[1].binding = 1;
        entries[1].sampler = sampler_;

        entries[2].binding = 2;
        entries[2].textureView = glyph_view_;

        entries[3].binding = 3;
        entries[3].textureView = char_idx_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Rich Text Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 4;
        bg_desc.entries = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        device_ = gpu->device;
        queue_  = gpu->queue;

        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kRichTextFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Rich Text Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(RichTextUniforms),
                                                          "Rich Text Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Rich Text Sampler");

        // Bind group layout: uniform (0), sampler (1), glyph texture (2), char index texture (3)
        WGPUBindGroupLayoutEntry bgl_entries[4]{};

        bgl_entries[0].binding = 0;
        bgl_entries[0].visibility = WGPUShaderStage_Fragment;
        bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entries[0].buffer.minBindingSize = sizeof(RichTextUniforms);

        bgl_entries[1].binding = 1;
        bgl_entries[1].visibility = WGPUShaderStage_Fragment;
        bgl_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        bgl_entries[2].binding = 2;
        bgl_entries[2].visibility = WGPUShaderStage_Fragment;
        bgl_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        bgl_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        bgl_entries[3].binding = 3;
        bgl_entries[3].visibility = WGPUShaderStage_Fragment;
        bgl_entries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        bgl_entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Rich Text BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries = bgl_entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Rich Text Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Create 1x1 placeholder textures
        auto make_placeholder = [&](WGPUTextureFormat fmt, const char* label,
                                    WGPUTexture& tex, WGPUTextureView& view) {
            WGPUTextureDescriptor tex_desc{};
            tex_desc.label = vivid_sv(label);
            tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            tex_desc.dimension = WGPUTextureDimension_2D;
            tex_desc.size = {1, 1, 1};
            tex_desc.format = fmt;
            tex_desc.mipLevelCount = 1;
            tex_desc.sampleCount = 1;
            tex = wgpuDeviceCreateTexture(gpu->device, &tex_desc);

            WGPUTextureViewDescriptor view_desc{};
            view_desc.label = vivid_sv(label);
            view_desc.format = fmt;
            view_desc.dimension = WGPUTextureViewDimension_2D;
            view_desc.mipLevelCount = 1;
            view_desc.arrayLayerCount = 1;
            view = wgpuTextureCreateView(tex, &view_desc);
        };

        make_placeholder(WGPUTextureFormat_R8Unorm, "Rich Text Glyph Placeholder",
                         glyph_tex_, glyph_view_);
        make_placeholder(WGPUTextureFormat_R32Float, "Rich Text CharIdx Placeholder",
                         char_idx_tex_, char_idx_view_);

        rebuild_bind_group();

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                 gpu->output_format, "Rich Text Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(RichText)
