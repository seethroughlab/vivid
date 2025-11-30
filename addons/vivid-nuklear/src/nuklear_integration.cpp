// Nuklear configuration - must be before including nuklear.h
// Enable STB implementations for font baking (Nuklear has embedded STB headers)
#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"

#include <vivid/nuklear/nuklear_integration.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vivid {
namespace nuklear {

// Vertex structure for Nuklear output
struct NkVertex {
    float position[2];
    float uv[2];
    nk_byte color[4];
};

// Software rasterizer helper functions
namespace {

inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float edgeFunction(float ax, float ay, float bx, float by, float cx, float cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

// Blend source over destination (premultiplied alpha)
inline void blendPixel(unsigned char* dst, const unsigned char* src) {
    float sa = src[3] / 255.0f;
    float da = dst[3] / 255.0f;
    float oa = sa + da * (1.0f - sa);

    if (oa > 0.0f) {
        dst[0] = (unsigned char)((src[0] * sa + dst[0] * da * (1.0f - sa)) / oa);
        dst[1] = (unsigned char)((src[1] * sa + dst[1] * da * (1.0f - sa)) / oa);
        dst[2] = (unsigned char)((src[2] * sa + dst[2] * da * (1.0f - sa)) / oa);
        dst[3] = (unsigned char)(oa * 255.0f);
    }
}

// Sample texture with bilinear filtering
inline void sampleTexture(const unsigned char* tex, int texW, int texH,
                          float u, float v, unsigned char* out) {
    // Clamp UV coordinates
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    float x = u * (texW - 1);
    float y = v * (texH - 1);

    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = std::min(x0 + 1, texW - 1);
    int y1 = std::min(y0 + 1, texH - 1);

    float fx = x - x0;
    float fy = y - y0;

    const unsigned char* p00 = tex + (y0 * texW + x0) * 4;
    const unsigned char* p10 = tex + (y0 * texW + x1) * 4;
    const unsigned char* p01 = tex + (y1 * texW + x0) * 4;
    const unsigned char* p11 = tex + (y1 * texW + x1) * 4;

    for (int c = 0; c < 4; c++) {
        float v0 = p00[c] * (1 - fx) + p10[c] * fx;
        float v1 = p01[c] * (1 - fx) + p11[c] * fx;
        out[c] = (unsigned char)(v0 * (1 - fy) + v1 * fy);
    }
}

} // anonymous namespace

NuklearUI::NuklearUI() = default;

NuklearUI::~NuklearUI() {
    if (ctx_) {
        nk_free(ctx_);
        delete ctx_;
    }
    if (atlas_) {
        nk_font_atlas_clear(atlas_);
        delete atlas_;
    }
}

void NuklearUI::init(int width, int height, float fontSize) {
    if (initialized_) return;

    width_ = width;
    height_ = height;

    // Allocate pixel buffer (RGBA)
    pixels_.resize(width * height * 4, 0);

    // Initialize font atlas - must zero before init to avoid garbage state
    atlas_ = new nk_font_atlas;
    memset(atlas_, 0, sizeof(nk_font_atlas));
    nk_font_atlas_init_default(atlas_);
    nk_font_atlas_begin(atlas_);

    // Use the embedded default font (ProggyClean)
    font_ = nk_font_atlas_add_default(atlas_, fontSize, nullptr);
    if (!font_) {
        std::cerr << "[NuklearUI] ERROR: Failed to add default font!\n";
    }

    // Bake font atlas
    int atlasW = 0, atlasH = 0;
    const void* atlasImage = nk_font_atlas_bake(atlas_, &atlasW, &atlasH, NK_FONT_ATLAS_RGBA32);

    // Store font atlas for texture sampling during rendering
    if (atlasImage && atlasW > 0 && atlasH > 0 && atlasH < 10000) {
        fontAtlasImage_.resize(atlasW * atlasH * 4);
        memcpy(fontAtlasImage_.data(), atlasImage, atlasW * atlasH * 4);
        fontAtlasW_ = atlasW;
        fontAtlasH_ = atlasH;
    } else {
        std::cerr << "[NuklearUI] WARNING: Invalid atlas, using blank 256x256\n";
        fontAtlasW_ = 256;
        fontAtlasH_ = 256;
        fontAtlasImage_.resize(fontAtlasW_ * fontAtlasH_ * 4, 255);
    }

    // Finalize atlas
    nk_font_atlas_end(atlas_, nk_handle_ptr(fontAtlasImage_.data()), nullptr);

    // Initialize Nuklear context - must zero before init
    ctx_ = new nk_context;
    memset(ctx_, 0, sizeof(nk_context));

    if (font_) {
        nk_init_default(ctx_, &font_->handle);
    } else {
        nk_init_default(ctx_, nullptr);
        std::cerr << "[NuklearUI] WARNING: Initialized without font\n";
    }

    initialized_ = true;
}

void NuklearUI::resize(int width, int height) {
    if (width == width_ && height == height_) return;

    width_ = width;
    height_ = height;
    pixels_.resize(width * height * 4, 0);
    needsRender_ = true;
}

void NuklearUI::inputBegin() {
    if (!initialized_) return;
    nk_input_begin(ctx_);
}

void NuklearUI::inputMouse(int x, int y, bool leftDown, bool rightDown) {
    if (!initialized_) return;
    nk_input_motion(ctx_, x, y);
    nk_input_button(ctx_, NK_BUTTON_LEFT, x, y, leftDown ? 1 : 0);
    nk_input_button(ctx_, NK_BUTTON_RIGHT, x, y, rightDown ? 1 : 0);
}

void NuklearUI::inputKey(int key, bool down) {
    if (!initialized_) return;
    // Map common keys - extend as needed
    nk_input_key(ctx_, (nk_keys)key, down ? 1 : 0);
}

void NuklearUI::inputChar(char c) {
    if (!initialized_) return;
    nk_input_char(ctx_, c);
}

void NuklearUI::inputScroll(float x, float y) {
    if (!initialized_) return;
    nk_input_scroll(ctx_, nk_vec2(x, y));
}

void NuklearUI::inputEnd() {
    if (!initialized_) return;
    nk_input_end(ctx_);
}

bool NuklearUI::begin(const std::string& title, float x, float y, float w, float h, unsigned int flags) {
    if (!initialized_) return false;

    // Default flags if none specified
    if (flags == 0) {
        flags = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE;
    }

    needsRender_ = true;
    return nk_begin(ctx_, title.c_str(), nk_rect(x, y, w, h), flags) != 0;
}

void NuklearUI::end() {
    if (!initialized_) return;
    nk_end(ctx_);
}

void NuklearUI::layoutRow(float height, int columns) {
    if (!initialized_) return;
    nk_layout_row_dynamic(ctx_, height, columns);
}

void NuklearUI::layoutRowDynamic(float height, int columns) {
    if (!initialized_) return;
    nk_layout_row_dynamic(ctx_, height, columns);
}

void NuklearUI::layoutRowStatic(float height, int itemWidth, int columns) {
    if (!initialized_) return;
    nk_layout_row_static(ctx_, height, itemWidth, columns);
}

void NuklearUI::label(const std::string& text) {
    if (!initialized_) return;
    nk_label(ctx_, text.c_str(), NK_TEXT_LEFT);
}

void NuklearUI::labelColored(const std::string& text, unsigned char r, unsigned char g, unsigned char b) {
    if (!initialized_) return;
    nk_label_colored(ctx_, text.c_str(), NK_TEXT_LEFT, nk_rgb(r, g, b));
}

bool NuklearUI::button(const std::string& text) {
    if (!initialized_) return false;
    return nk_button_label(ctx_, text.c_str()) != 0;
}

bool NuklearUI::checkbox(const std::string& text, bool* active) {
    if (!initialized_) return false;
    nk_bool nkActive = *active ? 1 : 0;
    bool changed = nk_checkbox_label(ctx_, text.c_str(), &nkActive) != 0;
    *active = nkActive != 0;
    return changed;
}

bool NuklearUI::slider(float* value, float min, float max, float step) {
    if (!initialized_) return false;
    float oldValue = *value;
    nk_slider_float(ctx_, min, value, max, step);
    return *value != oldValue;
}

bool NuklearUI::sliderInt(int* value, int min, int max, int step) {
    if (!initialized_) return false;
    int oldValue = *value;
    nk_slider_int(ctx_, min, value, max, step);
    return *value != oldValue;
}

bool NuklearUI::property(const std::string& name, float* value, float min, float max, float step, float incPerPixel) {
    if (!initialized_) return false;
    float oldValue = *value;
    nk_property_float(ctx_, name.c_str(), min, value, max, step, incPerPixel);
    return *value != oldValue;
}

bool NuklearUI::propertyInt(const std::string& name, int* value, int min, int max, int step, int incPerPixel) {
    if (!initialized_) return false;
    int oldValue = *value;
    nk_property_int(ctx_, name.c_str(), min, value, max, step, incPerPixel);
    return *value != oldValue;
}

void NuklearUI::progress(float current, float max, bool modifiable) {
    if (!initialized_) return;
    nk_size cur = (nk_size)current;
    nk_progress(ctx_, &cur, (nk_size)max, modifiable ? NK_MODIFIABLE : NK_FIXED);
}

bool NuklearUI::colorPicker(float* r, float* g, float* b, float* a) {
    if (!initialized_) return false;

    nk_colorf color;
    color.r = *r;
    color.g = *g;
    color.b = *b;
    color.a = a ? *a : 1.0f;

    nk_colorf newColor = nk_color_picker(ctx_, color, a ? NK_RGBA : NK_RGB);

    bool changed = (newColor.r != color.r || newColor.g != color.g ||
                    newColor.b != color.b || (a && newColor.a != color.a));

    *r = newColor.r;
    *g = newColor.g;
    *b = newColor.b;
    if (a) *a = newColor.a;

    return changed;
}

bool NuklearUI::combo(const std::string& selected, const std::vector<std::string>& items,
                      int* selectedIndex, int itemHeight) {
    if (!initialized_) return false;

    bool changed = false;

    if (nk_combo_begin_label(ctx_, selected.c_str(), nk_vec2(nk_widget_width(ctx_), 200))) {
        nk_layout_row_dynamic(ctx_, (float)itemHeight, 1);
        for (size_t i = 0; i < items.size(); i++) {
            if (nk_combo_item_label(ctx_, items[i].c_str(), NK_TEXT_LEFT)) {
                if ((int)i != *selectedIndex) {
                    *selectedIndex = (int)i;
                    changed = true;
                }
            }
        }
        nk_combo_end(ctx_);
    }

    return changed;
}

void NuklearUI::text(const std::string& text) {
    if (!initialized_) return;
    nk_text(ctx_, text.c_str(), (int)text.length(), NK_TEXT_LEFT);
}

bool NuklearUI::editString(std::string& buffer, int maxLength) {
    if (!initialized_) return false;

    // Ensure buffer has space
    if ((int)buffer.size() < maxLength) {
        buffer.resize(maxLength, '\0');
    }

    int len = (int)strlen(buffer.c_str());
    nk_flags state = nk_edit_string(ctx_, NK_EDIT_FIELD, &buffer[0], &len, maxLength, nk_filter_default);

    buffer.resize(len);
    return (state & NK_EDIT_COMMITED) != 0;
}

void NuklearUI::spacing(int columns) {
    if (!initialized_) return;
    nk_spacing(ctx_, columns);
}

void NuklearUI::separator() {
    if (!initialized_) return;
    // Draw a horizontal line by using layout
    nk_layout_row_dynamic(ctx_, 1, 1);
    // Could draw a rect here but spacing works too
}

bool NuklearUI::groupBegin(const std::string& title, unsigned int flags) {
    if (!initialized_) return false;
    return nk_group_begin(ctx_, title.c_str(), flags) != 0;
}

void NuklearUI::groupEnd() {
    if (!initialized_) return;
    nk_group_end(ctx_);
}

bool NuklearUI::treePush(const std::string& title, bool* state) {
    if (!initialized_) return false;
    nk_collapse_states nkState = *state ? NK_MAXIMIZED : NK_MINIMIZED;
    bool result = nk_tree_state_push(ctx_, NK_TREE_NODE, title.c_str(), &nkState) != 0;
    *state = (nkState == NK_MAXIMIZED);
    return result;
}

void NuklearUI::treePop() {
    if (!initialized_) return;
    nk_tree_pop(ctx_);
}

void NuklearUI::clear() {
    // Clear to transparent
    std::fill(pixels_.begin(), pixels_.end(), 0);
}

void NuklearUI::renderToBuffer() {
    if (!initialized_ || !needsRender_) return;

    clear();

    // Convert Nuklear draw commands to vertex buffer
    static const struct nk_draw_vertex_layout_element vertexLayout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(NkVertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(NkVertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(NkVertex, color)},
        {NK_VERTEX_LAYOUT_END}
    };

    struct nk_convert_config config;
    memset(&config, 0, sizeof(config));
    config.vertex_layout = vertexLayout;
    config.vertex_size = sizeof(NkVertex);
    config.vertex_alignment = alignof(NkVertex);
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;
    config.tex_null.texture.ptr = nullptr;
    config.tex_null.uv = nk_vec2(0, 0);

    // Allocate buffers
    struct nk_buffer cmds, verts, idx;
    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&verts);
    nk_buffer_init_default(&idx);

    // Convert commands to vertex buffer
    nk_convert(ctx_, &cmds, &verts, &idx, &config);

    // Get vertex and index data
    const NkVertex* vertices = (const NkVertex*)nk_buffer_memory_const(&verts);
    const nk_draw_index* indices = (const nk_draw_index*)nk_buffer_memory_const(&idx);

    // Rasterize triangles
    const struct nk_draw_command* cmd;
    nk_draw_foreach(cmd, ctx_, &cmds) {
        if (!cmd->elem_count) continue;

        // Get scissor rect
        int scissorX = clampInt((int)cmd->clip_rect.x, 0, width_);
        int scissorY = clampInt((int)cmd->clip_rect.y, 0, height_);
        int scissorW = clampInt((int)cmd->clip_rect.w, 0, width_ - scissorX);
        int scissorH = clampInt((int)cmd->clip_rect.h, 0, height_ - scissorY);

        // Draw triangles
        for (unsigned int i = 0; i < cmd->elem_count; i += 3) {
            const NkVertex& v0 = vertices[indices[i + 0]];
            const NkVertex& v1 = vertices[indices[i + 1]];
            const NkVertex& v2 = vertices[indices[i + 2]];

            // Bounding box
            int minX = clampInt((int)std::min({v0.position[0], v1.position[0], v2.position[0]}), scissorX, scissorX + scissorW);
            int maxX = clampInt((int)std::max({v0.position[0], v1.position[0], v2.position[0]}) + 1, scissorX, scissorX + scissorW);
            int minY = clampInt((int)std::min({v0.position[1], v1.position[1], v2.position[1]}), scissorY, scissorY + scissorH);
            int maxY = clampInt((int)std::max({v0.position[1], v1.position[1], v2.position[1]}) + 1, scissorY, scissorY + scissorH);

            // Triangle area (for barycentric coordinates)
            float area = edgeFunction(v0.position[0], v0.position[1],
                                      v1.position[0], v1.position[1],
                                      v2.position[0], v2.position[1]);

            if (std::abs(area) < 0.001f) continue; // Degenerate triangle

            // Rasterize
            for (int y = minY; y < maxY; y++) {
                for (int x = minX; x < maxX; x++) {
                    float px = x + 0.5f;
                    float py = y + 0.5f;

                    // Barycentric coordinates
                    float w0 = edgeFunction(v1.position[0], v1.position[1],
                                            v2.position[0], v2.position[1], px, py) / area;
                    float w1 = edgeFunction(v2.position[0], v2.position[1],
                                            v0.position[0], v0.position[1], px, py) / area;
                    float w2 = edgeFunction(v0.position[0], v0.position[1],
                                            v1.position[0], v1.position[1], px, py) / area;

                    // Check if inside triangle
                    if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                        // Interpolate color
                        unsigned char color[4];
                        color[0] = (unsigned char)(v0.color[0] * w0 + v1.color[0] * w1 + v2.color[0] * w2);
                        color[1] = (unsigned char)(v0.color[1] * w0 + v1.color[1] * w1 + v2.color[1] * w2);
                        color[2] = (unsigned char)(v0.color[2] * w0 + v1.color[2] * w1 + v2.color[2] * w2);
                        color[3] = (unsigned char)(v0.color[3] * w0 + v1.color[3] * w1 + v2.color[3] * w2);

                        // Blend to framebuffer
                        unsigned char* dst = &pixels_[(y * width_ + x) * 4];
                        blendPixel(dst, color);
                    }
                }
            }
        }

        indices += cmd->elem_count;
    }

    // Cleanup
    nk_buffer_free(&cmds);
    nk_buffer_free(&verts);
    nk_buffer_free(&idx);
    nk_clear(ctx_);

    needsRender_ = false;
}

void NuklearUI::render(Context& ctx, Texture& output) {
    if (!initialized_) return;

    // Resize if needed
    if (width_ != ctx.width() || height_ != ctx.height()) {
        resize(ctx.width(), ctx.height());
    }

    // Render UI to CPU buffer
    renderToBuffer();

    // Upload to GPU texture
    ctx.uploadTexturePixels(output, pixels_.data(), width_, height_);
}

} // namespace nuklear
} // namespace vivid
