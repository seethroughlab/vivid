#ifndef VIVID_RUNTIME_TEXT_RENDERER_H
#define VIVID_RUNTIME_TEXT_RENDERER_H

#include <webgpu/webgpu.h>
#include <cstdint>
#include <vector>
#include <string>

namespace vivid {

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

class TextRenderer {
public:
    bool init(WGPUDevice device, WGPUTextureFormat surface_format,
              const char* font_path, float font_size, float dpi_scale = 1.0f);
    void shutdown();

    void draw_text(float x, float y, const char* text, float r, float g, float b, float a = 1.0f, float scale = 1.0f);
    void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
    float text_width(const char* text, float scale = 1.0f) const;
    float line_height() const { return line_height_; }

    // Flush all accumulated quads in a render pass on top of the given surface view.
    // Uses loadOp=Load to composite over existing content.
    void flush(WGPUCommandEncoder encoder, WGPUTextureView surface_view,
               uint32_t surface_width, uint32_t surface_height);

private:
    void push_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   float r, float g, float b, float a);

    WGPUDevice device_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUTexture atlas_tex_ = nullptr;
    WGPUTextureView atlas_view_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBuffer vertex_bufs_[2]{};   // double-buffered vertex buffers
    int buf_idx_ = 0;               // alternates 0/1 each flush

    GlyphInfo glyphs_[128]{}; // ASCII 0-127 (only 32-126 used)
    float line_height_ = 0.0f;
    float font_size_ = 0.0f;

    static constexpr uint32_t kAtlasWidth = 1024;
    static constexpr uint32_t kAtlasHeight = 1024;
    static constexpr uint32_t kMaxVertices = 6 * 4096; // 4096 quads

    std::vector<TextVertex> vertices_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_TEXT_RENDERER_H
