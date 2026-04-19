#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// =============================================================================
// Text2D — emit a VividDrawable2D of type TEXT (glyph run on an atlas)
// =============================================================================

/**
 * @brief Emit a string of text as a glyph-run 2D drawable.
 *
 * Bakes the requested font at its requested raster size into a persistent
 * glyph atlas (ASCII 32–126), then on each frame that the text or layout
 * parameters change, walks the string and builds a per-glyph GlyphInstance2D
 * buffer. The emitted drawable carries the atlas view + the glyph buffer;
 * Render2D's TEXT pipeline variant draws one quad per glyph in a single
 * DrawIndexed call.
 *
 * MVP: single-line, left-aligned (after the `anchor` offset), no kerning.
 *
 * @param text        The text string to render.
 * @param font_size   Line height in NDC units.
 * @param position_x / position_y  Anchor position in NDC.
 * @param anchor      Which corner of the text box sits at (position_x, position_y).
 *                    0 = TopLeft (default), 4 = Center, 8 = BottomRight.
 * @param r / g / b / a  Text colour (applied uniformly to all glyphs).
 *
 * @tip Default font is JetBrainsMono-Regular.ttf (monospace) — shipped with vivid.
 * @tip Change `text` via the inspector's string widget or MCP `set_string_param`.
 * @recipe Text2D -> Render2D -> video_out
 * @common_companions Render2D, DrawableMerge, Transform2D
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Render2D, Shape2D, DrawableMerge
 */
struct Text2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName               = "Text2D";
    static constexpr bool kTimeDependent             = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;

    vivid::Param<vivid::TextValue> text {"text", "vivid"};
    vivid::Param<float> font_size  {"font_size",  0.12f, 0.01f, 1.0f};
    vivid::Param<float> position_x {"position_x", -0.6f, -2.0f, 2.0f};
    vivid::Param<float> position_y {"position_y",  0.0f, -2.0f, 2.0f};
    vivid::Param<int>   anchor     {"anchor",     0, {
        "TopLeft", "Top", "TopRight",
        "Left",    "Center",  "Right",
        "BottomLeft", "Bottom",  "BottomRight"}};

    vivid::Param<float> r {"r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    Text2D() {
        vivid::description(text, "The text string to render.");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(text,       "Text");
        vivid::param_group(font_size,  "Text");
        vivid::param_group(position_x, "Transform");
        vivid::param_group(position_y, "Transform");
        vivid::param_group(anchor,     "Transform");
        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");
        out.push_back(&text);
        out.push_back(&font_size);
        out.push_back(&position_x);
        out.push_back(&position_y);
        out.push_back(&anchor);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    ~Text2D() override {
        vivid::gpu::release(glyph_buffer_);
        vivid::gpu::release(atlas_view_);
        vivid::gpu::release(atlas_tex_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!ensure_atlas(ctx)) return;

        const std::string& t = text.str_value;
        const float size_ndc = font_size.value;

        // Rebuild glyph run if text, size, position, colour, or anchor changed.
        bool need_rebuild =
            (t != last_text_) ||
            (size_ndc != last_size_) ||
            (position_x.value != last_px_) ||
            (position_y.value != last_py_) ||
            (anchor.int_value() != last_anchor_) ||
            (r.value != last_r_) || (g.value != last_g_) ||
            (b.value != last_b_) || (a.value != last_a_);

        if (need_rebuild) {
            rebuild_glyph_run(ctx, t, size_ndc);
            last_text_   = t;
            last_size_   = size_ndc;
            last_px_     = position_x.value;
            last_py_     = position_y.value;
            last_anchor_ = anchor.int_value();
            last_r_      = r.value;
            last_g_      = g.value;
            last_b_      = b.value;
            last_a_      = a.value;
        }

        vivid::gpu::drawable_identity(output_);
        vivid::gpu::drawable_text(output_, atlas_view_, glyph_buffer_,
                                  static_cast<uint32_t>(last_glyph_count_),
                                  vivid::gpu::VIVID_BLEND_ALPHA);
        ctx->custom_outputs[0] = &output_;
    }

private:
    // Atlas dimensions (shared across all Text2D instances conceptually,
    // but we bake one atlas per operator — simpler ownership).
    static constexpr uint32_t kAtlasW = 1024;
    static constexpr uint32_t kAtlasH = 1024;
    // Rasterise at 64px line height; glyphs are filtered down to `font_size`
    // at draw time via the per-quad transform. Higher = sharper at large
    // sizes but larger atlas; 64 is a comfortable middle.
    static constexpr float kRasterPx = 64.0f;
    static constexpr int kFirstGlyph = 32;
    static constexpr int kLastGlyph  = 126;
    static constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;

    struct GlyphMetrics {
        float u0, v0, u1, v1;  // atlas UV
        float x0, y0;          // bitmap offset (in raster pixels) from pen
        float w, h;            // bitmap pixel size
        float advance;         // horizontal advance (raster pixels)
    };

    // --- State ---
    std::string       font_data_path_;   // resolved absolute path (or best effort)
    std::vector<unsigned char> font_data_;
    stbtt_fontinfo    font_info_{};
    bool              font_loaded_  = false;
    bool              atlas_built_  = false;
    GlyphMetrics      metrics_[kGlyphCount]{};
    float             baseline_px_  = 0.0f;  // ascent in raster pixels
    WGPUTexture       atlas_tex_    = nullptr;
    WGPUTextureView   atlas_view_   = nullptr;

    WGPUBuffer        glyph_buffer_        = nullptr;
    uint32_t          glyph_buffer_cap_    = 0;   // capacity in glyphs
    uint32_t          last_glyph_count_    = 0;

    // Cache keys for rebuilding the glyph run.
    std::string last_text_;
    float last_size_   = -1.0f;
    float last_px_     =  0.0f;
    float last_py_     =  0.0f;
    int   last_anchor_ = -1;
    float last_r_ = -1.0f, last_g_ = -1.0f, last_b_ = -1.0f, last_a_ = -1.0f;

    vivid::gpu::VividDrawable2D output_{};

    // -----------------------------------------------------------------------

    static std::string resolve_font_path() {
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
                p = dir + "../Resources/fonts/" + name;
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
            }
        }
#endif
        return name;
    }

    bool load_font() {
        if (font_loaded_) return true;
        std::string p = resolve_font_path();
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) { std::fprintf(stderr, "[text_label] cannot open %s\n", p.c_str()); return false; }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0) { std::fclose(f); return false; }
        font_data_.resize(sz);
        size_t rd = std::fread(font_data_.data(), 1, sz, f);
        std::fclose(f);
        if (static_cast<long>(rd) != sz) return false;
        if (!stbtt_InitFont(&font_info_, font_data_.data(), 0)) {
            std::fprintf(stderr, "[text_label] stbtt_InitFont failed\n");
            return false;
        }
        font_loaded_ = true;
        return true;
    }

    bool ensure_atlas(const VividGpuContext* ctx) {
        if (atlas_built_) return true;
        if (!load_font()) return false;

        const float scale = stbtt_ScaleForPixelHeight(&font_info_, kRasterPx);

        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info_, &ascent, &descent, &line_gap);
        baseline_px_ = ascent * scale;

        // Row-align to 256 bytes for WebGPU upload.
        const uint32_t row_stride = (kAtlasW + 255u) & ~255u;
        std::vector<unsigned char> bitmap(row_stride * kAtlasH, 0);

        uint32_t pen_x = 1, pen_y = 1;  // 1-pixel border for safety
        uint32_t row_height = 0;

        for (int c = kFirstGlyph; c <= kLastGlyph; ++c) {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font_info_, c, scale, scale, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0;
            const int gh = y1 - y0;

            if (pen_x + gw + 1 >= kAtlasW) {
                pen_x = 1;
                pen_y += row_height + 2;
                row_height = 0;
            }
            if (pen_y + gh + 1 >= kAtlasH) {
                std::fprintf(stderr, "[text_label] atlas overflow at char %d\n", c);
                break;
            }

            if (gw > 0 && gh > 0) {
                stbtt_MakeCodepointBitmap(&font_info_,
                                          bitmap.data() + pen_y * row_stride + pen_x,
                                          gw, gh, static_cast<int>(row_stride),
                                          scale, scale, c);
            }

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font_info_, c, &advance, &lsb);

            auto& m = metrics_[c - kFirstGlyph];
            m.u0 = static_cast<float>(pen_x)      / static_cast<float>(kAtlasW);
            m.v0 = static_cast<float>(pen_y)      / static_cast<float>(kAtlasH);
            m.u1 = static_cast<float>(pen_x + gw) / static_cast<float>(kAtlasW);
            m.v1 = static_cast<float>(pen_y + gh) / static_cast<float>(kAtlasH);
            m.x0 = static_cast<float>(x0);
            m.y0 = static_cast<float>(y0);
            m.w  = static_cast<float>(gw);
            m.h  = static_cast<float>(gh);
            m.advance = advance * scale;

            pen_x += static_cast<uint32_t>(gw) + 2;
            if (static_cast<uint32_t>(gh) > row_height) row_height = gh;
        }

        // Create GPU texture and upload.
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Text2D Atlas");
        td.dimension     = WGPUTextureDimension_2D;
        td.size.width    = kAtlasW;
        td.size.height   = kAtlasH;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.format        = WGPUTextureFormat_R8Unorm;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        atlas_tex_       = wgpuDeviceCreateTexture(ctx->device, &td);
        if (!atlas_tex_) return false;

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = atlas_tex_;
        dst.aspect  = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset       = 0;
        layout.bytesPerRow  = row_stride;
        layout.rowsPerImage = kAtlasH;

        WGPUExtent3D ext{};
        ext.width  = kAtlasW;
        ext.height = kAtlasH;
        ext.depthOrArrayLayers = 1;

        wgpuQueueWriteTexture(ctx->queue, &dst, bitmap.data(), bitmap.size(),
                               &layout, &ext);

        WGPUTextureViewDescriptor vd{};
        vd.label  = vivid_sv("Text2D Atlas View");
        vd.format = WGPUTextureFormat_R8Unorm;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        atlas_view_ = wgpuTextureCreateView(atlas_tex_, &vd);

        atlas_built_ = (atlas_view_ != nullptr);
        return atlas_built_;
    }

    void ensure_glyph_capacity(const VividGpuContext* ctx, uint32_t n) {
        if (n == 0) n = 1;
        if (glyph_buffer_ && glyph_buffer_cap_ >= n) return;
        vivid::gpu::release(glyph_buffer_);
        glyph_buffer_cap_ = n + 32;  // grow with slack
        WGPUBufferDescriptor bd{};
        bd.label = vivid_sv("Text2D Glyph Buffer");
        bd.size  = glyph_buffer_cap_ * sizeof(vivid::gpu::GlyphInstance2D);
        bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        glyph_buffer_ = wgpuDeviceCreateBuffer(ctx->device, &bd);
    }

    void rebuild_glyph_run(const VividGpuContext* ctx,
                            const std::string& str, float size_ndc) {
        if (str.empty()) { last_glyph_count_ = 0; return; }

        // NDC-per-raster-pixel scale factor.
        const float line_px  = kRasterPx;
        const float ndc_per_px = size_ndc / line_px;

        // First pass: measure total advance (for anchor offsets).
        float total_adv_px = 0.0f;
        for (size_t i = 0; i < str.size(); ++i) {
            int c = static_cast<unsigned char>(str[i]);
            if (c < kFirstGlyph || c > kLastGlyph) c = '?';
            total_adv_px += metrics_[c - kFirstGlyph].advance;
        }
        const float line_h_px = line_px;  // approximate — raster size is 1 line

        // Compute pen origin from anchor + position.
        //
        // NDC conventions: +Y is UP. Raster conventions: +Y is DOWN. Glyph
        // metrics (y0, y1) are in raster pixels; a letter like "A" has y0
        // negative (top above baseline) and y1 near zero. To map raster →
        // NDC we negate the y component.
        //
        // Anchor places a chosen corner of the text's ascent-box at
        // (position_x, position_y). TopLeft (row=0, col=0) means the text
        // reads downward and right from that point.
        const int ai = last_anchor_ >= 0 ? last_anchor_ : anchor.int_value();
        const int col = ai % 3;
        const int row = ai / 3;
        const float total_w_ndc = total_adv_px * ndc_per_px;
        const float line_h_ndc  = line_h_px    * ndc_per_px;
        const float ascent_ndc  = baseline_px_ * ndc_per_px;

        // x: col 0 left → x = position_x; col 1 centre → shift by -w/2; col 2 right → shift by -w.
        float pen_x_origin_ndc = position_x.value;
        if (col == 1) pen_x_origin_ndc -= 0.5f * total_w_ndc;
        else if (col == 2) pen_x_origin_ndc -= total_w_ndc;

        // y: baseline depends on which corner of the line box sits at position_y.
        // row 0 (top)    → line top at position_y, baseline = position_y - ascent
        // row 1 (middle) → line centre at position_y, baseline = position_y - ascent + line_h/2
        // row 2 (bottom) → line bottom at position_y, baseline = position_y - ascent + line_h
        float baseline_y_ndc = position_y.value - ascent_ndc;
        if (row == 1)      baseline_y_ndc += 0.5f * line_h_ndc;
        else if (row == 2) baseline_y_ndc += line_h_ndc;

        std::vector<vivid::gpu::GlyphInstance2D> glyphs;
        glyphs.reserve(str.size());

        float pen_x_px = 0.0f;

        for (size_t i = 0; i < str.size(); ++i) {
            int c = static_cast<unsigned char>(str[i]);
            if (c < kFirstGlyph || c > kLastGlyph) c = '?';
            const auto& m = metrics_[c - kFirstGlyph];

            if (m.w > 0.0f && m.h > 0.0f) {
                vivid::gpu::GlyphInstance2D gi{};
                const float half_w = 0.5f * m.w * ndc_per_px;
                const float half_h = 0.5f * m.h * ndc_per_px;
                // Glyph centre in NDC. y0 is in raster pixels (DOWN-positive),
                // so subtract it to move in the +NDC-Y direction. The glyph's
                // vertical extent in NDC is [baseline - y1*nps, baseline - y0*nps],
                // i.e. bottom = baseline - y1*nps, top = baseline - y0*nps,
                // centre = baseline - (y0 + h/2)*nps.
                const float cx = pen_x_origin_ndc + (pen_x_px + m.x0) * ndc_per_px + half_w;
                const float cy = baseline_y_ndc - (m.y0 * ndc_per_px + half_h);
                // Column-major mat3x2: x-scale (half_w), y-scale (+half_h, not
                // negated — local +Y = visual top, UV atlas v=0 also = top).
                gi.transform[0] = half_w;  gi.transform[1] = 0.0f;
                gi.transform[2] = 0.0f;    gi.transform[3] = half_h;
                gi.transform[4] = cx;      gi.transform[5] = cy;

                gi.uv_rect[0] = m.u0;
                gi.uv_rect[1] = m.v0;
                gi.uv_rect[2] = m.u1;
                gi.uv_rect[3] = m.v1;

                gi.color[0] = r.value;
                gi.color[1] = g.value;
                gi.color[2] = b.value;
                gi.color[3] = a.value;

                glyphs.push_back(gi);
            }

            pen_x_px += m.advance;
        }

        last_glyph_count_ = static_cast<uint32_t>(glyphs.size());
        if (last_glyph_count_ == 0) return;

        ensure_glyph_capacity(ctx, last_glyph_count_);
        wgpuQueueWriteBuffer(ctx->queue, glyph_buffer_, 0,
                              glyphs.data(),
                              glyphs.size() * sizeof(vivid::gpu::GlyphInstance2D));
    }
};

VIVID_REGISTER(Text2D)
