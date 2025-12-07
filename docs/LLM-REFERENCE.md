# Vivid LLM Reference

A compact reference for LLMs helping users build vivid projects.

## Project Structure

```
my-project/
  chain.cpp       # Your visual program (hot-reloaded)
  CLAUDE.md       # Optional: project-specific context for LLMs
```

## Basic Template

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Add operators here
    chain.add<Noise>("noise").scale(4.0f);

    // Specify what to display
    chain.output("noise");
}

void update(Context& ctx) {
    // Parameter tweaks go here (optional)
    // chain.process() is called automatically
}

VIVID_CHAIN(setup, update)
```

## How It Works

- **setup()** is called once on load and on each hot-reload
- **update()** is called every frame
- The core automatically calls `chain.init()` after setup and `chain.process()` after update
- Operator state (like Feedback buffers) is preserved across hot-reloads

## Keyboard Controls

- `Tab` - Toggle chain visualizer
- `F` - Toggle fullscreen
- `Esc` - Quit

## Operators Quick Reference

### Generators (no input required)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Noise` | Fractal noise | `.scale(4.0)` `.speed(0.5)` `.type(NoiseType::Perlin)` `.octaves(4)` |
| `SolidColor` | Solid color fill | `.color(r, g, b)` |
| `Gradient` | Color gradients | `.mode(GradientMode::Linear)` `.colorA(r,g,b)` `.colorB(r,g,b)` `.angle(0)` |
| `Ramp` | Animated HSV gradient | `.hueSpeed(0.5)` `.saturation(1.0)` `.type(RampType::Linear)` |
| `Shape` | SDF shapes | `.type(ShapeType::Circle)` `.size(0.5)` `.position(0.5, 0.5)` `.color(r,g,b)` |
| `LFO` | Oscillator (value) | `.waveform(LFOWaveform::Sine)` `.frequency(1.0)` `.amplitude(1.0)` |
| `Image` | Load image file | `.file("assets/image.jpg")` |

### Effects (require `.input()`)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Blur` | Gaussian blur | `.radius(5.0)` `.passes(1)` |
| `HSV` | Color adjustment | `.hueShift(0.0)` `.saturation(1.0)` `.value(1.0)` |
| `Brightness` | Brightness/contrast | `.brightness(1.0)` `.contrast(1.0)` `.gamma(1.0)` |
| `Transform` | Scale/rotate/translate | `.scale(1.0)` `.rotate(radians)` `.translate(x, y)` |
| `Mirror` | Axis mirroring | `.horizontal(true)` `.vertical(false)` `.kaleidoscope(4)` |
| `Displace` | Texture distortion | `.source(op)` `.map(op)` `.strength(0.1)` |
| `Edge` | Edge detection | `.strength(1.0)` |
| `Pixelate` | Mosaic effect | `.size(10)` |
| `Tile` | Texture tiling | `.tilesX(4)` `.tilesY(4)` |
| `ChromaticAberration` | RGB separation | `.amount(0.01)` `.angle(0)` |
| `Bloom` | Glow effect | `.threshold(0.8)` `.intensity(1.0)` `.radius(5.0)` |
| `Feedback` | Frame feedback | `.decay(0.95)` `.mix(0.5)` `.zoom(1.0)` `.rotate(0.01)` |

### Retro Effects

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Dither` | Ordered dithering | `.pattern(DitherPattern::Bayer4x4)` `.levels(8)` |
| `Quantize` | Color reduction | `.levels(8)` |
| `Scanlines` | CRT lines | `.spacing(2)` `.intensity(0.3)` `.thickness(0.5)` |
| `CRTEffect` | Full CRT sim | `.curvature(0.1)` `.vignette(0.3)` `.scanlines(0.2)` |
| `Downsample` | Low-res look | `.factor(4)` |

### Compositing

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Composite` | Blend two textures | `.inputA(op)` `.inputB(op)` `.mode(BlendMode::Over)` `.opacity(1.0)` |
| `Switch` | Select input | `.input0(op)` `.input1(op)` `.select(0)` |

### Particles

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Particles` | 2D particle system | `.emitRate(50)` `.life(2.0)` `.gravity(0.1)` `.size(0.02)` |
| `PointSprites` | Point-based particles | `.points(vector)` `.size(4.0)` |

### Math/Logic

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Math` | Math operations | `.operation(MathOp::Add)` `.value(0.5)` |
| `Logic` | Comparisons | `.operation(LogicOp::GreaterThan)` `.threshold(0.5)` |

### Canvas (2D Drawing)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Canvas` | Imperative 2D drawing | `.size(w, h)` `.loadFont(ctx, path, size)` |

Canvas is unique: you call draw methods in `update()`:

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Canvas>("canvas").size(1280, 720);
    chain.output("canvas");
}

void update(Context& ctx) {
    auto& canvas = ctx.chain().get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);  // Transparent background
    canvas.rectFilled(10, 10, 200, 50, {0.2f, 0.2f, 0.2f, 0.8f});
    canvas.circleFilled(640, 360, 50, {1, 0, 0, 1});
}
```

**Canvas drawing methods:**
- `clear(r, g, b, a)` - Clear canvas (call first)
- `rectFilled(x, y, w, h, color)` - Filled rectangle
- `rect(x, y, w, h, lineWidth, color)` - Rectangle outline
- `circleFilled(x, y, radius, color)` - Filled circle
- `circle(x, y, radius, lineWidth, color)` - Circle outline
- `line(x1, y1, x2, y2, width, color)` - Line segment
- `triangleFilled(a, b, c, color)` - Filled triangle (vec2 vertices)
- `text(str, x, y, color)` - Draw text (requires loadFont)
- `textCentered(str, x, y, color)` - Centered text
- `measureText(str)` - Returns glm::vec2 size

### 3D Rendering (vivid-render3d)

```cpp
#include <vivid/render3d/render3d.h>
using namespace vivid::render3d;
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `MeshBuilder` | Procedural geometry | `.box()` `.sphere()` `.cylinder()` `.cone()` `.torus()` `.plane()` |
| `Mesh` | GPU mesh data | `.upload(ctx)` `.release()` `.valid()` |
| `Camera3D` | Perspective camera | `.lookAt()` `.orbit()` `.fov()` `.near()` `.far()` |
| `Scene` | Multiple objects | `.add(mesh)` `.add(mesh, transform)` `.add(mesh, transform, color)` |
| `Render3D` | 3D renderer | `.scene()` `.camera()` `.shadingMode()` `.lightDirection()` |

**MeshBuilder primitives:**
```cpp
auto cube = MeshBuilder::box(1.0f, 1.0f, 1.0f);
auto sphere = MeshBuilder::sphere(0.5f, 24);        // radius, segments
auto cylinder = MeshBuilder::cylinder(0.5f, 2.0f, 16);  // radius, height, segments
auto cone = MeshBuilder::cone(0.5f, 2.0f, 16);
auto torus = MeshBuilder::torus(1.0f, 0.3f, 24, 12);  // outer, inner, segs, rings
auto plane = MeshBuilder::plane(10.0f, 10.0f, 4, 4);   // w, h, subdivX, subdivY
```

**MeshBuilder modifiers:**
```cpp
builder.transform(glm::mat4)     // Apply matrix
builder.translate(glm::vec3)     // Move vertices
builder.scale(float or vec3)     // Scale vertices
builder.rotate(angle, axis)      // Rotate (radians)
builder.computeNormals()         // Smooth normals
builder.computeFlatNormals()     // Faceted look
builder.mirror(Axis::X)          // Mirror + merge
builder.invert()                 // Flip normals
```

**CSG Boolean Operations:**
```cpp
auto cube = MeshBuilder::box(1.5f, 1.5f, 1.5f);
auto sphere = MeshBuilder::sphere(1.0f, 24);
cube.subtract(sphere);           // Carve sphere from cube
cube.add(otherMesh);             // Union
cube.intersect(otherMesh);       // Intersection
cube.computeFlatNormals();       // Recompute normals after CSG
Mesh mesh = cube.build();
mesh.upload(ctx);
```

**Scene setup:**
```cpp
Scene scene;
scene.add(mesh1);  // Identity transform, white
scene.add(mesh2, glm::translate(glm::mat4(1), glm::vec3(2, 0, 0)));
scene.add(mesh3, transform, glm::vec4(1, 0, 0, 1));  // Red color
```

**Render3D usage:**
```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    Camera3D camera;
    camera.lookAt(glm::vec3(5, 3, 5), glm::vec3(0, 0, 0))
          .fov(45.0f)
          .nearPlane(0.1f)
          .farPlane(100.0f);

    auto& renderer = chain.add<Render3D>("render3d");
    renderer.scene(scene)
            .camera(camera)
            .shadingMode(ShadingMode::Flat)
            .lightDirection(glm::normalize(glm::vec3(1, 2, 1)))
            .lightColor(glm::vec3(1, 1, 1))
            .ambient(0.15f)
            .clearColor(0.1f, 0.1f, 0.15f)
            .resolution(1280, 720);

    chain.output("render3d");
}

void update(Context& ctx) {
    // Animate camera
    camera.orbit(5.0f, ctx.time() * 0.3f, 0.4f);
    ctx.chain().get<Render3D>("render3d").camera(camera);
}
```

**Shading modes:**
```cpp
ShadingMode::Unlit    // No lighting, color only
ShadingMode::Flat     // Per-fragment lighting (faceted)
ShadingMode::Gouraud  // Per-vertex lighting (PS1-style smooth)
```

## Enum Types

```cpp
// Noise types
NoiseType::Perlin, NoiseType::Simplex, NoiseType::Worley, NoiseType::Value

// Shape types
ShapeType::Circle, ShapeType::Rectangle, ShapeType::RoundedRect,
ShapeType::Triangle, ShapeType::Star, ShapeType::Ring, ShapeType::Polygon

// LFO waveforms
LFOWaveform::Sine, LFOWaveform::Triangle, LFOWaveform::Saw,
LFOWaveform::Square, LFOWaveform::Noise

// Blend modes
BlendMode::Over, BlendMode::Add, BlendMode::Multiply,
BlendMode::Screen, BlendMode::Overlay, BlendMode::Difference

// Gradient modes
GradientMode::Linear, GradientMode::Radial, GradientMode::Angular, GradientMode::Diamond

// Dither patterns
DitherPattern::Bayer2x2, DitherPattern::Bayer4x4, DitherPattern::Bayer8x8
```

## Common Patterns

### Chain operators together

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise").scale(4.0f);
    chain.add<HSV>("color").input("noise").hueShift(0.3f);
    chain.add<Blur>("blur").input("color").radius(3.0f);
    chain.output("blur");
}
```

### Feedback loop (trails/echoes)

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise").scale(4.0f);
    chain.add<Feedback>("fb").input("noise").decay(0.95f).zoom(1.01f);
    chain.output("fb");
}
```

### Blend two sources

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise");
    chain.add<Image>("img").file("assets/photo.jpg");
    chain.add<Composite>("blend")
        .inputA("img")
        .inputB("noise")
        .mode(BlendMode::Overlay)
        .opacity(0.5f);
    chain.output("blend");
}
```

### Displace with noise

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    auto& img = chain.add<Image>("img").file("assets/photo.jpg");
    auto& noise = chain.add<Noise>("noise").scale(10.0f).speed(0.3f);
    chain.add<Displace>("warp")
        .source(&img)
        .map(&noise)
        .strength(0.05f);
    chain.output("warp");
}
```

### Retro/VHS look

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<VideoPlayer>("vid").file("assets/video.mov");
    chain.add<ChromaticAberration>("chroma").input("vid").amount(0.005f);
    chain.add<Scanlines>("lines").input("chroma").spacing(3).intensity(0.2f);
    chain.add<Quantize>("quant").input("lines").levels(32);
    chain.output("quant");
}
```

### Particle system

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Particles>("particles")
        .emitRate(100.0f)
        .life(3.0f)
        .gravity(0.05f)
        .size(0.01f, 0.005f)  // start, end
        .color(1.0f, 0.5f, 0.2f)
        .velocity(0.0f, -0.1f)
        .spread(30.0f);
    chain.output("particles");
}
```

### Dynamic output switching

```cpp
void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Switch output based on key press
    if (ctx.key(GLFW_KEY_1).pressed) {
        chain.output("effect1");
    } else if (ctx.key(GLFW_KEY_2).pressed) {
        chain.output("effect2");
    }
}
```

## Context API

```cpp
// In update() - available on ctx:
float time = ctx.time();      // Seconds since start
float dt = ctx.dt();          // Delta time (seconds)
int frame = ctx.frame();      // Frame number
int width = ctx.width();      // Output width
int height = ctx.height();    // Output height
glm::vec2 mouse = ctx.mouseNorm();  // Normalized mouse position (-1 to 1)
auto key = ctx.key(GLFW_KEY_SPACE); // Key state (.pressed, .held, .released)
```

## Troubleshooting

**"No output showing"**
- Make sure you have `chain.output("name")` pointing to a valid operator
- Check that all operators are connected in a chain

**"Operator not found"**
- Verify the operator name matches exactly (case-sensitive)
- Make sure you're using `chain.get<Type>("name")`

**"Hot reload not working"**
- Save the file
- Check for compile errors in the terminal

**"Black screen"**
- Check if input connections are correct
- Verify image/video paths are relative to project folder
