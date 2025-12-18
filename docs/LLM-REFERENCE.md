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

    // Add operators and configure via direct assignment
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;

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
- `V` - Toggle vsync (in examples that support it)
- `Esc` - Quit
- `Ctrl+Drag` - Pan the chain visualizer

## Operators Quick Reference

### Generators (no input required)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Noise` | Fractal noise | `scale` `speed` `octaves` `.type(NoiseType::Perlin)` |
| `SolidColor` | Solid color fill | `color.set(r, g, b)` |
| `Gradient` | Color gradients | `.mode(GradientMode::Linear)` `colorA.set(r,g,b)` `colorB.set(r,g,b)` `angle` |
| `Ramp` | Animated HSV gradient | `hueSpeed` `saturation` `.type(RampType::Linear)` |
| `Shape` | SDF shapes | `.type(ShapeType::Circle)` `size.set(w,h)` `position.set(x,y)` `color.set(r,g,b,a)` |
| `LFO` | Oscillator (value) | `.waveform(LFOWaveform::Sine)` `frequency` `amplitude` |
| `Image` | Load image file | `file = "assets/image.jpg"` |

### Effects (require `.input(&op)`)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Blur` | Gaussian blur | `radius` `passes` |
| `HSV` | Color adjustment | `hueShift` `saturation` `value` |
| `Brightness` | Brightness/contrast | `brightness` `contrast` `gamma` |
| `Transform` | Scale/rotate/translate | `scale.set(x,y)` `rotation` `translate.set(x,y)` |
| `Mirror` | Axis mirroring | `.mode(MirrorMode::Kaleidoscope)` `segments` |
| `Displace` | Texture distortion | `.source(&op)` `.map(&op)` `strength` |
| `Edge` | Edge detection | `strength` |
| `Pixelate` | Mosaic effect | `size.set(w,h)` |
| `Tile` | Texture tiling | `tilesX` `tilesY` |
| `ChromaticAberration` | RGB separation | `amount` `.radial(true)` |
| `Bloom` | Glow effect | `threshold` `intensity` `radius` |
| `Vignette` | Edge darkening | `intensity` `softness` `roundness` |
| `BarrelDistortion` | CRT screen curvature | `curvature` |
| `Feedback` | Frame feedback | `decay` `mix` `zoom` `rotation` |
| `FrameCache` | Buffer N frames | `frameCount` |
| `TimeMachine` | Temporal displacement | `.cache(&fc)` `.displacementMap(&tex)` `depth` |
| `Plexus` | Particle network | `.setNodeCount(200)` `.setConnectionDistance(0.1)` `.setTurbulence(0.1)` |

### Retro Effects

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Dither` | Ordered dithering | `.pattern(DitherPattern::Bayer4x4)` `levels` |
| `Quantize` | Color reduction | `levels` |
| `Scanlines` | CRT lines | `spacing` `intensity` `thickness` |
| `CRTEffect` | Full CRT sim | `curvature` `vignette` `scanlines` |
| `Downsample` | Low-res look | `targetW` `targetH` |

### Compositing

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Composite` | Blend two textures | `.inputA(&op)` `.inputB(&op)` `.mode(BlendMode::Over)` `opacity` |
| `Switch` | Select input | `.input(0, &op)` `.input(1, &op)` `index` `blend` |

### Particles

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Particles` | 2D particle system | `.emitRate(50)` `.life(2.0)` `.gravity(0.1)` `.size(0.02)` |
| `PointSprites` | Point-based particles | `.points(vector)` `.size(4.0)` |

### Math/Logic

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Math` | Math operations | `.operation(MathOperation::Add)` `inputA` `inputB` |
| `Logic` | Comparisons | `.operation(LogicOperation::GreaterThan)` `inputA` `inputB` |

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

### Media (vivid-video)

```cpp
#include <vivid/video/video.h>
using namespace vivid::video;
```

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `VideoPlayer` | Video playback | `.file(path)` `.loop(true)` `.play()` `.pause()` `.seek(time)` `.speed(1.0)` |
| `Webcam` | Camera capture | `.resolution(1280, 720)` `.frameRate(30)` |

**VideoPlayer usage:**
```cpp
auto& video = chain.add<VideoPlayer>("video")
    .file("assets/videos/clip.mov")
    .loop(true);

// In update() - control playback
video.isPlaying() ? video.pause() : video.play();
video.seek(5.0f);  // Jump to 5 seconds
float t = video.currentTime();
```

**Supported codecs:** HAP (best performance), H.264, ProRes, MPEG-2

### Audio (vivid-audio)

```cpp
#include <vivid/audio/audio.h>
using namespace vivid::audio;
```

**Audio Sources:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `AudioIn` | Microphone/line input | `.device(index)` |
| `AudioFile` | Audio file playback | `.file(path)` `.loop(true)` |

**Synthesis:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Oscillator` | Waveform generator | `.waveform(Waveform::Saw)` `.frequency(440)` `.amplitude(0.5)` |
| `NoiseGen` | Noise generator | `.type(NoiseType::White)` `.amplitude(0.5)` |
| `Crackle` | Vinyl crackle | `.density(0.3)` `.amplitude(0.5)` |
| `Formant` | Vowel formant filter | `.vowel(Vowel::A)` `.morph(0.5)` `.resonance(8.0)` `.mix(1.0)` |
| `Synth` | Oscillator + ADSR | `.setWaveform(Waveform::Saw)` `frequency` `attack` `decay` `sustain` `release` |

**Synth usage:**
```cpp
auto& synth = chain.add<Synth>("synth");
synth.setWaveform(Waveform::Saw);
synth.frequency = 440.0f;
synth.attack = 0.01f;
synth.release = 0.5f;

// Control playback
synth.noteOn();   // Start attack
synth.noteOff();  // Start release
```

**Formant vowel presets:**
- `Vowel::A` - "ah" as in "father" (800, 1200, 2500 Hz)
- `Vowel::E` - "eh" as in "bed" (400, 2000, 2600 Hz)
- `Vowel::I` - "ee" as in "feet" (300, 2300, 3000 Hz)
- `Vowel::O` - "oh" as in "boat" (500, 800, 2500 Hz)
- `Vowel::U` - "oo" as in "boot" (350, 600, 2400 Hz)
- `Vowel::Custom` - User-defined via `.f1()` `.f2()` `.f3()`

**Drum Synthesis:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Kick` | Kick drum | `.pitch(60)` `.decay(0.3)` `.drive(0.5)` |
| `Snare` | Snare drum | `.tone(200)` `.noise(0.5)` `.decay(0.2)` |
| `HiHat` | Hi-hat | `.tone(8000)` `.decay(0.1)` `.open(0.3)` |
| `Clap` | Hand clap | `.tone(1000)` `.spread(0.1)` `.decay(0.15)` |

**Effects:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Delay` | Delay effect | `.time(0.25)` `.feedback(0.5)` `.mix(0.3)` |
| `Echo` | Multi-tap echo | `delayTime` (ms) `decay` `taps` `mix` |
| `Reverb` | Reverb | `.roomSize(0.8)` `.damping(0.5)` `.mix(0.3)` |
| `Chorus` | Chorus | `.rate(1.5)` `.depth(0.5)` `.mix(0.5)` |
| `Flanger` | Flanger | `.rate(0.5)` `.depth(0.7)` `.feedback(0.6)` |
| `Phaser` | Phaser | `.rate(0.3)` `.depth(0.8)` `.stages(4)` |
| `Overdrive` | Distortion | `.drive(0.7)` `.tone(0.5)` |
| `Bitcrush` | Bit reduction | `.bits(8)` `.sampleRate(0.5)` |
| `AudioFilter` | Biquad filter | `.type(FilterType::Lowpass)` `.cutoff(1000)` `.resonance(2.0)` |
| `Compressor` | Dynamics | `.threshold(-20)` `.ratio(4.0)` `.attack(0.01)` `.release(0.1)` |
| `Limiter` | Brick-wall limiter | `ceiling` (dB) `release` (ms) `mix` |
| `Gate` | Noise gate | `threshold` `attack` `release` `mix` |

**Analysis:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `FFT` | Spectrum analyzer | `.size(1024)` |
| `BeatDetect` | Beat detection | `.sensitivity(0.8)` `.threshold(0.5)` |
| `Levels` | VU meter | `.smoothing(0.9)` |

**Sequencing:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Clock` | Tempo clock | `.bpm(120)` `.division(4)` |
| `Sequencer` | Step sequencer | `.steps(16)` `.pattern({1,0,1,0,...})` |
| `Euclidean` | Euclidean rhythms | `.steps(16)` `.hits(5)` `.rotation(0)` |

**Audio synthesis example:**
```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Sawtooth oscillator into formant filter
    auto& osc = chain.add<Oscillator>("osc")
        .waveform(Waveform::Saw)
        .frequency(110.0f);

    auto& formant = chain.add<Formant>("formant")
        .input(&osc)
        .vowel(Vowel::A)
        .resonance(8.0f);

    auto& reverb = chain.add<Reverb>("reverb")
        .input(&formant)
        .roomSize(0.7f)
        .mix(0.3f);

    chain.audioOutput("reverb");
}

void update(Context& ctx) {
    // Morph between vowels over time
    auto& formant = ctx.chain().get<Formant>("formant");
    float morph = (std::sin(ctx.time() * 0.5f) + 1.0f) * 0.5f;
    formant.morph(morph);
}
```

### 3D Rendering (vivid-render3d)

```cpp
#include <vivid/render3d/render3d.h>
using namespace vivid::render3d;
```

All 3D components are operators that appear in the chain visualizer.

**Primitive Operators:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Box` | Cube/box | `.size(w, h, d)` `.flatShading(true)` |
| `Sphere` | UV sphere | `.radius(1.0)` `.segments(32)` `.computeTangents()` |
| `Cylinder` | Cylinder | `.radius(0.5)` `.height(2.0)` `.segments(24)` |
| `Cone` | Cone | `.radius(0.5)` `.height(2.0)` `.segments(24)` |
| `Torus` | Donut | `.outerRadius(1.0)` `.innerRadius(0.3)` `.segments(32)` `.rings(16)` |
| `Plane` | Flat plane | `.size(10, 10)` `.subdivisions(4, 4)` |

**CSG Boolean Operations:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Boolean` | CSG operations | `.inputA(&mesh1)` `.inputB(&mesh2)` `.operation(BooleanOp::Subtract)` |

Operations: `BooleanOp::Union`, `BooleanOp::Subtract`, `BooleanOp::Intersect`

**Scene & Rendering:**

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `SceneComposer` | Compose meshes | `.add<Box>(name, transform, color)` `.add(&mesh, transform, color)` |
| `CameraOperator` | Camera | `.orbitCenter(x,y,z)` `.distance(10)` `.azimuth(0)` `.elevation(0.3)` `.fov(50)` `.orthographic()` `.orthoSize(10)` |
| `DirectionalLight` | Sun light | `.direction(x,y,z)` `.color(r,g,b)` `.intensity(1.5)` |
| `PointLight` | Omni light | `.position(x,y,z)` `.color(r,g,b)` `.intensity(2.0)` `.range(10.0)` |
| `SpotLight` | Spot light | `.position()` `.direction()` `.spotAngle(30)` `.spotBlend(0.3)` `.range(10)` |
| `Render3D` | Renderer | `.input(&scene)` `.cameraInput(&cam)` `.lightInput(&light)` `.addLight(&light2)` |
| `InstancedRender3D` | Instanced rendering | `.mesh(&mesh)` `.cameraInput(&cam)` `.lightInput(&light)` `.setInstances(vec)` |

**Multi-light support:** Up to 4 lights via `.lightInput()` (primary) and `.addLight()` (additional).

**Complete 3D example:**
```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create primitives
    auto& box = chain.add<Box>("box").size(1.2f);
    auto& sphere = chain.add<Sphere>("sphere").radius(0.8f).segments(24);

    // CSG: hollow cube
    auto& csg = chain.add<Boolean>("csg")
        .inputA(&box)
        .inputB(&sphere)
        .operation(BooleanOp::Subtract);

    // Scene composition
    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&csg, glm::mat4(1.0f), glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));

    // Camera
    auto& camera = chain.add<CameraOperator>("camera")
        .orbitCenter(0, 0, 0)
        .distance(5.0f)
        .fov(50.0f);

    // Lighting
    auto& sun = chain.add<DirectionalLight>("sun")
        .direction(1, 2, 1)
        .intensity(1.5f);

    // Render
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&sun)
        .shadingMode(ShadingMode::PBR)
        .metallic(0.1f)
        .roughness(0.5f)
        .clearColor(0.08f, 0.08f, 0.12f);

    chain.output("render");
}

void update(Context& ctx) {
    // Animate camera
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    camera.azimuth(ctx.time() * 0.3f);
}
```

**Shading modes:**
```cpp
ShadingMode::PBR      // Physically-based rendering (default)
ShadingMode::Flat     // Per-fragment flat shading
ShadingMode::Gouraud  // Per-vertex shading (PS1-style)
ShadingMode::Unlit    // No lighting, color only
```

**PBR Materials (textured):**
```cpp
auto& material = chain.add<TexturedMaterial>("mat")
    .baseColor("textures/albedo.png")
    .normal("textures/normal.png")
    .metallic("textures/metallic.png")
    .roughness("textures/roughness.png")
    .ao("textures/ao.png");

// Assign to scene entry
scene.entries().back().material = &material;
```

**IBL Environment (HDR lighting):**
```cpp
static IBLEnvironment iblEnv;

void setup(Context& ctx) {
    iblEnv.init(ctx);
    iblEnv.loadHDR(ctx, "assets/hdris/environment.hdr");

    // Render3D automatically uses IBL when available
    render.iblEnvironment(&iblEnv);
}
```

**Multi-light example:**
```cpp
// Directional (sun) + 2 point lights + 1 spot light
auto& sun = chain.add<DirectionalLight>("sun")
    .direction(1, 2, 1).intensity(0.5f);

auto& redLight = chain.add<PointLight>("redLight")
    .position(3, 1, 0).color(1.0f, 0.2f, 0.1f).intensity(3.0f).range(10.0f);

auto& blueLight = chain.add<PointLight>("blueLight")
    .position(-3, 1, 0).color(0.1f, 0.3f, 1.0f).intensity(3.0f).range(10.0f);

auto& spot = chain.add<SpotLight>("spot")
    .position(0, 4, 2).direction(0, -1, -0.3f)
    .spotAngle(25.0f).spotBlend(0.3f).intensity(5.0f).range(12.0f);

auto& render = chain.add<Render3D>("render")
    .input(&scene)
    .lightInput(&sun)       // Primary light
    .addLight(&redLight)    // Additional lights
    .addLight(&blueLight)
    .addLight(&spot);
```

**GPU Instancing (thousands of identical meshes):**
```cpp
// Instance3D struct: { transform, color, metallic, roughness }
auto& sphere = chain.add<Sphere>("sphere").radius(0.15f).segments(8);
auto& camera = chain.add<CameraOperator>("camera").distance(20.0f);
auto& sun = chain.add<DirectionalLight>("sun").direction(1, 1.5f, 0.5f);

auto& instanced = chain.add<InstancedRender3D>("asteroids")
    .mesh(&sphere)
    .cameraInput(&camera)
    .lightInput(&sun)
    .metallic(0.3f)
    .roughness(0.7f);

instanced.reserve(1000);  // Pre-allocate capacity

// In update():
instanced.clearInstances();
for (int i = 0; i < 1000; i++) {
    Instance3D inst;
    inst.transform = glm::translate(glm::mat4(1.0f), positions[i]);
    inst.color = glm::vec4(colors[i], 1.0f);
    inst.metallic = 0.2f;
    inst.roughness = 0.8f;
    instanced.addInstance(inst);
}
// Or use convenience methods:
instanced.addInstance(glm::vec3(x, y, z), scale, color);  // Position + uniform scale
instanced.addInstance(transformMatrix, color);            // Full matrix
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

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;

    auto& color = chain.add<HSV>("color");
    color.input(&noise);
    color.hueShift = 0.3f;

    auto& blur = chain.add<Blur>("blur");
    blur.input(&color);
    blur.radius = 3.0f;

    chain.output("blur");
}
```

### Feedback loop (trails/echoes)

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;

    auto& fb = chain.add<Feedback>("fb");
    fb.input(&noise);
    fb.decay = 0.95f;
    fb.zoom = 1.01f;

    chain.output("fb");
}
```

### Blend two sources

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");

    auto& img = chain.add<Image>("img");
    img.file = "assets/photo.jpg";

    auto& blend = chain.add<Composite>("blend");
    blend.inputA(&img);
    blend.inputB(&noise);
    blend.mode(BlendMode::Overlay);
    blend.opacity = 0.5f;

    chain.output("blend");
}
```

### Displace with noise

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& img = chain.add<Image>("img");
    img.file = "assets/photo.jpg";

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 10.0f;
    noise.speed = 0.3f;

    auto& warp = chain.add<Displace>("warp");
    warp.source(&img);
    warp.map(&noise);
    warp.strength = 0.05f;

    chain.output("warp");
}
```

### Retro/VHS look

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& vid = chain.add<VideoPlayer>("vid");
    vid.file("assets/video.mov");

    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input(&vid);
    chroma.amount = 0.005f;

    auto& lines = chain.add<Scanlines>("lines");
    lines.input(&chroma);
    lines.spacing = 3;
    lines.intensity = 0.2f;

    auto& quant = chain.add<Quantize>("quant");
    quant.input(&lines);
    quant.levels = 32;

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

### Slit-scan / temporal displacement

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& video = chain.add<VideoPlayer>("video");
    video.file("assets/video.mov");

    // Cache 64 frames for temporal effects
    auto& cache = chain.add<FrameCache>("cache");
    cache.input(&video);
    cache.frameCount = 64;

    // Gradient controls which frame is shown where
    auto& gradient = chain.add<Gradient>("map");
    gradient.mode(GradientMode::Linear);
    gradient.angle(90.0f);

    // TimeMachine samples different frames based on gradient
    auto& timeMachine = chain.add<TimeMachine>("slit");
    timeMachine.cache(&cache);
    timeMachine.displacementMap(&gradient);
    timeMachine.depth = 1.0f;

    chain.output("slit");
}
```

### Plexus network effect

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& plexus = chain.add<Plexus>("net");
    plexus.setNodeCount(300);
    plexus.setConnectionDistance(0.12f);
    plexus.setNodeSize(0.005f);
    plexus.setNodeColor(0.0f, 0.8f, 1.0f, 1.0f);
    plexus.setLineColor(0.0f, 0.6f, 0.9f, 0.4f);
    plexus.setTurbulence(0.15f);
    plexus.setClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("net");
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

// Display settings (can be set in setup() or update())
ctx.fullscreen(true);         // Start/switch to fullscreen
ctx.fullscreen(false);        // Switch to windowed
bool fs = ctx.fullscreen();   // Get current fullscreen state

ctx.vsync(true);              // Enable vsync (default)
ctx.vsync(false);             // Disable vsync
bool vs = ctx.vsync();        // Get current vsync state

// Window controls
ctx.borderless(true);         // Remove window decorations (title bar, borders)
ctx.borderless(false);        // Show window decorations
bool bl = ctx.borderless();   // Get current borderless state

ctx.alwaysOnTop(true);        // Keep window above other windows
ctx.alwaysOnTop(false);       // Normal window stacking
bool top = ctx.alwaysOnTop(); // Get current always-on-top state

ctx.cursorVisible(true);      // Show mouse cursor (default)
ctx.cursorVisible(false);     // Hide mouse cursor
bool cv = ctx.cursorVisible();// Get current cursor visibility

// Multi-monitor
int count = ctx.monitorCount();   // Get number of connected monitors
int idx = ctx.currentMonitor();   // Get index of monitor containing window (0-based)
ctx.moveToMonitor(1);             // Move window to monitor at index 1

// Window position and size
int x = ctx.windowX();            // Get window X position (screen coordinates)
int y = ctx.windowY();            // Get window Y position
ctx.setWindowPos(100, 200);       // Move window to position (100, 200)
ctx.setWindowSize(1280, 720);     // Resize window to 1280x720

// Resize detection
if (ctx.wasResized()) {
    // Window was resized this frame (user or programmatic)
    // Useful for updating layouts or resolution-dependent resources
}

// Mouse delta (movement since last frame)
glm::vec2 delta = ctx.mouseDelta();        // In pixels
glm::vec2 deltaN = ctx.mouseDeltaNorm();   // Normalized (-2 to 2 range)

// Key modifiers (convenient helpers)
if (ctx.shiftHeld()) { /* Shift is down */ }
if (ctx.ctrlHeld())  { /* Ctrl is down */ }
if (ctx.altHeld())   { /* Alt is down */ }
if (ctx.superHeld()) { /* Cmd/Win is down */ }

// Example: Shift+click detection
if (ctx.mouseButton(0).pressed && ctx.shiftHeld()) {
    // Shift+left click
}
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
