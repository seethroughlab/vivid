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
#include <vector>

namespace vivid {
class FontAtlas;
class CanvasRenderer;
}

namespace vivid::effects {

// -------------------------------------------------------------------------
// Canvas State Types (HTML Canvas 2D-style)
// -------------------------------------------------------------------------

/// @brief Line cap style for stroke endpoints
enum class LineCap {
    Butt,   ///< Flat end at exactly the endpoint
    Round,  ///< Semicircle at endpoint
    Square  ///< Flat end extended by half line width
};

/// @brief Line join style for stroke corners
enum class LineJoin {
    Miter,  ///< Sharp corner (limited by miterLimit)
    Round,  ///< Rounded corner
    Bevel   ///< Flat diagonal corner
};

/// @brief Path command types for vector path construction
enum class PathCommandType {
    MoveTo,
    LineTo,
    Arc,
    ArcTo,
    QuadraticCurveTo,
    BezierCurveTo,
    ClosePath
};

/// @brief A single path command with parameters
struct PathCommand {
    PathCommandType type;
    std::vector<float> params;
};

// -------------------------------------------------------------------------
// Gradient Types (HTML Canvas 2D-style)
// -------------------------------------------------------------------------

/// @brief Gradient type
enum class GradientType {
    Linear,  ///< Linear gradient along a line
    Radial,  ///< Radial gradient between two circles
    Conic    ///< Conic (angular) gradient around a point
};

/// @brief A color stop in a gradient
struct ColorStop {
    float offset;    ///< Position in gradient (0.0 to 1.0)
    glm::vec4 color; ///< Color at this position (RGBA)
};

/**
 * @brief Gradient for Canvas fill/stroke styles
 *
 * Create gradients using Canvas::createLinearGradient(), createRadialGradient(),
 * or createConicGradient(), then add color stops with addColorStop().
 *
 * @par Example
 * @code
 * auto gradient = canvas.createLinearGradient(0, 0, 200, 0);
 * gradient.addColorStop(0.0, {1, 0, 0, 1});  // Red at start
 * gradient.addColorStop(1.0, {0, 0, 1, 1});  // Blue at end
 * canvas.fillStyle(gradient);
 * canvas.fillRect(0, 0, 200, 100);
 * @endcode
 */
class CanvasGradient {
public:
    /**
     * @brief Add a color stop to the gradient
     * @param offset Position in gradient (0.0 to 1.0)
     * @param color Color at this position (RGBA, 0-1 range)
     */
    void addColorStop(float offset, const glm::vec4& color);

    /**
     * @brief Add a color stop to the gradient
     * @param offset Position in gradient (0.0 to 1.0)
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1)
     */
    void addColorStop(float offset, float r, float g, float b, float a = 1.0f);

    // Gradient data (public for renderer access)
    GradientType type = GradientType::Linear;
    glm::vec2 p0 = {0, 0};  ///< Start point (linear) or start circle center (radial) or center (conic)
    glm::vec2 p1 = {0, 0};  ///< End point (linear) or end circle center (radial)
    float r0 = 0.0f;        ///< Start radius (radial only)
    float r1 = 0.0f;        ///< End radius (radial only)
    float startAngle = 0.0f; ///< Start angle in radians (conic only)
    std::vector<ColorStop> colorStops;

    static constexpr int MAX_COLOR_STOPS = 8; ///< Maximum color stops (GPU uniform limit)
};

// -------------------------------------------------------------------------
// Text Types (HTML Canvas 2D-style)
// -------------------------------------------------------------------------

/// @brief Text horizontal alignment
enum class TextAlign {
    Left,   ///< Align to left of x position
    Right,  ///< Align to right of x position
    Center, ///< Center text on x position
    Start,  ///< Same as Left (LTR) or Right (RTL)
    End     ///< Same as Right (LTR) or Left (RTL)
};

/// @brief Text baseline alignment
enum class TextBaseline {
    Top,         ///< Top of em square
    Hanging,     ///< Hanging baseline (near top)
    Middle,      ///< Middle of em square
    Alphabetic,  ///< Normal baseline (default)
    Ideographic, ///< Bottom of ideographic characters
    Bottom       ///< Bottom of em square
};

/// @brief Text measurement results
struct TextMetrics {
    float width = 0;                    ///< Advance width of text
    float actualBoundingBoxLeft = 0;    ///< Distance to left edge from alignment point
    float actualBoundingBoxRight = 0;   ///< Distance to right edge from alignment point
    float actualBoundingBoxAscent = 0;  ///< Distance above baseline
    float actualBoundingBoxDescent = 0; ///< Distance below baseline
    float fontBoundingBoxAscent = 0;    ///< Font's ascender height
    float fontBoundingBoxDescent = 0;   ///< Font's descender depth
};

/// @brief Canvas drawing state (saved/restored with save()/restore())
struct CanvasState {
    glm::vec4 fillColor = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 strokeColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float lineWidth = 1.0f;
    LineCap lineCap = LineCap::Butt;
    LineJoin lineJoin = LineJoin::Miter;
    float miterLimit = 10.0f;
    float globalAlpha = 1.0f;
    glm::mat3 transform = glm::mat3(1.0f);

    // Gradient styles (optional, takes precedence over solid color when set)
    std::shared_ptr<CanvasGradient> fillGradient;
    std::shared_ptr<CanvasGradient> strokeGradient;

    // Text state
    TextAlign textAlign = TextAlign::Left;
    TextBaseline textBaseline = TextBaseline::Alphabetic;

    // Clipping state
    int clipDepth = 0;  ///< Current stencil clip depth (0 = no clipping)
};

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
 * @par Example (HTML Canvas 2D-style API)
 * @code
 * void update(Context& ctx) {
 *     auto& canvas = chain->get<Canvas>("ui");
 *     canvas.clear(0, 0, 0, 0);
 *
 *     // Stateful drawing (like HTML Canvas)
 *     canvas.fillStyle({0.2, 0.4, 0.8, 1.0});
 *     canvas.fillRect(10, 10, 200, 50);
 *
 *     // Path-based drawing
 *     canvas.beginPath();
 *     canvas.moveTo(100, 100);
 *     canvas.lineTo(200, 100);
 *     canvas.lineTo(150, 50);
 *     canvas.closePath();
 *     canvas.fill();
 *
 *     // Transforms
 *     canvas.save();
 *     canvas.translate(400, 300);
 *     canvas.rotate(ctx.time());
 *     canvas.fillRect(-50, -50, 100, 100);
 *     canvas.restore();
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
     */
    void size(int w, int h) {
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
    /// @name State Management (HTML Canvas 2D-style)
    /// @{

    /**
     * @brief Set fill color for subsequent fill operations
     * @param color Fill color (RGBA, 0-1 range)
     */
    void fillStyle(const glm::vec4& color);

    /**
     * @brief Set fill color for subsequent fill operations
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1)
     */
    void fillStyle(float r, float g, float b, float a = 1.0f);

    /**
     * @brief Set stroke color for subsequent stroke operations
     * @param color Stroke color (RGBA, 0-1 range)
     */
    void strokeStyle(const glm::vec4& color);

    /**
     * @brief Set stroke color for subsequent stroke operations
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1)
     */
    void strokeStyle(float r, float g, float b, float a = 1.0f);

    /**
     * @brief Set fill style to a gradient
     * @param gradient Gradient created with createLinearGradient, etc.
     */
    void fillStyle(const CanvasGradient& gradient);

    /**
     * @brief Set stroke style to a gradient
     * @param gradient Gradient created with createLinearGradient, etc.
     */
    void strokeStyle(const CanvasGradient& gradient);

    /**
     * @brief Set line width for stroke operations
     * @param width Line width in pixels
     */
    void lineWidth(float width);

    /**
     * @brief Set line cap style for stroke endpoints
     * @param cap LineCap::Butt, Round, or Square
     */
    void lineCap(LineCap cap);

    /**
     * @brief Set line join style for stroke corners
     * @param join LineJoin::Miter, Round, or Bevel
     */
    void lineJoin(LineJoin join);

    /**
     * @brief Set miter limit for sharp corners
     * @param limit Maximum miter length (default 10)
     */
    void miterLimit(float limit);

    /**
     * @brief Set global alpha for all drawing operations
     * @param alpha Alpha multiplier (0-1)
     */
    void globalAlpha(float alpha);

    /**
     * @brief Push current state onto stack
     *
     * Saves: fillColor, strokeColor, lineWidth, lineCap, lineJoin,
     * miterLimit, globalAlpha, transform
     */
    void save();

    /**
     * @brief Pop state from stack
     *
     * Restores all state saved by the last save() call.
     */
    void restore();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Gradients
    /// @{

    /**
     * @brief Create a linear gradient
     * @param x0 Start X coordinate
     * @param y0 Start Y coordinate
     * @param x1 End X coordinate
     * @param y1 End Y coordinate
     * @return A gradient object to add color stops to
     *
     * @par Example
     * @code
     * auto grad = canvas.createLinearGradient(0, 0, 200, 0);
     * grad.addColorStop(0.0, {1, 0, 0, 1});
     * grad.addColorStop(1.0, {0, 0, 1, 1});
     * canvas.fillStyle(grad);
     * @endcode
     */
    CanvasGradient createLinearGradient(float x0, float y0, float x1, float y1);

    /**
     * @brief Create a radial gradient
     * @param x0 Start circle center X
     * @param y0 Start circle center Y
     * @param r0 Start circle radius
     * @param x1 End circle center X
     * @param y1 End circle center Y
     * @param r1 End circle radius
     * @return A gradient object to add color stops to
     */
    CanvasGradient createRadialGradient(float x0, float y0, float r0,
                                        float x1, float y1, float r1);

    /**
     * @brief Create a conic (angular) gradient
     * @param startAngle Start angle in radians (0 = right, PI/2 = down)
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @return A gradient object to add color stops to
     */
    CanvasGradient createConicGradient(float startAngle, float x, float y);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Transforms
    /// @{

    /**
     * @brief Translate the coordinate system
     * @param x Horizontal translation in pixels
     * @param y Vertical translation in pixels
     */
    void translate(float x, float y);

    /**
     * @brief Rotate the coordinate system
     * @param radians Rotation angle in radians (clockwise)
     */
    void rotate(float radians);

    /**
     * @brief Scale the coordinate system
     * @param x Horizontal scale factor
     * @param y Vertical scale factor
     */
    void scale(float x, float y);

    /**
     * @brief Scale the coordinate system uniformly
     * @param uniform Scale factor for both axes
     */
    void scale(float uniform);

    /**
     * @brief Set the transform matrix directly
     * @param matrix 3x3 transformation matrix
     */
    void setTransform(const glm::mat3& matrix);

    /**
     * @brief Reset transform to identity
     */
    void resetTransform();

    /**
     * @brief Get the current transform matrix
     * @return Current 3x3 transformation matrix
     */
    glm::mat3 getTransform() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Path API
    /// @{

    /**
     * @brief Begin a new path, clearing any existing path
     */
    void beginPath();

    /**
     * @brief Close the current subpath by drawing a line to the start
     */
    void closePath();

    /**
     * @brief Move to a point without drawing
     * @param x X coordinate in pixels
     * @param y Y coordinate in pixels
     */
    void moveTo(float x, float y);

    /**
     * @brief Draw a line from current point to (x, y)
     * @param x X coordinate in pixels
     * @param y Y coordinate in pixels
     */
    void lineTo(float x, float y);

    /**
     * @brief Draw an arc
     * @param x Center X
     * @param y Center Y
     * @param radius Arc radius
     * @param startAngle Start angle in radians (0 = right)
     * @param endAngle End angle in radians
     * @param counterclockwise Draw counter-clockwise (default false)
     */
    void arc(float x, float y, float radius, float startAngle, float endAngle,
             bool counterclockwise = false);

    /**
     * @brief Draw an arc using tangent points
     * @param x1 First control point X
     * @param y1 First control point Y
     * @param x2 Second control point X
     * @param y2 Second control point Y
     * @param radius Arc radius
     */
    void arcTo(float x1, float y1, float x2, float y2, float radius);

    /**
     * @brief Draw a quadratic Bezier curve
     * @param cpx Control point X
     * @param cpy Control point Y
     * @param x End point X
     * @param y End point Y
     */
    void quadraticCurveTo(float cpx, float cpy, float x, float y);

    /**
     * @brief Draw a cubic Bezier curve
     * @param cp1x First control point X
     * @param cp1y First control point Y
     * @param cp2x Second control point X
     * @param cp2y Second control point Y
     * @param x End point X
     * @param y End point Y
     */
    void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);

    /**
     * @brief Add a rectangle subpath
     * @param x Left edge
     * @param y Top edge
     * @param w Width
     * @param h Height
     */
    void pathRect(float x, float y, float w, float h);

    /**
     * @brief Fill the current path with fillStyle color
     */
    void fill();

    /**
     * @brief Stroke the current path with strokeStyle color
     */
    void stroke();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Clipping
    /// @{

    /**
     * @brief Clip to the current path
     *
     * All subsequent drawing will be restricted to the area inside the current path.
     * Multiple clip() calls intersect (nested clips). Use save()/restore() to
     * manage clip state, or resetClip() to remove clipping.
     *
     * @par Example
     * @code
     * canvas.beginPath();
     * canvas.arc(100, 100, 50, 0, 2 * M_PI);  // Circle
     * canvas.clip();
     * // All drawing is now clipped to the circle
     * canvas.fillRect(0, 0, 200, 200);  // Only portion inside circle is visible
     * @endcode
     */
    void clip();

    /**
     * @brief Reset clipping to no clip region
     *
     * Removes all clipping, allowing drawing to the entire canvas.
     * Note: In HTML Canvas, there's no direct resetClip() - you use save/restore instead.
     * This is a Vivid extension for convenience.
     */
    void resetClip();

    /**
     * @brief Check if clipping is active
     * @return true if a clip region is currently set
     */
    bool isClipped() const { return m_state.clipDepth > 0; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Convenience Methods (HTML Canvas 2D-style)
    /// @{

    /**
     * @brief Fill a rectangle using fillStyle
     * @param x Left edge
     * @param y Top edge
     * @param w Width
     * @param h Height
     */
    void fillRect(float x, float y, float w, float h);

    /**
     * @brief Stroke a rectangle using strokeStyle
     * @param x Left edge
     * @param y Top edge
     * @param w Width
     * @param h Height
     */
    void strokeRect(float x, float y, float w, float h);

    /**
     * @brief Clear a rectangle to transparent
     * @param x Left edge
     * @param y Top edge
     * @param w Width
     * @param h Height
     */
    void clearRect(float x, float y, float w, float h);

    /**
     * @brief Fill a circle using fillStyle (Vivid extension)
     * @param x Center X
     * @param y Center Y
     * @param radius Radius
     * @param segments Number of segments (default 32)
     */
    void fillCircle(float x, float y, float radius, int segments = 32);

    /**
     * @brief Stroke a circle using strokeStyle (Vivid extension)
     * @param x Center X
     * @param y Center Y
     * @param radius Radius
     * @param segments Number of segments (default 32)
     */
    void strokeCircle(float x, float y, float radius, int segments = 32);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Image Drawing
    /// @{

    /**
     * @brief Draw another operator's output onto the canvas
     * @param source Operator to draw (must have been processed)
     * @param dx Destination X coordinate
     * @param dy Destination Y coordinate
     *
     * Draws the operator's output at its natural size.
     */
    void drawImage(Operator& source, float dx, float dy);

    /**
     * @brief Draw another operator's output with scaling
     * @param source Operator to draw (must have been processed)
     * @param dx Destination X coordinate
     * @param dy Destination Y coordinate
     * @param dw Destination width (scales image)
     * @param dh Destination height (scales image)
     */
    void drawImage(Operator& source, float dx, float dy, float dw, float dh);

    /**
     * @brief Draw a portion of another operator's output
     * @param source Operator to draw (must have been processed)
     * @param sx Source X (top-left of source region)
     * @param sy Source Y (top-left of source region)
     * @param sw Source width
     * @param sh Source height
     * @param dx Destination X coordinate
     * @param dy Destination Y coordinate
     * @param dw Destination width
     * @param dh Destination height
     */
    void drawImage(Operator& source,
                   float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh);

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
    /// @name Text Rendering
    /// @{

    /**
     * @brief Set text horizontal alignment
     * @param align TextAlign::Left, Right, Center, Start, or End
     *
     * Affects how the x coordinate in fillText is interpreted.
     */
    void textAlign(TextAlign align);

    /**
     * @brief Set text baseline alignment
     * @param baseline TextBaseline::Top, Hanging, Middle, Alphabetic, Ideographic, or Bottom
     *
     * Affects how the y coordinate in fillText is interpreted.
     */
    void textBaseline(TextBaseline baseline);

    /**
     * @brief Get current text alignment
     * @return Current TextAlign value
     */
    TextAlign getTextAlign() const { return m_state.textAlign; }

    /**
     * @brief Get current text baseline
     * @return Current TextBaseline value
     */
    TextBaseline getTextBaseline() const { return m_state.textBaseline; }

    /**
     * @brief Draw text at a position using fillStyle color
     * @param str Text string
     * @param x X position (interpreted based on textAlign)
     * @param y Y position (interpreted based on textBaseline)
     * @param letterSpacing Extra spacing between letters (default 0)
     *
     * Requires a font to be loaded via loadFont().
     * Position interpretation depends on textAlign and textBaseline settings.
     */
    void fillText(const std::string& str, float x, float y, float letterSpacing = 0.0f);

    /**
     * @brief Draw text centered at a position using fillStyle color
     * @param str Text string
     * @param x Center X in pixels
     * @param y Center Y in pixels
     * @param letterSpacing Extra spacing between letters (default 0)
     *
     * @note This method ignores textAlign/textBaseline and always centers.
     * @deprecated Use textAlign(Center) + textBaseline(Middle) + fillText() instead.
     */
    void fillTextCentered(const std::string& str, float x, float y, float letterSpacing = 0.0f);

    /**
     * @brief Measure text dimensions
     * @param str Text string to measure
     * @return Width and height in pixels
     */
    glm::vec2 measureText(const std::string& str) const;

    /**
     * @brief Get detailed text metrics (HTML Canvas 2D compatible)
     * @param str Text string to measure
     * @return TextMetrics with detailed bounding box information
     */
    TextMetrics measureTextMetrics(const std::string& str) const;

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
    // Helper methods
    glm::vec2 transformPoint(const glm::vec2& p) const;
    glm::vec4 applyAlpha(const glm::vec4& color) const;
    glm::vec4 getFillColorAt(const glm::vec2& pos) const;    ///< Get fill color at position (samples gradient if set)
    glm::vec4 getStrokeColorAt(const glm::vec2& pos) const;  ///< Get stroke color at position (samples gradient if set)
    std::vector<glm::vec2> pathToPolygon() const;
    void tessellateArc(std::vector<glm::vec2>& points, float cx, float cy, float radius,
                       float startAngle, float endAngle, bool ccw) const;
    void tessellateQuadratic(std::vector<glm::vec2>& points, const glm::vec2& start,
                             float cpx, float cpy, float x, float y) const;
    void tessellateBezier(std::vector<glm::vec2>& points, const glm::vec2& start,
                          float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) const;
    void generateStrokeGeometry(const std::vector<glm::vec2>& points, bool closed);

    std::unique_ptr<CanvasRenderer> m_renderer;
    std::unique_ptr<FontAtlas> m_font;

    // State management
    CanvasState m_state;
    std::vector<CanvasState> m_stateStack;

    // Path data
    std::vector<PathCommand> m_currentPath;
    glm::vec2 m_pathCursor = {0, 0};
    glm::vec2 m_pathStart = {0, 0};

    glm::vec4 m_clearColor = {0, 0, 0, 1};
    bool m_frameBegun = false;
};

} // namespace vivid::effects
