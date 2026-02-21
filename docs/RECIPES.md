# Vivid Recipes

Complete chain.cpp examples for common effects. Vivid treats audio and visuals as equal peers—many recipes combine both domains.

## Table of Contents

### Visual Effects (No Noise)
1. [Pulsing Shapes](#pulsing-shapes) - Audio-reactive geometry ⭐
2. [Geometric Flash](#geometric-flash) - Beat-synced shape effects ⭐
3. [Gradient Pulse](#gradient-pulse) - Color transitions, no procedural noise ⭐
4. [Particle Burst](#particle-burst) - Beat-synced particles ⭐

### Visual Effects (With Noise)
5. [VHS/Retro Look](#vhsretro-look)
6. [Feedback Tunnel](#feedback-tunnel)
7. [Video with Overlay Effects](#video-with-overlay-effects)
8. [Animated Background](#animated-background)
9. [Glitch Effect](#glitch-effect)
10. [Dream Sequence](#dream-sequence)
11. [Fire/Plasma](#fireplasma)
12. [Kaleidoscope](#kaleidoscope)
13. [Layer Compositing with Canvas](#layer-compositing-with-canvas)

### Audio-Visual
14. [Drum Machine with Visual Triggers](#drum-machine-with-visual-triggers)
15. [Audio-Reactive Particles](#audio-reactive-particles)
16. [Bidirectional Modulation](#bidirectional-modulation)

### Input Handling
17. [Event-Driven Patterns](#event-driven-patterns) - MIDI, OSC, keyboard, mouse

### Synthesis
18. [Per-Voice Modulation](#per-voice-modulation) - LFO, ADSR modulators attached to synths

---

## Pulsing Shapes

Audio-reactive geometric shapes without procedural noise. Great starting point for beat-reactive visuals.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio analysis
    auto& audioIn = chain.add<AudioIn>("audioIn");
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audioIn");

    // Star shape - size driven by bass
    auto& shape = chain.add<Shape>("shape");
    shape.type = ShapeType::Star;
    shape.sides = 5;
    shape.size.set(0.3f, 0.3f);
    shape.softness = 0.1f;
    shape.position.set(0.5f, 0.5f);

    // Colorize with audio-driven hue
    auto& hsv = chain.add<HSV>("color");
    hsv.input("shape");

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("color");
    bloom.threshold = 0.2f;
    bloom.radius = 20.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& bands = chain.get<BandSplit>("bands");
    auto& shape = chain.get<Shape>("shape");
    auto& hsv = chain.get<HSV>("color");
    auto& bloom = chain.get<Bloom>("bloom");

    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // Bass drives size (center-scaling, so it pulses outward)
    float s = 0.2f + bass * 0.5f;
    shape.size.set(s, s);

    // Mid drives rotation speed
    shape.rotation = static_cast<float>(ctx.time()) * (0.5f + mid);

    // High drives hue shift
    hsv.hueShift = high * 0.5f;

    // Audio-driven color (RGB from frequency bands)
    shape.color.set(bass, mid, high, 1.0f);

    // Bass drives bloom intensity
    bloom.intensity = 0.5f + bass * 1.5f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Geometric Flash

Beat-synced shape effects with Flash operator. Clean geometric look without noise.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Sequenced triggers
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.start();

    auto& seq = chain.add<Sequencer>("seq");
    seq.setTriggerSource("clock");
    seq.steps = 16;
    seq.setSteps({1, 4, 7, 9, 12, 15});  // Syncopated rhythm

    // Concentric circles
    auto& circle1 = chain.add<Shape>("circle1");
    circle1.type = ShapeType::Ellipse;
    circle1.size = 0.3f;
    circle1.softness = 0.4f;
    circle1.position.set(0.5f, 0.5f);
    circle1.color.set(0.2f, 0.5f, 1.0f, 1.0f);

    auto& circle2 = chain.add<Shape>("circle2");
    circle2.type = ShapeType::Ellipse;
    circle2.size = 0.5f;
    circle2.softness = 0.3f;
    circle2.position.set(0.5f, 0.5f);
    circle2.color.set(1.0f, 0.3f, 0.5f, 1.0f);

    // Composite the shapes
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("circle2");
    comp.inputB("circle1");
    comp.mode = BlendMode::Add;

    // Flash overlay
    auto& flash = chain.add<Flash>("flash");
    flash.input("comp");
    flash.decay = 0.88f;
    flash.color.set(1.0f, 1.0f, 1.0f);

    // Bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("flash");
    bloom.threshold = 0.3f;
    bloom.intensity = 1.2f;
    bloom.radius = 15.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& seq = chain.get<Sequencer>("seq");
    auto& circle1 = chain.get<Shape>("circle1");
    auto& circle2 = chain.get<Shape>("circle2");
    auto& flash = chain.get<Flash>("flash");

    // Trigger flash on beat
    if (seq.triggered()) {
        flash.trigger();
    }

    // Animate sizes with slow oscillation
    float t = static_cast<float>(ctx.time());
    circle1.size = 0.25f + 0.1f * std::sin(t * 2.0f);
    circle2.size = 0.45f + 0.1f * std::sin(t * 1.5f + 1.0f);

    // Counter-rotate
    circle1.rotation = t * 0.3f;
    circle2.rotation = -t * 0.2f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Gradient Pulse

Animated color gradients driven by audio. No procedural noise - pure color and geometry.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio analysis
    auto& audioIn = chain.add<AudioIn>("audioIn");
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audioIn");

    // Radial gradient - center glow
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.type = RampType::Radial;
    ramp.position.set(0.5f, 0.5f);

    // HSV adjustment for animated color
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("ramp");

    // Feedback for trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("hsv");
    feedback.decay = 0.92f;
    feedback.zoom = 1.005f;
    feedback.mix = 0.7f;

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("feedback");
    bloom.threshold = 0.2f;
    bloom.radius = 25.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& bands = chain.get<BandSplit>("bands");
    auto& ramp = chain.get<Ramp>("ramp");
    auto& hsv = chain.get<HSV>("hsv");
    auto& bloom = chain.get<Bloom>("bloom");

    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // Bass drives gradient radius (breathing effect)
    ramp.radius = 0.3f + bass * 0.5f;

    // Time-based hue animation + high frequency boost
    hsv.hueShift = static_cast<float>(ctx.time()) * 0.1f + high * 0.3f;
    hsv.saturation = 0.7f + mid * 0.3f;
    hsv.value = 0.5f + bass * 0.5f;

    // Audio-driven bloom
    bloom.intensity = 0.8f + bass * 1.5f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Particle Burst

Beat-synced particle system. Particles emit on triggers - no continuous noise texture.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Clock and triggers
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 128.0f;
    clock.start();

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.steps = 16;
    kickSeq.setSteps({0, 4, 8, 12});  // Four on floor

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.steps = 16;
    snareSeq.setSteps({0, 8});  // Backbeat

    // Particles - emit only on trigger
    auto& particles = chain.add<Particles>("particles");
    particles.emitRate = 0.0f;  // No continuous emission
    particles.emitterShape = EmitterShape::Point;
    particles.position.set(0.5f, 0.5f);
    particles.maxParticles = 500;
    particles.life = 1.5f;
    particles.size = 0.03f;
    particles.sizeEnd = 0.005f;
    particles.radialVelocity = 0.4f;
    particles.spread = 360.0f;
    particles.color.set(0.2f, 0.8f, 1.0f, 1.0f);
    particles.colorEnd.set(1.0f, 0.2f, 0.5f, 0.0f);

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("particles");
    bloom.threshold = 0.2f;
    bloom.intensity = 1.5f;
    bloom.radius = 20.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& snareSeq = chain.get<Sequencer>("snareSeq");
    auto& particles = chain.get<Particles>("particles");

    // Kick: big central burst
    if (kickSeq.triggered()) {
        particles.position.set(0.5f, 0.5f);
        particles.radialVelocity = 0.5f;
        particles.color.set(1.0f, 0.4f, 0.2f, 1.0f);  // Orange
        particles.burst(80);
    }

    // Snare: smaller burst with different color
    if (snareSeq.triggered()) {
        particles.position.set(0.5f, 0.5f);
        particles.radialVelocity = 0.3f;
        particles.color.set(0.2f, 0.8f, 1.0f, 1.0f);  // Cyan
        particles.burst(40);
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## VHS/Retro Look

Classic VHS tape aesthetic with scan lines, color bleeding, and noise.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source - video or image
    auto& src = chain.add<VideoPlayer>("src");
    src.file = "assets/video.mov";

    // Chromatic aberration (color bleeding)
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input("src");
    chroma.amount = 0.004f;
    chroma.angle = 0.0f;

    // Reduce color depth
    auto& quant = chain.add<Quantize>("quant");
    quant.input("chroma");
    quant.levels = 32;

    // Add scan lines
    auto& lines = chain.add<Scanlines>("lines");
    lines.input("quant");
    lines.spacing = 3;
    lines.intensity = 0.25f;
    lines.thickness = 0.4f;

    // Subtle noise overlay
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 100.0f;
    noise.speed = 10.0f;

    auto& noisy = chain.add<Composite>("noisy");
    noisy.inputA("lines");
    noisy.inputB("noise");
    noisy.mode = BlendMode::Add;
    noisy.opacity = 0.05f;

    // Slight blur for softness
    auto& soft = chain.add<Blur>("soft");
    soft.input("noisy");
    soft.radius = 0.5f;

    chain.output("soft");
}

void update(Context& ctx) {
    // No animation needed - noise animates via speed parameter
}

VIVID_CHAIN(setup, update)
```

---

## Beat-Synced Flash

Triggered flash overlay for beat-reactive visuals. Flash decays over time and supports additive, screen, or replace blend modes.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base visual
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.octaves = 3;

    // Flash overlay - white strobe
    auto& flash = chain.add<Flash>("flash");
    flash.input("noise");
    flash.decay = 0.85f;  // Fast decay (0.5-0.995)
    flash.color.set(1.0f, 1.0f, 1.0f);  // White
    flash.mode = 0;  // 0=Additive, 1=Screen, 2=Replace

    chain.output("flash");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& flash = chain.get<Flash>("flash");

    // Trigger on spacebar
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        flash.trigger();  // Full intensity
    }

    // Or trigger with custom intensity
    if (ctx.key(GLFW_KEY_1).pressed) {
        flash.trigger(0.5f);  // Half intensity
    }

    // Read current intensity for other effects
    float intensity = flash.intensity();

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

**Parameters:**
- `decay` (0.5-0.995): How fast flash fades. Lower = faster fade.
- `color.set(r, g, b)`: Flash color (0-1 range)
- `mode`: 0=Additive (adds light), 1=Screen (soft blend), 2=Replace (solid overlay)

**Methods:**
- `trigger()`: Start flash at full intensity
- `trigger(float intensity)`: Start at custom intensity (0-1)
- `intensity()`: Get current flash intensity (for other effects)

---

## Step Callbacks (Audio-Visual Sync)

Use `onStep()` callbacks to automatically sync audio and visual events. No manual polling in update() needed.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Clock and sequencer
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setSteps({0, 4, 8, 12});  // Kick on 1,5,9,13

    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");  // Audio-thread triggering

    // Visual
    auto& noise = chain.add<Noise>("noise");
    auto& flash = chain.add<Flash>("flash");
    flash.input("noise");

    auto& particles = chain.add<Particles>("particles");
    particles.emitRate = 0.0f;  // Only burst on trigger

    // === The key: onStep callback ===
    kickSeq.onStep([&](float velocity) {
        flash.trigger(velocity);         // Visual flash
        particles.burst(30);             // Visual particles
    });

    chain.output("flash");
}

void update(Context& ctx) {
    // Sequencer advances automatically via setTriggerSource("clock")
    // Drum triggers automatically via setTriggerSource("kickSeq")
    // Callbacks handle visual sync!
    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
```

**Callback signatures:**
- `Sequencer::onStep(std::function<void(float velocity)>)` - with velocity
- `Sequencer::onStep(std::function<void()>)` - simple, no velocity
- `Euclidean::onStep(std::function<void()>)` - no velocity

---

## Parameter Binding (Reactive Parameters)

Use `bind()` to create reactive connections between parameters and data sources. No manual update() code needed - bindings evaluate automatically when parameters are read.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio with analysis
    auto& synth = chain.add<PolySynth>("synth");
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("synth");

    // Visual
    auto& noise = chain.add<Noise>("noise");
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("noise");

    // === Parameter bindings ===

    // Bind with range mapping: source (0-1) → output (min-max)
    noise.scale.bind([&]() { return bands.bass(); }, 2.0f, 15.0f);
    bloom.intensity.bind([&]() { return bands.high(); }, 0.5f, 3.0f);

    // Bind direct (no range mapping)
    noise.speed.bindDirect([&]() {
        return 0.5f + ctx.mouseNorm().x;
    });

    chain.output("bloom");
}

void update(Context& ctx) {
    // No parameter updates needed!
    // Bindings evaluate automatically when parameters are read
    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
```

**Binding methods on Param<T>:**
- `bind(source, outMin, outMax)` - Map normalized source (0-1) to output range
- `bindDirect(source)` - Use source value directly (no mapping)
- `unbind()` - Clear binding
- `isBound()` - Check if bound

**Note:** Assignment (`param = value`) clears any existing binding.

---

## Feedback Tunnel

Infinite tunnel effect using frame feedback with zoom and rotation.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Seed shape in the center
    auto& shape = chain.add<Shape>("shape");
    shape.type = ShapeType::Star;
    shape.size.set(0.1f, 0.1f);
    shape.position.set(0.5f, 0.5f);
    shape.color.set(1.0f, 0.3f, 0.5f);

    // Feedback creates the tunnel
    auto& tunnel = chain.add<Feedback>("tunnel");
    tunnel.input("shape");
    tunnel.decay = 0.98f;
    tunnel.zoom = 1.02f;      // Slight zoom creates depth
    tunnel.rotate = 0.01f;    // Rotation adds spiral
    tunnel.mix = 0.95f;

    // Color shift for rainbow effect
    auto& rainbow = chain.add<HSV>("rainbow");
    rainbow.input("tunnel");
    rainbow.hueShift = 0.002f;  // Shifts each frame
    rainbow.saturation = 1.2f;

    // Bloom for glow
    auto& glow = chain.add<Bloom>("glow");
    glow.input("rainbow");
    glow.threshold = 0.3f;
    glow.intensity = 0.8f;
    glow.radius = 10.0f;

    chain.output("glow");
}

void update(Context& ctx) {
    // Feedback effect animates automatically
}

VIVID_CHAIN(setup, update)
```

---

## Video with Overlay Effects

Composite video with animated graphics overlay.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Background video
    auto& video = chain.add<VideoPlayer>("video");
    video.file = "assets/background.mov";

    // Animated noise pattern
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.speed = 0.3f;
    noise.type = NoiseType::Simplex;

    // Colorize the noise
    auto& colored = chain.add<HSV>("colored");
    colored.input("noise");
    colored.hueShift = 0.6f;
    colored.saturation = 0.8f;

    // Blend noise with video
    auto& blend = chain.add<Composite>("blend");
    blend.inputA("video");
    blend.inputB("colored");
    blend.mode = BlendMode::Overlay;
    blend.opacity = 0.3f;

    // Add logo/watermark
    auto& logo = chain.add<Image>("logo");
    logo.file = "assets/logo.png";

    auto& final_ = chain.add<Composite>("final");
    final_.inputA("blend");
    final_.inputB("logo");
    final_.mode = BlendMode::Over;
    final_.opacity = 0.8f;

    chain.output("final");
}

void update(Context& ctx) {
    // Video and noise animate automatically
}

VIVID_CHAIN(setup, update)
```

---

## Animated Background

Flowing abstract background for presentations or streams.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base noise layer
    auto& base = chain.add<Noise>("base");
    base.scale = 2.0f;
    base.speed = 0.1f;
    base.type = NoiseType::Simplex;
    base.octaves = 3;

    // Second noise for variation
    auto& detail = chain.add<Noise>("detail");
    detail.scale = 8.0f;
    detail.speed = 0.2f;
    detail.type = NoiseType::Perlin;

    // Combine noise layers
    auto& combined = chain.add<Composite>("combined");
    combined.inputA("base");
    combined.inputB("detail");
    combined.mode = BlendMode::Multiply;
    combined.opacity = 1.0f;

    // Animated color gradient
    auto& colors = chain.add<Ramp>("colors");
    colors.hueSpeed = 0.05f;
    colors.saturation = 0.7f;
    colors.type = RampType::Radial;

    // Apply colors to noise
    auto& colored = chain.add<Composite>("colored");
    colored.inputA("combined");
    colored.inputB("colors");
    colored.mode = BlendMode::Overlay;
    colored.opacity = 1.0f;

    // Smooth it out
    auto& smooth = chain.add<Blur>("smooth");
    smooth.input("colored");
    smooth.radius = 3.0f;

    chain.output("smooth");
}

void update(Context& ctx) {
    // All animations driven by speed parameters
}

VIVID_CHAIN(setup, update)
```

---

## Glitch Effect

Digital glitch/corruption aesthetic.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source
    auto& src = chain.add<Image>("src");
    src.file = "assets/photo.jpg";

    // Horizontal displacement noise
    auto& glitchNoise = chain.add<Noise>("glitchNoise");
    glitchNoise.scale = 1.0f;
    glitchNoise.speed = 5.0f;
    glitchNoise.type = NoiseType::Value;

    // Pixelate the noise for blocky glitches
    auto& blocks = chain.add<Pixelate>("blocks");
    blocks.input("glitchNoise");
    blocks.size = 20;

    // Displace the image
    auto& displaced = chain.add<Displace>("displaced");
    displaced.source("src");
    displaced.map("blocks");
    displaced.strength = 0.1f;

    // Heavy chromatic aberration
    auto& rgb = chain.add<ChromaticAberration>("rgb");
    rgb.input("displaced");
    rgb.amount = 0.015f;

    // Quantize for digital look
    auto& quant = chain.add<Quantize>("quant");
    quant.input("rgb");
    quant.levels = 16;

    chain.output("quant");
}

void update(Context& ctx) {
    // Glitch animates via noise speed
}

VIVID_CHAIN(setup, update)
```

---

## Dream Sequence

Soft, ethereal look for dreamlike visuals.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source
    auto& src = chain.add<VideoPlayer>("src");
    src.file = "assets/video.mov";

    // Soft glow
    auto& glow = chain.add<Bloom>("glow");
    glow.input("src");
    glow.threshold = 0.4f;
    glow.intensity = 1.5f;
    glow.radius = 20.0f;

    // Desaturate slightly
    auto& desat = chain.add<HSV>("desat");
    desat.input("glow");
    desat.saturation = 0.6f;
    desat.value = 1.1f;

    // Subtle noise for displacement
    auto& warpNoise = chain.add<Noise>("warpNoise");
    warpNoise.scale = 5.0f;
    warpNoise.speed = 0.2f;

    // Gentle warping
    auto& warp = chain.add<Displace>("warp");
    warp.source("desat");
    warp.map("warpNoise");
    warp.strength = 0.02f;

    // Heavy blur for dreamy softness
    auto& soft = chain.add<Blur>("soft");
    soft.input("warp");
    soft.radius = 5.0f;

    // Blend sharp and soft
    auto& dream = chain.add<Composite>("dream");
    dream.inputA("warp");
    dream.inputB("soft");
    dream.mode = BlendMode::Screen;
    dream.opacity = 0.5f;

    chain.output("dream");
}

void update(Context& ctx) {
    // Dream effect animates automatically
}

VIVID_CHAIN(setup, update)
```

---

## Fire/Plasma

Animated fire or plasma effect using layered noise.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base turbulent noise
    auto& turb = chain.add<Noise>("turb");
    turb.scale = 4.0f;
    turb.speed = 0.8f;
    turb.type = NoiseType::Simplex;
    turb.octaves = 6;

    // Vertical gradient for fire shape
    auto& grad = chain.add<Gradient>("grad");
    grad.mode = GradientMode::Linear;
    grad.angle = 90.0f;
    grad.colorA.set(1.0f, 1.0f, 1.0f);
    grad.colorB.set(0.0f, 0.0f, 0.0f);

    // Multiply to shape flames
    auto& shaped = chain.add<Composite>("shaped");
    shaped.inputA("turb");
    shaped.inputB("grad");
    shaped.mode = BlendMode::Multiply;

    // Fire colors
    auto& colored = chain.add<HSV>("colored");
    colored.input("shaped");
    colored.hueShift = -0.05f;  // Shift toward orange/red
    colored.saturation = 1.5f;
    colored.value = 1.2f;

    // Bloom for glow
    auto& glow = chain.add<Bloom>("glow");
    glow.input("colored");
    glow.threshold = 0.3f;
    glow.intensity = 1.0f;
    glow.radius = 8.0f;

    chain.output("glow");
}

void update(Context& ctx) {
    // Fire animates via noise speed
}

VIVID_CHAIN(setup, update)
```

---

## Kaleidoscope

Mirrored kaleidoscope effect with animated source.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Animated source pattern
    auto& pattern = chain.add<Noise>("pattern");
    pattern.scale = 3.0f;
    pattern.speed = 0.3f;
    pattern.type = NoiseType::Worley;

    // Colorize
    auto& colored = chain.add<HSV>("colored");
    colored.input("pattern");
    colored.hueShift = 0.3f;
    colored.saturation = 1.0f;

    // Kaleidoscope mirror
    auto& kaleido = chain.add<Mirror>("kaleido");
    kaleido.input("colored");
    kaleido.mode = MirrorMode::Kaleidoscope;
    kaleido.segments = 8;  // 8-fold symmetry

    // Transform for rotation
    auto& spin = chain.add<Transform>("spin");
    spin.input("kaleido");

    // Feedback for trails
    auto& trails = chain.add<Feedback>("trails");
    trails.input("spin");
    trails.decay = 0.95f;
    trails.mix = 0.3f;

    chain.output("trails");
}

void update(Context& ctx) {
    // Animate rotation
    auto& spin = ctx.chain().get<Transform>("spin");
    spin.rotation = static_cast<float>(ctx.time()) * 0.1f;
}

VIVID_CHAIN(setup, update)
```

---

## Layer Compositing with Canvas

Use Canvas as an FBO (Frame Buffer Object) to composite multiple operators into a single texture, then apply effects to the combined result.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create source operators
    auto& video = chain.add<VideoPlayer>("video");
    video.file = "assets/videos/background.mov";
    video.loop = true;

    auto& noise = chain.add<Noise>("overlay");
    noise.setResolution(400, 400);
    noise.scale = 3.0f;

    // Canvas acts as FBO - renders to its own texture
    auto& canvas = chain.add<Canvas>("layer");
    canvas.size(1920, 1080);

    // Apply effects to the composited result
    auto& blur = chain.add<Blur>("blur");
    blur.input("canvas");
    blur.radius = 5.0f;

    chain.output("blur");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.get<Canvas>("layer");
    auto& video = chain.get<VideoPlayer>("video");
    auto& noise = chain.get<Noise>("overlay");

    // Clear with solid background
    canvas.clear(0, 0, 0, 1);

    // Draw video as background (full canvas)
    canvas.drawImage(video, 0, 0, 1920, 1080);

    // Draw noise overlay with transform
    canvas.save();
    canvas.translate(760, 340);  // Position overlay
    canvas.drawImage(noise, 0, 0, 400, 400);
    canvas.restore();
}

VIVID_CHAIN(setup, update)
```

### Key Concepts

- **Independent resolution**: `canvas.size(w, h)` sets canvas resolution independent of sources
- **Frame clearing**: `canvas.clear(r, g, b, a)` starts each frame (use `a=0` for transparent overlays)
- **Draw operators**: `canvas.drawImage(op, x, y, w, h)` draws an operator's output at a position
- **Canvas as input**: Other operators can use `input(&canvas)` to process the composited result
- **Transform isolation**: Use `save()/restore()` to isolate position/rotation/scale changes

### Common Use Cases

1. **Picture-in-picture**: Draw video at smaller size over background
2. **UI overlay**: Draw Canvas with transparent background over main content
3. **Multi-layer effects**: Composite several sources before applying expensive effects
4. **Resolution independence**: Render at different resolution than sources

---

## Tips for Creating Your Own Effects

1. **Layer noise** - Multiple noise sources at different scales create organic patterns
2. **Use feedback sparingly** - High decay values (0.95+) create trails, lower values fade quickly
3. **Displacement adds movement** - Even subtle displacement (0.01-0.05) adds life
4. **Bloom sells it** - Bloom makes colors pop and creates atmosphere
5. **Resolution behavior** - Generators use window size at init if no explicit resolution set:
   - Use `setResolution(w, h)` for explicit dimensions (e.g., `noise.setResolution(512, 512)`)
   - Resolution locks after init - window resize won't affect generator textures
   - Only Output/Display scale to window
6. **Watch performance** - Blur and feedback are expensive; keep passes low
7. **State preservation** - Feedback and video playback state survives hot-reloads automatically
8. **Window configuration** - Use `VIVID_CHAIN_CONFIG` for custom window sizes:
   ```cpp
   VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
       .windowWidth = 1920,
       .windowHeight = 1080,
       .resizable = false
   }))
   ```

---

## Drum Machine with Visual Triggers

Sequenced drums with synchronized visual feedback. Audio triggers run on the audio thread
for sample-accurate timing; visual feedback is polled in update().

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio: Clock and sequencers
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.start();

    // Sequencers advance on clock (audio thread)
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.steps = 16;
    kickSeq.setSteps({0, 4, 8, 12});  // Four on floor

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.steps = 16;
    snareSeq.setSteps({0, 8});  // Backbeat

    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.steps = 16;
    hatSeq.hits = 7;

    // Drum synths trigger on sequencer output (audio thread)
    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");

    auto& snare = chain.add<Snare>("snare");
    snare.setTriggerSource("snareSeq");

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.setTriggerSource("hatSeq");

    // Audio mix
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "kick");
    mixer.setGain(0, 0.8f);
    mixer.setInput(1, "snare");
    mixer.setGain(1, 0.6f);
    mixer.setInput(2, "hihat");
    mixer.setGain(2, 0.4f);

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("mixer");
    chain.audioOutput("audioOut");

    // Visuals: Noise background with flash overlays
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;

    auto& kickFlash = chain.add<Flash>("kickFlash");
    kickFlash.input("noise");
    kickFlash.decay = 0.85f;
    kickFlash.color.set(1.0f, 1.0f, 1.0f);

    auto& snareFlash = chain.add<Flash>("snareFlash");
    snareFlash.input("kickFlash");
    snareFlash.decay = 0.9f;
    snareFlash.color.set(1.0f, 0.5f, 0.2f);

    chain.output("snareFlash");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Poll sequencers for visual feedback (main thread)
    if (chain.get<Sequencer>("kickSeq").triggered()) {
        chain.get<Flash>("kickFlash").trigger(1.0f);
    }
    if (chain.get<Sequencer>("snareSeq").triggered()) {
        chain.get<Flash>("snareFlash").trigger(1.0f);
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Audio-Reactive Particles

Particle system driven by audio analysis. Bass triggers bursts, frequency bands control behavior.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio input (microphone or line in)
    auto& audioIn = chain.add<AudioIn>("audioIn");

    // Analysis
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audioIn");

    auto& levels = chain.add<Levels>("levels");
    levels.input("audioIn");

    // Output the input (monitoring)
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("audioIn");
    chain.audioOutput("audioOut");

    // Visuals: Particles
    auto& particles = chain.add<Particles>("particles");
    particles.emitterShape = EmitterShape::Disc;
    particles.position.set(0.5f, 0.5f);
    particles.emitterSize = 0.1f;
    particles.maxParticles = 500;
    particles.life = 2.0f;
    particles.size = 0.02f;
    particles.sizeEnd = 0.005f;
    particles.color.set(0.2f, 0.8f, 1.0f, 1.0f);
    particles.colorEnd.set(1.0f, 0.3f, 0.5f, 0.0f);

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("particles");
    bloom.threshold = 0.3f;
    bloom.radius = 15.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& bands = chain.get<BandSplit>("bands");
    auto& levels = chain.get<Levels>("levels");
    auto& particles = chain.get<Particles>("particles");
    auto& bloom = chain.get<Bloom>("bloom");

    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();
    float rms = levels.rms();

    // Bass controls emit rate and burst
    particles.emitRate = bass * 200.0f;
    if (bass > 0.7f) {
        particles.burst(static_cast<int>(bass * 50));  // burst() is a method, not a property
    }

    // Mid controls velocity
    particles.radialVelocity = 0.2f + mid * 0.5f;

    // High controls spread
    particles.spread = 90.0f + high * 180.0f;

    // Overall level controls bloom
    bloom.intensity = 0.5f + rms * 2.0f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Bidirectional Modulation

Mouse controls both audio (pitch) and visuals (scale) simultaneously. Demonstrates audio-visual parity.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio: Synth controlled by mouse
    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform = Waveform::Saw;
    synth.attack = 0.01f;
    synth.release = 0.3f;
    synth.volume = 0.4f;

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("synth");
    reverb.roomSize = 0.7f;
    reverb.mix = 0.3f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("reverb");
    chain.audioOutput("audioOut");

    // Visuals: Noise controlled by same input
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.octaves = 4;

    auto& hsv = chain.add<HSV>("color");
    hsv.input("noise");
    hsv.saturation = 0.8f;

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("hsv");
    bloom.threshold = 0.4f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& synth = chain.get<PolySynth>("synth");
    auto& noise = chain.get<Noise>("noise");
    auto& hsv = chain.get<HSV>("color");
    auto& bloom = chain.get<Bloom>("bloom");

    // Normalized mouse position (0 to 1, Y-down)
    float mouseX = ctx.mouseNorm().x;
    float mouseY = ctx.mouseNorm().y;

    // X controls pitch (audio) and hue (visual)
    float frequency = 200.0f + mouseX * 600.0f;  // 200-800 Hz
    hsv.hueShift = mouseX * 0.5f;

    // Y controls filter (audio) and scale (visual)
    noise.scale = 2.0f + mouseY * 10.0f;
    bloom.intensity = 0.5f + mouseY * 2.0f;

    // Click to play note
    if (ctx.mouseButton(0).pressed) {
        synth.noteOn(frequency);
    }
    if (ctx.mouseButton(0).released) {
        synth.allNotesOff();
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

### Key Patterns

1. **Step callbacks** - `seq.onStep([&]() { ... })` fires audio + visual events simultaneously
2. **Audio analysis** - `BandSplit` gives bass/mid/high; `Levels` gives RMS/peak
3. **Parameter binding** - Same value drives both domains (mouse → pitch + scale)
4. **Capture chain pointer** - Use `auto* chainPtr = &chain` in callbacks to avoid dangling references

---

## Event-Driven Patterns

External events (MIDI, OSC, etc.) can trigger visual effects using the `Trigger` operator. Trigger provides an attack/decay envelope that converts discrete events into smooth values.

### MIDI-Triggered Effects

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::midi;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // MIDI input
    auto& midiIn = chain.add<MidiIn>("midi");
    midiIn.openPortByName("Arturia");  // Partial match

    // Triggers for different drum hits
    auto& kickTrigger = chain.add<Trigger>("kick");
    kickTrigger.decay = 0.85f;  // Fast decay

    auto& snareTrigger = chain.add<Trigger>("snare");
    snareTrigger.decay = 0.9f;  // Medium decay

    auto& hihatTrigger = chain.add<Trigger>("hihat");
    hihatTrigger.decay = 0.95f;  // Slow decay

    // Visuals
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("noise");

    auto& flash = chain.add<Flash>("flash");
    flash.input("bloom");

    chain.output("flash");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& midiIn = chain.get<MidiIn>("midi");
    auto& kickTrigger = chain.get<Trigger>("kick");
    auto& snareTrigger = chain.get<Trigger>("snare");
    auto& hihatTrigger = chain.get<Trigger>("hihat");

    // Fire triggers from MIDI events
    for (const auto& e : midiIn.events()) {
        if (e.type == MidiEventType::NoteOn) {
            float vel = e.velocity / 127.0f;
            if (e.note == 36) kickTrigger.fire(vel);
            if (e.note == 38) snareTrigger.fire(vel);
            if (e.note == 42) hihatTrigger.fire(vel);
        }
    }

    // Use trigger values to drive effects
    auto& noise = chain.get<Noise>("noise");
    noise.scale = 4.0f + kickTrigger.value() * 8.0f;

    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.3f + snareTrigger.value() * 1.5f;

    auto& flash = chain.get<Flash>("flash");
    if (hihatTrigger.active()) {
        flash.trigger(hihatTrigger.value() * 0.3f);
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

### OSC Parameter Mapping

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // OSC receiver (TouchOSC, Lemur, etc.)
    auto& osc = chain.add<OscIn>("osc");
    osc.port(8000);

    // Visual
    auto& noise = chain.add<Noise>("noise");
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("noise");

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& osc = chain.get<OscIn>("osc");
    auto& noise = chain.get<Noise>("noise");
    auto& bloom = chain.get<Bloom>("bloom");

    // Map OSC faders to parameters
    noise.scale = osc.getFloat("/fader/1", 4.0f) * 10.0f;
    noise.speed = osc.getFloat("/fader/2", 0.5f);
    bloom.intensity = osc.getFloat("/fader/3", 0.5f) * 2.0f;

    // Check for button triggers
    if (osc.hasMessage("/button/1")) {
        // Reset to defaults
        noise.scale = 4.0f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

### Keyboard and Mouse Input

For keyboard and mouse, use Context methods directly - they're simpler than operators:

```cpp
void update(Context& ctx) {
    // Keyboard: Check key state
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        flash.trigger();
    }
    if (ctx.key(GLFW_KEY_UP).held) {
        value += ctx.dt();  // Continuous while held
    }

    // Mouse: Position and buttons
    glm::vec2 mouse = ctx.mouseNorm();  // 0-1 normalized
    if (ctx.mouseButton(0).pressed) {
        particles.burst(100);
    }

    // Modifiers
    float speed = ctx.shiftHeld() ? 10.0f : 1.0f;
}
```

**When to use each approach:**
- **MIDI/OSC** → Use event operators + Trigger for decay envelopes
- **Keyboard/Mouse** → Use `ctx.key()`, `ctx.mouse()` directly
- **Window events** → Use `WindowEvents` operator for resize/focus

---

## Per-Voice Modulation

Attach modulators (LFO, ADSR) to synths for per-voice control. Inspired by Bitwig's unified modulation system.

### Key Concepts

**Attached vs Standalone Modulators:**
- **Standalone**: `chain.add<LFO>("lfo")` - Runs globally, access via `lfo.value()`
- **Attached**: `synth.addModulator<LFO>("lfo")` - Runs per-voice inside synth

**perVoice Toggle:**
- `perVoice = true`: Each voice has independent modulator state (e.g., per-note envelope)
- `perVoice = false`: All voices share one state (e.g., global filter sweep)

**Modulation Routing:**
```cpp
synth.modulate(modulator, "paramName", depth, bipolar);
// depth: How much modulation (0.0 - 1.0 = percentage of param range)
// bipolar: true for -1 to +1 range, false for 0 to 1
```

### Per-Voice Filter Envelope

Classic subtractive synth sound: each note gets its own filter sweep.

```cpp
#include <vivid/vivid.h>
#include <vivid/audio/modulators/lfo.h>
#include <vivid/audio/modulators/adsr.h>

using namespace vivid;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Analog);
    synth.filterCutoff = 500.0f;    // Low base cutoff
    synth.filterResonance = 0.5f;

    // Attach per-voice filter envelope
    auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
    filterEnv.attack = 0.01f;
    filterEnv.decay = 0.4f;
    filterEnv.sustain = 0.1f;
    filterEnv.release = 0.3f;
    filterEnv.perVoice = true;  // Each note gets own envelope

    // Route to filter cutoff (unipolar: envelope opens filter)
    synth.modulate(filterEnv, "filterCutoff", 0.9f, false);

    chain.audioOutput("synth");
}
```

### Per-Voice LFO (Vibrato/Wobble)

Each voice gets independent LFO phase - creates rich, organic sound.

```cpp
auto& synth = chain.add<WavetableSynth>("synth");
synth.loadBuiltin(BuiltinTable::Digital);

// Per-voice wavetable position LFO
auto& posLfo = synth.addModulator<LFO>("posLfo");
posLfo.rate = 0.5f;
posLfo.waveform = LFOWaveform::Triangle;
posLfo.perVoice = true;
posLfo.retrigger = true;  // Reset phase on noteOn

synth.modulate(posLfo, "position", 0.4f);  // Sweep 40% of wavetable
```

### Global vs Per-Voice Comparison

```cpp
// GLOBAL LFO: All voices move together (pulsing/breathing)
auto& globalLfo = synth.addModulator<LFO>("globalLfo");
globalLfo.rate = 0.1f;
globalLfo.perVoice = false;  // Shared by all voices
synth.modulate(globalLfo, "filterCutoff", 0.3f);

// PER-VOICE LFO: Each voice has own phase (organic/alive)
auto& voiceLfo = synth.addModulator<LFO>("voiceLfo");
voiceLfo.rate = 2.0f;
voiceLfo.perVoice = true;    // Independent per voice
synth.modulate(voiceLfo, "volume", 0.2f);  // Tremolo
```

### Multiple Modulators on Same Parameter

Modulators targeting the same parameter sum together.

```cpp
auto& synth = chain.add<WavetableSynth>("synth");
synth.filterCutoff = 1000.0f;

// Filter envelope (fast attack, opens filter on note start)
auto& env = synth.addModulator<ADSRMod>("env");
env.attack = 0.01f;
env.decay = 0.3f;
env.sustain = 0.2f;
synth.modulate(env, "filterCutoff", 0.7f, false);

// Slow LFO (adds subtle movement)
auto& lfo = synth.addModulator<LFO>("lfo");
lfo.rate = 0.2f;
lfo.perVoice = false;
synth.modulate(lfo, "filterCutoff", 0.15f);

// Result: Envelope sweeps filter, LFO adds gentle wobble on top
```

### Complete Polyphonic Patch

```cpp
#include <vivid/vivid.h>
#include <vivid/audio/modulators/lfo.h>
#include <vivid/audio/modulators/adsr.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Synth with modular modulation
    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Analog);
    synth.maxVoices = 6;
    synth.unisonVoices = 2;
    synth.unisonSpread = 10.0f;
    synth.filterCutoff = 800.0f;
    synth.filterResonance = 0.4f;

    // Per-voice filter envelope
    auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
    filterEnv.attack = 0.005f;
    filterEnv.decay = 0.4f;
    filterEnv.sustain = 0.1f;
    filterEnv.perVoice = true;
    synth.modulate(filterEnv, "filterCutoff", 0.8f, false);

    // Per-voice position LFO
    auto& posLfo = synth.addModulator<LFO>("posLfo");
    posLfo.rate = 0.3f;
    posLfo.waveform = LFOWaveform::Triangle;
    posLfo.perVoice = true;
    synth.modulate(posLfo, "position", 0.4f);

    // Global filter LFO (breathing)
    auto& filterLfo = synth.addModulator<LFO>("filterLfo");
    filterLfo.rate = 0.15f;
    filterLfo.perVoice = false;
    synth.modulate(filterLfo, "filterCutoff", 0.15f);

    // Effects
    auto& delay = chain.add<Delay>("delay");
    delay.setInput(&synth);
    delay.time = 0.375f;
    delay.feedback = 0.4f;
    delay.mix = 0.25f;

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.setInput(&delay);
    reverb.roomSize = 0.7f;
    reverb.wet = 0.3f;

    chain.audioOutput("reverb");
}

void update(Context& ctx) {
    auto& synth = ctx.chain().get<WavetableSynth>("synth");

    // Play notes (example: simple arpeggio)
    static float lastNote = -1.0f;
    float notes[] = {261.63f, 329.63f, 392.00f, 493.88f};
    int idx = static_cast<int>(ctx.time() * 4.0f) % 4;

    if (notes[idx] != lastNote) {
        if (lastNote > 0) synth.noteOff(lastNote);
        synth.noteOn(notes[idx], 0.8f);
        lastNote = notes[idx];
    }

    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

### Available Modulators

| Modulator | Output Range | Description |
|-----------|--------------|-------------|
| `LFO` | -1 to +1 | Periodic waveforms (sine, tri, square, saw) |
| `ADSRMod` | 0 to 1 | Attack-Decay-Sustain-Release envelope |

### LFO Waveforms

```cpp
lfo.waveform = LFOWaveform::Sine;       // Smooth, classic
lfo.waveform = LFOWaveform::Triangle;   // Linear ramps
lfo.waveform = LFOWaveform::Square;     // On/off gating
lfo.waveform = LFOWaveform::Saw;        // Rising ramp
lfo.waveform = LFOWaveform::SawDown;    // Falling ramp
lfo.waveform = LFOWaveform::SampleHold; // Random steps
```

### Tempo Sync

LFOs can sync to tempo:

```cpp
auto& lfo = synth.addModulator<LFO>("gateLfo");
lfo.sync = true;
lfo.division = static_cast<int>(ClockDiv::Sixteenth);
lfo.bpm = 120.0f;  // Or use setClockSource("clockOperator")
lfo.waveform = LFOWaveform::Square;
synth.modulate(lfo, "volume", 1.0f);  // Tempo-synced gate
```
