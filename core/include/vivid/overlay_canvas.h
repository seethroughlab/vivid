#pragma once

/**
 * @file overlay_canvas.h
 * @brief Lightweight 2D canvas for screen overlay rendering
 *
 * Unlike the full Canvas operator (which renders to a texture), OverlayCanvas
 * renders directly to an existing render pass. Used for UI overlays like the
 * node graph visualizer.
 *
 * Features:
 * - Renders to existing render pass (no texture allocation)
 * - Batched drawing (single draw call)
 * - Transform stack for zoom/pan
 * - Text rendering with FontAtlas
 * - No clipping support (simpler pipeline, no stencil needed)
 */

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace vivid {

class Context;
class FontAtlas;

/**
 * @brief Vertex for overlay rendering
 */
struct OverlayVertex {
    glm::vec2 position;  ///< Screen space position in pixels
    glm::vec2 uv;        ///< Texture coordinates (0.5, 0.5 for solid color)
    glm::vec4 color;     ///< Vertex color (RGBA, premultiplied alpha)
};

/**
 * @brief Lightweight 2D canvas for screen overlays
 *
 * Designed for rendering UI elements directly to the screen without
 * allocating intermediate textures. Ideal for node graph visualization,
 * debug overlays, and HUD elements.
 *
 * Usage:
 * @code
 * // Initialize once
 * overlay.init(ctx);
 * overlay.loadFont(ctx, "fonts/ui.ttf", 14);
 *
 * // Each frame
 * overlay.begin(width, height);
 * overlay.setTransform(zoomPanMatrix);
 * overlay.fillRect(10, 10, 100, 50, {0.2, 0.3, 0.4, 1.0});
 * overlay.text("Hello", 20, 30, {1, 1, 1, 1});
 * overlay.render(renderPass);
 * @endcode
 */
class OverlayCanvas {
public:
    OverlayCanvas();
    ~OverlayCanvas();

    /**
     * @brief Initialize GPU resources
     * @param ctx Context for device/queue access
     * @param surfaceFormat The surface texture format (from window manager)
     * @return true on success
     */
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8UnormSrgb);

    /**
     * @brief Load a font for text rendering
     * @param ctx Context for GPU access
     * @param path Path to TTF file
     * @param fontSize Font size in pixels
     * @return true on success
     */
    bool loadFont(Context& ctx, const std::string& path, float fontSize);

    /**
     * @brief Load additional font size (for zoom-aware text)
     * @param ctx Context for GPU access
     * @param path Path to TTF file
     * @param fontSize Font size in pixels
     * @param index Font index (0-2)
     * @return true on success
     */
    bool loadFontSize(Context& ctx, const std::string& path, float fontSize, int index);

    /**
     * @brief Clean up GPU resources
     */
    void cleanup();

    // -------------------------------------------------------------------------
    /// @name Frame Lifecycle
    /// @{

    /**
     * @brief Begin a new frame
     * @param width Canvas width in pixels
     * @param height Canvas height in pixels
     *
     * Clears batched geometry from previous frame.
     */
    void begin(int width, int height);

    /**
     * @brief Render all batched geometry to the render pass
     * @param pass Active render pass encoder
     *
     * Call this after all drawing is complete. The pass must already be started
     * and should not be ended until after this returns.
     */
    void render(WGPURenderPassEncoder pass);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Transform
    /// @{

    /**
     * @brief Push current transform onto stack
     */
    void save();

    /**
     * @brief Pop transform from stack
     */
    void restore();

    /**
     * @brief Set the transform matrix
     * @param matrix 3x3 transformation matrix
     */
    void setTransform(const glm::mat3& matrix);

    /**
     * @brief Get current transform matrix
     */
    glm::mat3 getTransform() const { return m_transform; }

    /**
     * @brief Reset transform to identity
     */
    void resetTransform();

    /**
     * @brief Apply translation
     */
    void translate(float x, float y);

    /**
     * @brief Apply uniform scale
     */
    void scale(float s);

    /**
     * @brief Apply non-uniform scale
     */
    void scale(float sx, float sy);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Primitives
    /// @{

    /**
     * @brief Draw a filled rectangle
     */
    void fillRect(float x, float y, float w, float h, const glm::vec4& color);

    /**
     * @brief Draw a rectangle outline
     * @param lineWidth Line width in screen pixels (not affected by transform)
     */
    void strokeRect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color);

    /**
     * @brief Draw a filled circle
     * @param segments Number of segments (use getCircleSegments for zoom-aware)
     */
    void fillCircle(float cx, float cy, float radius, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a circle outline
     * @param lineWidth Line width in screen pixels
     */
    void strokeCircle(float cx, float cy, float radius, float lineWidth, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a line
     * @param lineWidth Line width in screen pixels
     */
    void line(float x1, float y1, float x2, float y2, float lineWidth, const glm::vec4& color);

    /**
     * @brief Draw a filled triangle
     */
    void fillTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color);

    /**
     * @brief Draw a cubic bezier curve
     * @param lineWidth Line width in screen pixels
     * @param segments Number of line segments to approximate curve
     */
    void bezierCurve(float x1, float y1, float cx1, float cy1,
                     float cx2, float cy2, float x2, float y2,
                     float lineWidth, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a filled rounded rectangle
     * @param radius Corner radius
     * @param segments Segments per corner arc
     */
    void fillRoundedRect(float x, float y, float w, float h, float radius,
                         const glm::vec4& color, int segments = 8);

    /**
     * @brief Draw a rounded rectangle outline
     */
    void strokeRoundedRect(float x, float y, float w, float h, float radius,
                           float lineWidth, const glm::vec4& color, int segments = 8);

    /**
     * @brief Draw a textured rectangle (for operator previews)
     * @param x Left edge
     * @param y Top edge
     * @param w Width
     * @param h Height
     * @param textureView WebGPU texture view to sample from
     * @param tint Optional tint color (default white = no tint)
     */
    void texturedRect(float x, float y, float w, float h, WGPUTextureView textureView,
                      const glm::vec4& tint = glm::vec4(1.0f));

    /// @}
    // -------------------------------------------------------------------------
    /// @name Topmost Layer (for tooltips - renders on top of everything)
    /// @{

    /**
     * @brief Draw a filled rounded rectangle in the topmost layer
     * @note Use for tooltips that should appear above all other content
     */
    void fillRoundedRectTopmost(float x, float y, float w, float h, float radius,
                                 const glm::vec4& color, int segments = 8);

    /**
     * @brief Draw a rounded rectangle outline in the topmost layer
     */
    void strokeRoundedRectTopmost(float x, float y, float w, float h, float radius,
                                   float lineWidth, const glm::vec4& color, int segments = 8);

    /**
     * @brief Draw text in the topmost layer (for tooltips)
     */
    void textTopmost(const std::string& str, float x, float y, const glm::vec4& color, int fontIndex = 0);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Text
    /// @{

    /**
     * @brief Draw text
     * @param str Text string
     * @param x X position (left edge)
     * @param y Y position (baseline)
     * @param color Text color
     * @param fontIndex Which font to use (0-2, for multi-size fonts)
     */
    void text(const std::string& str, float x, float y, const glm::vec4& color, int fontIndex = 0);

    /**
     * @brief Draw scaled text
     * @param str Text string
     * @param x X position (left edge)
     * @param y Y position (baseline)
     * @param color Text color
     * @param scale Scale factor (1.0 = normal, 2.0 = double size)
     * @param fontIndex Which font to use (0-2)
     */
    void textScaled(const std::string& str, float x, float y, const glm::vec4& color, float scale, int fontIndex = 0);

    /**
     * @brief Measure text width
     * @param str Text string
     * @param fontIndex Which font to use
     * @return Width in pixels
     */
    float measureText(const std::string& str, int fontIndex = 0) const;

    /**
     * @brief Measure scaled text width
     * @param str Text string
     * @param scale Scale factor
     * @param fontIndex Which font to use
     * @return Width in pixels (scaled)
     */
    float measureTextScaled(const std::string& str, float scale, int fontIndex = 0) const;

    /**
     * @brief Get recommended font index for current zoom level
     * @param zoom Current zoom factor
     * @return Font index (0, 1, or 2)
     */
    int getFontForZoom(float zoom) const;

    /**
     * @brief Get font line height
     * @param fontIndex Which font to use (0-2)
     * @return Line height in pixels, or 0 if font not loaded
     */
    float fontLineHeight(int fontIndex = 0) const;

    /**
     * @brief Get font ascent (baseline to top)
     * @param fontIndex Which font to use (0-2)
     * @return Ascent in pixels, or 0 if font not loaded
     */
    float fontAscent(int fontIndex = 0) const;

    /**
     * @brief Get font descent (baseline to bottom, typically negative)
     * @param fontIndex Which font to use (0-2)
     * @return Descent in pixels, or 0 if font not loaded
     */
    float fontDescent(int fontIndex = 0) const;

    /**
     * @brief Get font size
     * @param fontIndex Which font to use (0-2)
     * @return Font size in pixels, or 0 if font not loaded
     */
    float fontSize(int fontIndex = 0) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Utilities
    /// @{

    /**
     * @brief Get recommended circle segments for zoom level
     * @param radius Circle radius in world units
     * @param zoom Current zoom factor
     * @return Number of segments
     */
    static int getCircleSegments(float radius, float zoom);

    /**
     * @brief Transform a point from world to screen coordinates
     */
    glm::vec2 transformPoint(const glm::vec2& p) const;

    /**
     * @brief Transform a point from screen to world coordinates
     */
    glm::vec2 inverseTransformPoint(const glm::vec2& p) const;

    /// @}

private:
    void createPipeline(Context& ctx);
    void createWhiteTexture(Context& ctx);

    // Add a solid-colored quad (positions already in screen space)
    void addQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, const glm::vec4& color);

    // Add a textured quad for text glyphs (to specific font batch)
    void addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                     glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                     const glm::vec4& color, int fontIndex);

    // Batched geometry
    std::vector<OverlayVertex> m_solidVertices;
    std::vector<uint32_t> m_solidIndices;
    // Per-font text batches (up to 3 fonts)
    std::vector<OverlayVertex> m_textVertices[3];
    std::vector<uint32_t> m_textIndices[3];
    // Topmost layer (for tooltips - rendered last, on top of everything)
    std::vector<OverlayVertex> m_topmostVertices;
    std::vector<uint32_t> m_topmostIndices;
    std::vector<OverlayVertex> m_topmostTextVertices[3];
    std::vector<uint32_t> m_topmostTextIndices[3];

    // Textured rects (for operator previews - not batched, drawn individually)
    struct TexturedRect {
        glm::vec2 pos;
        glm::vec2 size;
        WGPUTextureView textureView;
        glm::vec4 tint;
    };
    std::vector<TexturedRect> m_texturedRects;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // White texture for solid primitives
    WGPUTexture m_whiteTexture = nullptr;
    WGPUTextureView m_whiteTextureView = nullptr;
    WGPUBindGroup m_whiteBindGroup = nullptr;

    // Fonts (up to 3 sizes for zoom-aware text)
    std::unique_ptr<FontAtlas> m_fonts[3];
    WGPUBindGroup m_fontBindGroups[3] = {nullptr, nullptr, nullptr};

    // Persistent buffers
    WGPUBuffer m_solidVertexBuffer = nullptr;
    WGPUBuffer m_solidIndexBuffer = nullptr;
    WGPUBuffer m_textVertexBuffer[3] = {nullptr, nullptr, nullptr};
    WGPUBuffer m_textIndexBuffer[3] = {nullptr, nullptr, nullptr};
    size_t m_solidVertexCapacity = 0;
    size_t m_solidIndexCapacity = 0;
    size_t m_textVertexCapacity[3] = {0, 0, 0};
    size_t m_textIndexCapacity[3] = {0, 0, 0};

    // Transform state
    glm::mat3 m_transform = glm::mat3(1.0f);
    std::vector<glm::mat3> m_transformStack;

    // Frame state
    int m_width = 0;
    int m_height = 0;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_BGRA8UnormSrgb;
    bool m_initialized = false;

    static constexpr size_t INITIAL_VERTEX_CAPACITY = 1024;
    static constexpr size_t INITIAL_INDEX_CAPACITY = 4096;
};

} // namespace vivid
