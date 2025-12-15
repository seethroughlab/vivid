# Vivid Canvas API Reference

This document provides a complete reference for the Vivid Canvas 2D drawing API, modeled after the [HTML Canvas 2D API](https://developer.mozilla.org/en-US/docs/Web/API/CanvasRenderingContext2D).

## Table of Contents

- [Getting Started](#getting-started)
- [Drawing Rectangles](#drawing-rectangles)
- [Drawing Paths](#drawing-paths)
- [Drawing Text](#drawing-text)
- [Line Styles](#line-styles)
- [Fill and Stroke Styles](#fill-and-stroke-styles)
- [Gradients](#gradients)
- [Transformations](#transformations)
- [Compositing and Clipping](#compositing-and-clipping)
- [Drawing Images](#drawing-images)
- [Canvas State](#canvas-state)
- [Vivid Extensions](#vivid-extensions)
- [Differences from HTML Canvas](#differences-from-html-canvas)

---

## Getting Started

Canvas is a TextureOperator that provides imperative 2D drawing commands. Add it to your chain and call drawing methods in your `update()` function.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);
    chain.output("canvas");
}

void update(Context& ctx) {
    auto& canvas = ctx.chain().get<Canvas>("canvas");

    // Clear with dark background
    canvas.clear(0.1f, 0.1f, 0.2f, 1.0f);

    // Draw a red rectangle
    canvas.fillStyle(1.0f, 0.0f, 0.0f, 1.0f);
    canvas.fillRect(100, 100, 200, 150);
}

VIVID_CHAIN(setup, update)
```

---

## Drawing Rectangles

The Canvas API provides three methods for drawing rectangles.

### `fillRect(x, y, width, height)`

Draws a filled rectangle at (x, y) with the given dimensions using the current `fillStyle`.

```cpp
canvas.fillStyle(0.2f, 0.4f, 0.8f, 1.0f);  // Blue
canvas.fillRect(10, 10, 200, 100);
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| x | float | Left edge of the rectangle |
| y | float | Top edge of the rectangle |
| width | float | Width in pixels |
| height | float | Height in pixels |

### `strokeRect(x, y, width, height)`

Draws the outline of a rectangle at (x, y) using the current `strokeStyle` and `lineWidth`.

```cpp
canvas.strokeStyle(1.0f, 1.0f, 1.0f, 1.0f);  // White
canvas.lineWidth(2.0f);
canvas.strokeRect(10, 10, 200, 100);
```

**Parameters:** Same as `fillRect`.

### `clearRect(x, y, width, height)`

Clears the specified rectangular area, making it fully transparent.

```cpp
canvas.clearRect(50, 50, 100, 100);  // Clear a hole in the canvas
```

**Parameters:** Same as `fillRect`.

---

## Drawing Paths

Paths allow drawing complex shapes by defining a series of connected points.

### Path Methods

#### `beginPath()`

Starts a new path, clearing any existing path data.

```cpp
canvas.beginPath();
```

#### `closePath()`

Closes the current subpath by drawing a straight line back to the starting point.

```cpp
canvas.beginPath();
canvas.moveTo(100, 100);
canvas.lineTo(200, 100);
canvas.lineTo(150, 50);
canvas.closePath();  // Draws line from (150,50) back to (100,100)
canvas.fill();
```

#### `moveTo(x, y)`

Moves the pen to (x, y) without drawing.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| x | float | X coordinate |
| y | float | Y coordinate |

#### `lineTo(x, y)`

Draws a line from the current position to (x, y).

```cpp
canvas.beginPath();
canvas.moveTo(0, 0);
canvas.lineTo(100, 100);
canvas.stroke();
```

#### `arc(x, y, radius, startAngle, endAngle, counterclockwise)`

Draws an arc centered at (x, y) with the given radius.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| x | float | Center X coordinate |
| y | float | Center Y coordinate |
| radius | float | Arc radius |
| startAngle | float | Start angle in radians (0 = right/3 o'clock) |
| endAngle | float | End angle in radians |
| counterclockwise | bool | Draw counter-clockwise (default: false) |

```cpp
// Full circle
canvas.beginPath();
canvas.arc(100, 100, 50, 0, 2 * M_PI);
canvas.fill();

// Half circle (top)
canvas.beginPath();
canvas.arc(200, 100, 50, M_PI, 0);  // PI to 0 draws top half
canvas.fill();
```

#### `arcTo(x1, y1, x2, y2, radius)`

Draws an arc using tangent points. The arc is tangent to the line from the current point to (x1, y1) and tangent to the line from (x1, y1) to (x2, y2).

```cpp
canvas.beginPath();
canvas.moveTo(50, 50);
canvas.arcTo(100, 50, 100, 100, 25);  // Rounded corner
canvas.lineTo(100, 150);
canvas.stroke();
```

#### `quadraticCurveTo(cpx, cpy, x, y)`

Draws a quadratic Bezier curve.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| cpx | float | Control point X |
| cpy | float | Control point Y |
| x | float | End point X |
| y | float | End point Y |

```cpp
canvas.beginPath();
canvas.moveTo(50, 200);
canvas.quadraticCurveTo(100, 100, 150, 200);  // Curve up then down
canvas.stroke();
```

#### `bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y)`

Draws a cubic Bezier curve.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| cp1x | float | First control point X |
| cp1y | float | First control point Y |
| cp2x | float | Second control point X |
| cp2y | float | Second control point Y |
| x | float | End point X |
| y | float | End point Y |

```cpp
canvas.beginPath();
canvas.moveTo(50, 200);
canvas.bezierCurveTo(75, 100, 125, 300, 150, 200);  // S-curve
canvas.stroke();
```

#### `pathRect(x, y, width, height)`

Adds a rectangular subpath to the current path (does not draw immediately).

```cpp
canvas.beginPath();
canvas.pathRect(10, 10, 100, 50);
canvas.pathRect(120, 10, 100, 50);  // Add another rectangle
canvas.fill();  // Fills both rectangles
```

### Path Drawing Methods

#### `fill()`

Fills the current path with the current `fillStyle`.

```cpp
canvas.beginPath();
canvas.arc(100, 100, 50, 0, 2 * M_PI);
canvas.fillStyle(1.0f, 0.0f, 0.0f, 1.0f);
canvas.fill();
```

#### `stroke()`

Strokes (outlines) the current path with the current `strokeStyle`.

```cpp
canvas.beginPath();
canvas.arc(100, 100, 50, 0, 2 * M_PI);
canvas.strokeStyle(1.0f, 1.0f, 1.0f, 1.0f);
canvas.lineWidth(3.0f);
canvas.stroke();
```

---

## Drawing Text

Canvas supports bitmap font rendering for text.

### Loading Fonts

Before drawing text, load a TTF font:

```cpp
void setup(Context& ctx) {
    auto& canvas = ctx.chain().get<Canvas>("canvas");
    canvas.loadFont(ctx, "fonts/Roboto-Regular.ttf", 24.0f);
}
```

### Text Methods

#### `fillText(text, x, y, letterSpacing)`

Draws filled text at (x, y) using the current `fillStyle`.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| text | string | Text to draw |
| x | float | X position |
| y | float | Y position |
| letterSpacing | float | Extra space between letters (default: 0) |

```cpp
canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
canvas.fillText("Hello, World!", 100, 100);
```

#### `textAlign(align)`

Sets horizontal text alignment.

**Values:**
| Value | Description |
|-------|-------------|
| `TextAlign::Left` | Align left edge to X position (default) |
| `TextAlign::Right` | Align right edge to X position |
| `TextAlign::Center` | Center text on X position |
| `TextAlign::Start` | Same as Left (LTR languages) |
| `TextAlign::End` | Same as Right (LTR languages) |

```cpp
canvas.textAlign(TextAlign::Center);
canvas.fillText("Centered", 640, 360);  // Centered at (640, 360)
```

#### `textBaseline(baseline)`

Sets vertical text alignment.

**Values:**
| Value | Description |
|-------|-------------|
| `TextBaseline::Top` | Top of em square |
| `TextBaseline::Hanging` | Hanging baseline |
| `TextBaseline::Middle` | Middle of em square |
| `TextBaseline::Alphabetic` | Normal baseline (default) |
| `TextBaseline::Ideographic` | Bottom of ideographic characters |
| `TextBaseline::Bottom` | Bottom of em square |

```cpp
canvas.textBaseline(TextBaseline::Middle);
canvas.textAlign(TextAlign::Center);
canvas.fillText("Perfectly Centered", 640, 360);
```

#### `measureText(text)` / `measureTextMetrics(text)`

Measures text dimensions without drawing.

```cpp
glm::vec2 size = canvas.measureText("Hello");
float width = size.x;
float height = size.y;

// More detailed metrics
TextMetrics metrics = canvas.measureTextMetrics("Hello");
float advance = metrics.width;
float ascent = metrics.actualBoundingBoxAscent;
```

---

## Line Styles

Control the appearance of stroked lines.

### `lineWidth(width)`

Sets the line thickness in pixels.

```cpp
canvas.lineWidth(5.0f);
canvas.strokeRect(10, 10, 100, 100);
```

### `lineCap(cap)`

Sets the style of line endpoints.

**Values:**
| Value | Description |
|-------|-------------|
| `LineCap::Butt` | Flat end at exactly the endpoint (default) |
| `LineCap::Round` | Semicircle at endpoint |
| `LineCap::Square` | Flat end extended by half line width |

```cpp
canvas.lineCap(LineCap::Round);
canvas.beginPath();
canvas.moveTo(50, 100);
canvas.lineTo(200, 100);
canvas.stroke();
```

### `lineJoin(join)`

Sets the style of corners where lines meet.

**Values:**
| Value | Description |
|-------|-------------|
| `LineJoin::Miter` | Sharp corner (default) |
| `LineJoin::Round` | Rounded corner |
| `LineJoin::Bevel` | Flat diagonal corner |

```cpp
canvas.lineJoin(LineJoin::Round);
canvas.beginPath();
canvas.moveTo(50, 150);
canvas.lineTo(100, 50);
canvas.lineTo(150, 150);
canvas.stroke();
```

### `miterLimit(limit)`

Sets the maximum miter length for sharp corners (only affects `LineJoin::Miter`).

```cpp
canvas.miterLimit(5.0f);
```

---

## Fill and Stroke Styles

### `fillStyle(color)` / `fillStyle(r, g, b, a)`

Sets the color or gradient used for fill operations.

```cpp
// Using glm::vec4
canvas.fillStyle(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

// Using separate components
canvas.fillStyle(1.0f, 0.0f, 0.0f, 1.0f);  // Red

// Using gradient
auto gradient = canvas.createLinearGradient(0, 0, 200, 0);
gradient.addColorStop(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);  // Red
gradient.addColorStop(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);  // Blue
canvas.fillStyle(gradient);
```

### `strokeStyle(color)` / `strokeStyle(r, g, b, a)`

Sets the color or gradient used for stroke operations.

```cpp
canvas.strokeStyle(1.0f, 1.0f, 1.0f, 1.0f);  // White outline
```

### `globalAlpha(alpha)`

Sets a global transparency multiplier applied to all drawing operations.

```cpp
canvas.globalAlpha(0.5f);  // 50% transparent
canvas.fillRect(0, 0, 100, 100);  // Semi-transparent
```

---

## Gradients

Create gradients to use as fill or stroke styles.

### `createLinearGradient(x0, y0, x1, y1)`

Creates a linear gradient along a line from (x0, y0) to (x1, y1).

```cpp
auto gradient = canvas.createLinearGradient(0, 0, 200, 0);  // Horizontal
gradient.addColorStop(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);  // Red at start
gradient.addColorStop(0.5f, 1.0f, 1.0f, 0.0f, 1.0f);  // Yellow in middle
gradient.addColorStop(1.0f, 0.0f, 1.0f, 0.0f, 1.0f);  // Green at end
canvas.fillStyle(gradient);
canvas.fillRect(0, 0, 200, 100);
```

### `createRadialGradient(x0, y0, r0, x1, y1, r1)`

Creates a radial gradient between two circles.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| x0, y0 | float | Center of start circle |
| r0 | float | Radius of start circle (can be 0) |
| x1, y1 | float | Center of end circle |
| r1 | float | Radius of end circle |

```cpp
// Gradient from center outward
auto gradient = canvas.createRadialGradient(100, 100, 0, 100, 100, 80);
gradient.addColorStop(0.0f, 1.0f, 1.0f, 1.0f, 1.0f);  // White center
gradient.addColorStop(1.0f, 0.0f, 0.0f, 0.5f, 1.0f);  // Dark purple edge
canvas.fillStyle(gradient);
canvas.fillCircle(100, 100, 80);
```

### `createConicGradient(startAngle, x, y)`

Creates a conic (angular) gradient around a point.

```cpp
auto gradient = canvas.createConicGradient(0, 100, 100);  // Start at right
gradient.addColorStop(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
gradient.addColorStop(0.33f, 0.0f, 1.0f, 0.0f, 1.0f);
gradient.addColorStop(0.66f, 0.0f, 0.0f, 1.0f, 1.0f);
gradient.addColorStop(1.0f, 1.0f, 0.0f, 0.0f, 1.0f);
canvas.fillStyle(gradient);
canvas.fillCircle(100, 100, 80);
```

### `CanvasGradient::addColorStop(offset, r, g, b, a)`

Adds a color stop to a gradient.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| offset | float | Position in gradient (0.0 to 1.0) |
| r, g, b, a | float | Color components (0.0 to 1.0) |

**Note:** Maximum 8 color stops per gradient (GPU uniform limit).

---

## Transformations

Transform the coordinate system to position, rotate, and scale drawings.

### `translate(x, y)`

Moves the canvas origin.

```cpp
canvas.translate(100, 100);  // Move origin to (100, 100)
canvas.fillRect(0, 0, 50, 50);  // Draws at (100, 100)
```

### `rotate(radians)`

Rotates the canvas around the current origin.

```cpp
canvas.translate(100, 100);  // Move to center of rotation
canvas.rotate(0.785f);       // Rotate 45 degrees
canvas.fillRect(-25, -25, 50, 50);  // Draw centered, rotated square
```

### `scale(x, y)` / `scale(uniform)`

Scales the canvas coordinate system.

```cpp
canvas.scale(2.0f, 2.0f);    // Everything is 2x bigger
canvas.fillRect(10, 10, 50, 50);  // Actually 100x100 pixels

canvas.scale(0.5f);  // Uniform scale to 50%
```

### `setTransform(matrix)`

Sets the transform matrix directly (3x3 matrix).

```cpp
glm::mat3 matrix = glm::mat3(1.0f);  // Identity
canvas.setTransform(matrix);
```

### `resetTransform()`

Resets the transform to identity (no transformation).

```cpp
canvas.resetTransform();
```

### `getTransform()`

Returns the current transformation matrix.

```cpp
glm::mat3 current = canvas.getTransform();
```

### Transform Example

```cpp
// Draw a rotating star
canvas.save();
canvas.translate(640, 360);          // Move to center
canvas.rotate(ctx.time());           // Rotate over time
canvas.fillStyle(1.0f, 0.9f, 0.2f);  // Yellow
// Draw star shape at origin...
canvas.restore();
```

---

## Compositing and Clipping

### Clipping

Restrict drawing to a specific region.

#### `clip()`

Uses the current path as a clipping region. All subsequent drawing is restricted to inside the path.

```cpp
// Draw content clipped to a circle
canvas.save();
canvas.beginPath();
canvas.arc(100, 100, 50, 0, 2 * M_PI);
canvas.clip();

// This gradient is only visible inside the circle
canvas.fillStyle(canvas.createLinearGradient(50, 50, 150, 150));
canvas.fillRect(0, 0, 200, 200);

canvas.restore();  // Remove clipping
```

**Important:** Use `save()` before `clip()` and `restore()` after to remove the clipping region.

#### `resetClip()`

Removes all clipping (Vivid extension, not in HTML Canvas).

```cpp
canvas.resetClip();  // Drawing is no longer clipped
```

#### `isClipped()`

Returns true if a clipping region is active.

```cpp
if (canvas.isClipped()) {
    // Currently drawing inside a clip region
}
```

### Nested Clipping

Multiple `clip()` calls intersect (only the overlapping region remains):

```cpp
canvas.save();

// First clip: circle
canvas.beginPath();
canvas.arc(100, 100, 60, 0, 2 * M_PI);
canvas.clip();

// Second clip: rectangle (intersects with circle)
canvas.beginPath();
canvas.pathRect(80, 50, 100, 100);
canvas.clip();

// Drawing is now clipped to the intersection
canvas.fillRect(0, 0, 200, 200);

canvas.restore();
```

---

## Drawing Images

Draw other operators' outputs onto the canvas.

### `drawImage(source, dx, dy)`

Draws an operator's output at its natural size.

```cpp
auto& noise = chain.get<Noise>("noise");
canvas.drawImage(noise, 100, 100);  // Draw at (100, 100)
```

### `drawImage(source, dx, dy, dw, dh)`

Draws an operator's output scaled to fit the destination rectangle.

```cpp
// Scale to half size
canvas.drawImage(noise, 100, 100, 320, 180);
```

### `drawImage(source, sx, sy, sw, sh, dx, dy, dw, dh)`

Draws a portion of an operator's output (sprite sheet style).

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| sx, sy | float | Source rectangle top-left |
| sw, sh | float | Source rectangle size |
| dx, dy | float | Destination top-left |
| dw, dh | float | Destination size |

```cpp
// Draw top-left quarter, scaled to 200x200
canvas.drawImage(noise, 0, 0, 320, 180, 100, 100, 200, 200);
```

---

## Canvas State

Save and restore the complete drawing state.

### `save()`

Pushes the current state onto a stack. State includes:
- `fillStyle` and `strokeStyle`
- `lineWidth`, `lineCap`, `lineJoin`, `miterLimit`
- `globalAlpha`
- Current transform matrix
- Clipping region
- Text alignment settings

```cpp
canvas.save();
```

### `restore()`

Pops the most recently saved state from the stack.

```cpp
canvas.save();
canvas.fillStyle(1.0f, 0.0f, 0.0f);
canvas.translate(100, 100);
canvas.rotate(0.5f);
// ... draw something ...
canvas.restore();  // Back to previous state
```

### State Stack Example

```cpp
canvas.fillStyle(1.0f, 1.0f, 1.0f);  // White

canvas.save();
    canvas.fillStyle(1.0f, 0.0f, 0.0f);  // Red
    canvas.save();
        canvas.fillStyle(0.0f, 1.0f, 0.0f);  // Green
        canvas.fillRect(0, 0, 50, 50);       // Green rectangle
    canvas.restore();
    canvas.fillRect(60, 0, 50, 50);          // Red rectangle
canvas.restore();
canvas.fillRect(120, 0, 50, 50);             // White rectangle
```

---

## Vivid Extensions

These methods are not part of the HTML Canvas 2D standard but are provided for convenience.

### `fillCircle(x, y, radius, segments)`

Fills a circle using the current `fillStyle`.

```cpp
canvas.fillStyle(1.0f, 0.5f, 0.0f);
canvas.fillCircle(100, 100, 50);  // Orange circle
```

### `strokeCircle(x, y, radius, segments)`

Strokes a circle using the current `strokeStyle`.

```cpp
canvas.strokeStyle(1.0f, 1.0f, 1.0f);
canvas.strokeCircle(100, 100, 50);  // White circle outline
```

### `resetClip()`

Removes all clipping regions. In standard HTML Canvas, you must use `save()`/`restore()` to manage clipping.

### `fillTextCentered(text, x, y)`

**Deprecated.** Use `textAlign(TextAlign::Center)` + `textBaseline(TextBaseline::Middle)` + `fillText()` instead.

---

## Differences from HTML Canvas

| Feature | HTML Canvas | Vivid Canvas |
|---------|-------------|--------------|
| Context creation | `canvas.getContext('2d')` | `chain.add<Canvas>("name")` |
| Colors | CSS strings `"#ff0000"` | Float components (0-1) |
| Font loading | CSS fonts, `font` property | `loadFont(ctx, path, size)` |
| Font string | `"24px Arial"` | Not supported (use loadFont) |
| `strokeText()` | Supported | Not yet implemented |
| `globalCompositeOperation` | Multiple blend modes | Not yet implemented |
| `shadowBlur/Color/Offset` | Supported | Not yet implemented |
| `getImageData/putImageData` | Pixel access | Not yet implemented |
| `createPattern()` | Pattern fills | Not yet implemented |
| `resetClip()` | Not available | Vivid extension |
| `fillCircle/strokeCircle` | Not available | Vivid extension |
| Gradients | Unlimited stops | Max 8 color stops |

---

## Complete Example

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);
    canvas.loadFont(ctx, "fonts/Roboto-Regular.ttf", 32.0f);
    chain.output("canvas");
}

void update(Context& ctx) {
    auto& canvas = ctx.chain().get<Canvas>("canvas");
    float time = ctx.time();

    // Clear with dark background
    canvas.clear(0.1f, 0.1f, 0.2f, 1.0f);

    // Gradient background rectangle
    auto bgGrad = canvas.createLinearGradient(0, 0, 1280, 720);
    bgGrad.addColorStop(0.0f, 0.1f, 0.1f, 0.3f, 1.0f);
    bgGrad.addColorStop(1.0f, 0.2f, 0.1f, 0.2f, 1.0f);
    canvas.fillStyle(bgGrad);
    canvas.fillRect(0, 0, 1280, 720);

    // Animated bouncing ball
    float ballX = 640 + 200 * std::sin(time * 1.5f);
    float ballY = 360 + 100 * std::sin(time * 2.3f);
    canvas.fillStyle(1.0f, 0.4f, 0.2f);
    canvas.fillCircle(ballX, ballY, 40);

    // Rotating squares
    for (int i = 0; i < 4; i++) {
        float angle = time * 0.5f + i * 1.5708f;
        float x = 640 + 180 * std::cos(angle);
        float y = 360 + 180 * std::sin(angle);

        canvas.save();
        canvas.translate(x, y);
        canvas.rotate(time + i);
        canvas.fillStyle(0.5f + 0.5f * std::sin(time + i),
                        0.5f + 0.5f * std::cos(time + i),
                        0.8f, 1.0f);
        canvas.fillRect(-25, -25, 50, 50);
        canvas.restore();
    }

    // Clipped gradient circle
    canvas.save();
    canvas.beginPath();
    canvas.arc(150, 600, 60, 0, 6.28318f);
    canvas.clip();
    auto clipGrad = canvas.createLinearGradient(90, 540, 210, 660);
    clipGrad.addColorStop(0.0f, 1.0f, 0.0f, 0.5f, 1.0f);
    clipGrad.addColorStop(1.0f, 0.0f, 0.5f, 1.0f, 1.0f);
    canvas.fillStyle(clipGrad);
    canvas.fillRect(90, 540, 120, 120);
    canvas.restore();

    // Circle outline
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.8f);
    canvas.lineWidth(2.0f);
    canvas.strokeCircle(150, 600, 60);

    // Text
    canvas.textAlign(TextAlign::Center);
    canvas.textBaseline(TextBaseline::Middle);
    canvas.fillStyle(1.0f, 1.0f, 1.0f);
    canvas.fillText("Vivid Canvas API", 640, 50);
}

VIVID_CHAIN(setup, update)
```

---

## See Also

- [MDN Canvas Tutorial](https://developer.mozilla.org/en-US/docs/Web/API/Canvas_API/Tutorial)
- [MDN CanvasRenderingContext2D](https://developer.mozilla.org/en-US/docs/Web/API/CanvasRenderingContext2D)
- [Vivid Operator API](OPERATOR-API.md)
- [Vivid Recipes](RECIPES.md)
