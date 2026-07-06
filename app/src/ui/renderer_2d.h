#pragma once

#include <webgpu/webgpu.h>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

namespace vivid::ui {

namespace detail {

struct PhysicalScissorRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Internal test seam: clip rects are stored in physical pixels, while vertex
// space remains logical. Clamp against the actual framebuffer bounds rather
// than inferring them from logical size * dpi.
inline bool clamp_physical_scissor_rect(float sx, float sy, float sw, float sh,
                                        uint32_t framebuffer_width,
                                        uint32_t framebuffer_height,
                                        PhysicalScissorRect* out) {
    if (!out || framebuffer_width == 0 || framebuffer_height == 0) return false;
    float x0 = std::max(0.0f, sx);
    float y0 = std::max(0.0f, sy);
    float x1 = std::min(static_cast<float>(framebuffer_width), sx + std::max(0.0f, sw));
    float y1 = std::min(static_cast<float>(framebuffer_height), sy + std::max(0.0f, sh));
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = static_cast<uint32_t>(x0);
    out->y = static_cast<uint32_t>(y0);
    out->width = static_cast<uint32_t>(x1 - x0);
    out->height = static_cast<uint32_t>(y1 - y0);
    return out->width > 0 && out->height > 0;
}

} // namespace detail

struct TextVertex {
    float x, y;     // position (pixels)
    float u, v;     // UV into glyph atlas
    float r, g, b, a; // color
};

struct GlyphInfo {
    float u0, v0, u1, v1;  // UV coords in atlas
    float x0, y0, x1, y1;  // pixel offset from baseline
    float advance;          // horizontal advance
};

class Renderer2D {
public:
    ~Renderer2D() { shutdown(); }
    Renderer2D() = default;
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    bool init(WGPUDevice device, WGPUTextureFormat surface_format,
              const char* font_path, float font_size, float dpi_scale = 1.0f,
              const uint32_t* extra_codepoints = nullptr, size_t extra_count = 0);
    void shutdown();

    void draw_text(float x, float y, const char* text, float r, float g, float b, float a = 1.0f, float scale = 1.0f);
    void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
    void draw_rounded_rect(float x, float y, float w, float h, float radius,
                           float r, float g, float b, float a = 1.0f);
    void draw_line(float x1, float y1, float x2, float y2, float thickness,
                   float r, float g, float b, float a = 1.0f);
    void draw_tri(float x0, float y0, float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a = 1.0f);
    void draw_arc(float cx, float cy, float radius,
                  float start_angle, float end_angle,
                  float thickness, int segments,
                  float r, float g, float b, float a = 1.0f);
    void draw_circle(float cx, float cy, float radius, float thickness,
                     float r, float g, float b, float a = 1.0f);
    void draw_dashed_line(float x1, float y1, float x2, float y2,
                          float thickness, float dash_len, float gap_len,
                          float r, float g, float b, float a = 1.0f);
    void draw_polyline(const float* xs, const float* ys, uint32_t count,
                       float thickness, float r, float g, float b, float a = 1.0f);
    // Full-colour textured quad at (x,y,w,h) sampling `tex` (full 0..1 UV), tinted
    // by (r,g,b,a). Respects the view transform + clip rect. The view must remain
    // valid until the next flush(); blends as premultiplied-by-color alpha.
    void draw_texture(float x, float y, float w, float h, WGPUTextureView tex,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    float text_width(const char* text, float scale = 1.0f) const;
    float line_height() const { return line_height_; }

    std::vector<std::string> wrap_text(const char* text, float max_width, float scale = 1.0f) const;
    float draw_text_wrapped(float x, float y, const char* text, float max_width,
                            float r, float g, float b, float a = 1.0f, float scale = 1.0f);

    // Clip rect stack — content drawn between push/pop is scissored to (x,y,w,h)
    // in logical pixel coordinates. Calls finalize the current vertex batch.
    void push_clip_rect(float x, float y, float w, float h);
    void pop_clip_rect();

    // CPU view transform applied to every subsequent vertex: screen = world*scale
    // + (ox,oy). Used for pan/zoom of node-graph content; call set_transform(0,0,1)
    // to return to identity before drawing chrome. Does not affect clip rects.
    void set_transform(float ox, float oy, float scale) { tf_ox_ = ox; tf_oy_ = oy; tf_scale_ = scale; }

    // Flush all accumulated quads in a render pass on top of the given surface
    // view. Logical dimensions drive vertex-space uniforms; framebuffer
    // dimensions drive physical scissor clamping.
    void flush(WGPUCommandEncoder encoder, WGPUTextureView surface_view,
               uint32_t logical_width, uint32_t logical_height,
               uint32_t framebuffer_width, uint32_t framebuffer_height);

    // Reset the ring-buffer write offset. Call once per frame before multiple
    // flush() calls on the same command encoder (e.g. thumbnail rendering loop).
    void reset_ring();

    bool has_pending_draws() const { return !vertices_.empty(); }
    size_t pending_vertex_count() const { return vertices_.size(); }

private:
    void push_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   float r, float g, float b, float a);
    void push_tri(float x0, float y0, float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a);
    void finalize_batch(WGPUTextureView tex = nullptr);
    static uint32_t decode_utf8(const char*& p);
    const GlyphInfo* lookup_glyph(uint32_t codepoint) const;

    WGPUDevice device_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPURenderPipeline tex_pipeline_ = nullptr;   // full-colour textured quads
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer uniform_buf_ = nullptr;     // persistent 8-byte uniform (screen_size)
    WGPUBindGroup bind_group_ = nullptr;   // persistent bind group (uniforms + sampler + atlas)
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUTexture atlas_tex_ = nullptr;
    WGPUTextureView atlas_view_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBuffer vertex_bufs_[2]{};   // double-buffered vertex buffers
    int buf_idx_ = 0;               // current buffer index (flips on overflow)
    size_t ring_byte_offset_ = 0;   // running write offset within current buffer

    GlyphInfo glyphs_[128]{}; // ASCII 0-127 (only 32-126 used)
    std::unordered_map<uint32_t, GlyphInfo> extra_glyphs_;
    float line_height_ = 0.0f;
    float font_size_ = 0.0f;
    float solid_u_ = 0.0f;   // UV center of the solid-white 2x2 atlas block
    float solid_v_ = 0.0f;

    static constexpr uint32_t kAtlasWidth = 1024;
    static constexpr uint32_t kAtlasHeight = 1024;
    static constexpr uint32_t kMaxVertices = 6 * 21845; // 21845 quads

    std::vector<TextVertex> vertices_;

    // Clip rect batching: each batch records a vertex range + optional scissor rect
    struct DrawBatch {
        uint32_t start = 0, count = 0;
        bool has_scissor = false;
        float sx = 0, sy = 0, sw = 0, sh = 0; // physical pixel coords
        WGPUTextureView tex = nullptr;        // null = glyph atlas (alpha pipeline)
    };
    std::vector<DrawBatch> batches_;
    std::vector<DrawBatch> clip_stack_; // push/pop stack (reuses DrawBatch for rect storage)
    float dpi_scale_ = 1.0f;
    uint32_t overflow_count_ = 0; // quads dropped this frame due to full vertex buffer
    float tf_ox_ = 0.0f, tf_oy_ = 0.0f, tf_scale_ = 1.0f;  // CPU view transform
};


} // namespace vivid::ui
