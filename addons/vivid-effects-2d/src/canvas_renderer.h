#pragma once

/**
 * @file canvas_renderer.h
 * @brief Batched 2D renderer for Canvas operator
 *
 * Renders primitives (rectangles, circles, lines, text) in a single draw call.
 */

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace vivid {

class Context;
class FontAtlas;

/**
 * @brief Vertex for 2D canvas rendering
 */
struct CanvasVertex {
    glm::vec2 position;  ///< Screen space position in pixels
    glm::vec2 uv;        ///< Texture coordinates (0,0 for solid color)
    glm::vec4 color;     ///< Vertex color (premultiplied alpha)
};

/**
 * @brief Batched 2D renderer
 *
 * Collects primitives and renders them in a single draw call.
 * Supports solid-colored shapes and textured quads (for text).
 */
class CanvasRenderer {
public:
    CanvasRenderer() = default;
    ~CanvasRenderer();

    /**
     * @brief Initialize the renderer
     * @param ctx Context for GPU access
     * @return true on success
     */
    bool init(Context& ctx);

    /**
     * @brief Begin a new frame
     * @param width Canvas width in pixels
     * @param height Canvas height in pixels
     * @param clearColor Clear color (RGBA)
     */
    void begin(int width, int height, const glm::vec4& clearColor);

    // -------------------------------------------------------------------------
    /// @name Primitives
    /// @{

    /**
     * @brief Draw a filled rectangle
     */
    void rectFilled(float x, float y, float w, float h, const glm::vec4& color);

    /**
     * @brief Draw a rectangle outline
     */
    void rect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color);

    /**
     * @brief Draw a filled circle
     * @param segments Number of segments (higher = smoother)
     */
    void circleFilled(float x, float y, float radius, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a circle outline
     */
    void circle(float x, float y, float radius, float lineWidth, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a line
     */
    void line(float x1, float y1, float x2, float y2, float width, const glm::vec4& color);

    /**
     * @brief Draw a filled triangle
     */
    void triangleFilled(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Text
    /// @{

    /**
     * @brief Draw text
     * @param font Font atlas to use
     * @param str Text string
     * @param x X position (left edge)
     * @param y Y position (baseline)
     * @param color Text color
     */
    void text(FontAtlas& font, const std::string& str, float x, float y, const glm::vec4& color);

    /// @}
    // -------------------------------------------------------------------------

    /**
     * @brief Render all batched primitives to texture
     * @param ctx Context for GPU access
     * @param targetTexture Target texture to render to
     * @param targetView Target texture view
     */
    void render(Context& ctx, WGPUTexture targetTexture, WGPUTextureView targetView);

    /**
     * @brief Clean up GPU resources
     */
    void cleanup();

private:
    void createPipeline(Context& ctx);
    void createWhiteTexture(Context& ctx);

    // Add a quad (two triangles)
    void addQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                 glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                 const glm::vec4& color);

    // Batched geometry
    std::vector<CanvasVertex> m_vertices;
    std::vector<uint32_t> m_indices;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // White 1x1 texture for solid-colored primitives
    WGPUTexture m_whiteTexture = nullptr;
    WGPUTextureView m_whiteTextureView = nullptr;
    WGPUBindGroup m_whiteBindGroup = nullptr;

    // Current font bind group (created per-frame if text is used)
    WGPUBindGroup m_fontBindGroup = nullptr;
    FontAtlas* m_currentFont = nullptr;

    // Frame state
    int m_width = 0;
    int m_height = 0;
    glm::vec4 m_clearColor = {0, 0, 0, 1};

    bool m_initialized = false;

    static constexpr int MAX_VERTICES = 65536;
    static constexpr int MAX_INDICES = MAX_VERTICES * 3;
};

} // namespace vivid
