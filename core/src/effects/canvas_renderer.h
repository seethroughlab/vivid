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
     * @param letterSpacing Extra spacing between letters (default 0)
     */
    void text(FontAtlas& font, const std::string& str, float x, float y,
              const glm::vec4& color, float letterSpacing = 0.0f);

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

    // -------------------------------------------------------------------------
    /// @name Low-level Batch API (for Canvas path rendering)
    /// @{

    /**
     * @brief Add a solid-colored quad to the batch
     * @param p0 First vertex (top-left typically)
     * @param p1 Second vertex (top-right)
     * @param p2 Third vertex (bottom-right)
     * @param p3 Fourth vertex (bottom-left)
     * @param color Fill color (RGBA)
     */
    void addSolidQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, const glm::vec4& color);

    /**
     * @brief Add a textured image to draw
     * @param textureView Source texture view
     * @param srcWidth Source texture width
     * @param srcHeight Source texture height
     * @param sx Source X (in pixels)
     * @param sy Source Y (in pixels)
     * @param sw Source width (in pixels)
     * @param sh Source height (in pixels)
     * @param dx Destination X (in canvas pixels)
     * @param dy Destination Y (in canvas pixels)
     * @param dw Destination width (in canvas pixels)
     * @param dh Destination height (in canvas pixels)
     * @param alpha Global alpha
     */
    void addImage(WGPUTextureView textureView,
                  int srcWidth, int srcHeight,
                  float sx, float sy, float sw, float sh,
                  float dx, float dy, float dw, float dh,
                  float alpha);

    /**
     * @brief Add a clip region (triangles to write to stencil)
     * @param vertices Clip path vertices
     * @param indices Clip path indices
     */
    void addClip(const std::vector<glm::vec2>& vertices, const std::vector<uint32_t>& indices);

    /**
     * @brief Set the current clip depth (for stencil reference)
     * @param depth Clip depth (0 = no clipping)
     *
     * Flushes any pending geometry before changing clip state.
     */
    void setClipDepth(int depth);

    /**
     * @brief Get current clip depth
     */
    int clipDepth() const { return m_clipDepth; }

    /// @}

private:
    void createPipeline(Context& ctx);
    void createWhiteTexture(Context& ctx);

    // Add a quad to text batch (for font glyphs)
    void addTextQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                     glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
                     const glm::vec4& color);

    // Render a batch
    void renderBatch(WGPURenderPassEncoder pass, Context& ctx,
                     const std::vector<CanvasVertex>& vertices,
                     const std::vector<uint32_t>& indices,
                     WGPUBindGroup bindGroup);

    // Image draw command for deferred rendering
    struct ImageDrawCmd {
        WGPUTextureView textureView;
        std::vector<CanvasVertex> vertices;
        std::vector<uint32_t> indices;
        int clipDepth;  // Stencil reference for this draw
    };

    // Solid draw command (tracks clip state at submission time)
    struct SolidDrawCmd {
        std::vector<CanvasVertex> vertices;
        std::vector<uint32_t> indices;
        int clipDepth;  // Stencil reference for this draw
    };

    // Clip command for stencil rendering
    struct ClipCmd {
        std::vector<CanvasVertex> vertices;
        std::vector<uint32_t> indices;
        int clipDepth;  // Stencil value to write
    };

    void createStencilTexture(Context& ctx, int width, int height);
    void flushSolidBatch();  ///< Flush current solid vertices to a command

    // Batched geometry - separate batches for solid and text primitives
    std::vector<CanvasVertex> m_solidVertices;  ///< Current batch being built
    std::vector<uint32_t> m_solidIndices;
    std::vector<SolidDrawCmd> m_solidCommands;  ///< Completed solid draw commands
    std::vector<CanvasVertex> m_textVertices;
    std::vector<uint32_t> m_textIndices;
    std::vector<ImageDrawCmd> m_imageCommands;
    std::vector<ClipCmd> m_clipCommands;
    int m_clipDepth = 0;  ///< Current stencil reference value

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;           ///< Main drawing pipeline (with stencil test)
    WGPURenderPipeline m_clipPipeline = nullptr;       ///< Stencil-write pipeline (for clip paths)
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Stencil buffer for clipping
    WGPUTexture m_stencilTexture = nullptr;
    WGPUTextureView m_stencilView = nullptr;
    int m_stencilWidth = 0;
    int m_stencilHeight = 0;

    // White 1x1 texture for solid-colored primitives
    WGPUTexture m_whiteTexture = nullptr;
    WGPUTextureView m_whiteTextureView = nullptr;
    WGPUBindGroup m_whiteBindGroup = nullptr;

    // Current font bind group (created per-frame if text is used)
    WGPUBindGroup m_fontBindGroup = nullptr;
    FontAtlas* m_currentFont = nullptr;

    // Persistent vertex/index buffers (reused each frame to avoid allocation churn)
    WGPUBuffer m_solidVertexBuffer = nullptr;
    WGPUBuffer m_solidIndexBuffer = nullptr;
    WGPUBuffer m_textVertexBuffer = nullptr;
    WGPUBuffer m_textIndexBuffer = nullptr;
    size_t m_solidVertexCapacity = 0;
    size_t m_solidIndexCapacity = 0;
    size_t m_textVertexCapacity = 0;
    size_t m_textIndexCapacity = 0;

    // Frame state
    int m_width = 0;
    int m_height = 0;
    glm::vec4 m_clearColor = {0, 0, 0, 1};

    bool m_initialized = false;

    static constexpr int MAX_VERTICES = 65536;
    static constexpr int MAX_INDICES = MAX_VERTICES * 3;
    static constexpr size_t INITIAL_VERTEX_CAPACITY = 1024;
    static constexpr size_t INITIAL_INDEX_CAPACITY = 4096;
};

} // namespace vivid
