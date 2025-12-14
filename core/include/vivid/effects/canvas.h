#pragma once

/**
 * @file canvas.h
 * @brief Canvas operator for imperative 2D drawing with text support
 *
 * Unlike Shape (which draws a single SDF shape), Canvas allows drawing
 * multiple primitives and text in a single operator using an imperative API.
 * All primitives are batched into a single draw call for efficiency.
 */

#include <vivid/effects/texture_operator.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace vivid {
class FontAtlas;
class CanvasRenderer;
}

namespace vivid::effects {

/**
 * @brief Imperative 2D drawing operator
 *
 * Canvas provides an immediate-mode drawing API for 2D graphics and text.
 * Unlike other operators that are declarative, Canvas requires calling
 * draw methods in your update() function before process().
 *
 * @par Features
 * - Multiple primitives in a single operator
 * - TTF font loading and text rendering
 * - Efficient batched rendering (single draw call)
 * - Transparent backgrounds for overlays
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     chain->add<Canvas>("ui").size(1280, 720);
 *     chain->get<Canvas>("ui").loadFont("assets/fonts/pixel.ttf", 32);
 * }
 *
 * void update(Context& ctx) {
 *     auto& canvas = chain->get<Canvas>("ui");
 *     canvas.clear(0, 0, 0, 0);  // Transparent background
 *
 *     // Draw UI elements
 *     canvas.rectFilled(10, 10, 200, 50, {0.2, 0.2, 0.2, 0.8});
 *     canvas.text("Score: 1000", 20, 40, {1, 1, 1, 1});
 *     canvas.circleFilled(640, 360, 50, {1, 0, 0, 1});
 *
 *     chain->process();
 * }
 * @endcode
 *
 * @see Shape for single SDF shape rendering
 * @see Composite for layering Canvas over other content
 */
class Canvas : public TextureOperator {
public:
    Canvas();
    ~Canvas() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set canvas resolution
     * @param w Width in pixels
     * @param h Height in pixels
     *
     * Note: This also locks the resolution to prevent auto-resize to window size.
     */
    void size(int w, int h) {
        // Always lock resolution when size() is explicitly called
        m_resolutionLocked = true;
        if (m_width != w || m_height != h) {
            m_width = w;
            m_height = h;
            markDirty();
        }
    }

    /**
     * @brief Load a TTF font for text rendering
     * @param ctx Context for GPU access
     * @param path Path to TTF file
     * @param fontSize Font size in pixels
     * @return true on success
     */
    bool loadFont(Context& ctx, const std::string& path, float fontSize);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Frame Control
    /// @{

    /**
     * @brief Clear canvas and begin a new frame
     * @param r Red component (0-1)
     * @param g Green component (0-1)
     * @param b Blue component (0-1)
     * @param a Alpha component (0-1), use 0 for transparent overlay
     *
     * Call this at the start of each frame before drawing primitives.
     */
    void clear(float r, float g, float b, float a = 1.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rectangle Primitives
    /// @{

    /**
     * @brief Draw a filled rectangle
     * @param x Left edge in pixels
     * @param y Top edge in pixels
     * @param w Width in pixels
     * @param h Height in pixels
     * @param color Fill color (RGBA)
     */
    void rectFilled(float x, float y, float w, float h, const glm::vec4& color);

    /**
     * @brief Draw a rectangle outline
     * @param x Left edge in pixels
     * @param y Top edge in pixels
     * @param w Width in pixels
     * @param h Height in pixels
     * @param lineWidth Line thickness in pixels
     * @param color Stroke color (RGBA)
     */
    void rect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Circle Primitives
    /// @{

    /**
     * @brief Draw a filled circle
     * @param x Center X in pixels
     * @param y Center Y in pixels
     * @param radius Radius in pixels
     * @param color Fill color (RGBA)
     * @param segments Number of segments (higher = smoother)
     */
    void circleFilled(float x, float y, float radius, const glm::vec4& color, int segments = 32);

    /**
     * @brief Draw a circle outline
     * @param x Center X in pixels
     * @param y Center Y in pixels
     * @param radius Radius in pixels
     * @param lineWidth Line thickness in pixels
     * @param color Stroke color (RGBA)
     * @param segments Number of segments (higher = smoother)
     */
    void circle(float x, float y, float radius, float lineWidth, const glm::vec4& color, int segments = 32);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Line Primitives
    /// @{

    /**
     * @brief Draw a line segment
     * @param x1 Start X in pixels
     * @param y1 Start Y in pixels
     * @param x2 End X in pixels
     * @param y2 End Y in pixels
     * @param width Line thickness in pixels
     * @param color Line color (RGBA)
     */
    void line(float x1, float y1, float x2, float y2, float width, const glm::vec4& color);

    /**
     * @brief Draw a filled triangle
     * @param a First vertex in pixels
     * @param b Second vertex in pixels
     * @param c Third vertex in pixels
     * @param color Fill color (RGBA)
     */
    void triangleFilled(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Text Rendering
    /// @{

    /**
     * @brief Draw text at a position
     * @param str Text string
     * @param x Left edge X in pixels
     * @param y Baseline Y in pixels
     * @param color Text color (RGBA)
     * @param letterSpacing Extra spacing between letters (default 0)
     *
     * Requires a font to be loaded via loadFont().
     */
    void text(const std::string& str, float x, float y, const glm::vec4& color,
              float letterSpacing = 0.0f);

    /**
     * @brief Draw text centered at a position
     * @param str Text string
     * @param x Center X in pixels
     * @param y Center Y in pixels
     * @param color Text color (RGBA)
     * @param letterSpacing Extra spacing between letters (default 0)
     */
    void textCentered(const std::string& str, float x, float y, const glm::vec4& color,
                      float letterSpacing = 0.0f);

    /**
     * @brief Measure text dimensions
     * @param str Text string to measure
     * @return Width and height in pixels
     */
    glm::vec2 measureText(const std::string& str) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Canvas"; }

    /// @}

private:
    std::unique_ptr<CanvasRenderer> m_renderer;
    std::unique_ptr<FontAtlas> m_font;

    glm::vec4 m_clearColor = {0, 0, 0, 1};
    bool m_initialized = false;
    bool m_frameBegun = false;
};

} // namespace vivid::effects
