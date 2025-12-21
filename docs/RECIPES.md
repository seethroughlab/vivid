# Vivid Recipes

Complete chain.cpp examples for common effects. Vivid treats audio and visuals as equal peers—many recipes combine both domains.

## Table of Contents

### Visual Effects
1. [VHS/Retro Look](#vhsretro-look)
2. [Feedback Tunnel](#feedback-tunnel)
3. [Video with Overlay Effects](#video-with-overlay-effects)
4. [Animated Background](#animated-background)
5. [Glitch Effect](#glitch-effect)
6. [Dream Sequence](#dream-sequence)
7. [Fire/Plasma](#fireplasma)
8. [Kaleidoscope](#kaleidoscope)
9. [Layer Compositing with Canvas](#layer-compositing-with-canvas)

### Audio-Visual
10. [Drum Machine with Visual Triggers](#drum-machine-with-visual-triggers)
11. [Audio-Reactive Particles](#audio-reactive-particles)
12. [Bidirectional Modulation](#bidirectional-modulation)

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
    chain.add<VideoPlayer>("src").file("assets/video.mov");

    // Chromatic aberration (color bleeding)
    chain.add<ChromaticAberration>("chroma")
        .input("src")
        .amount(0.004f)
        .angle(0.0f);

    // Reduce color depth
    chain.add<Quantize>("quant")
        .input("chroma")
        .levels(32);

    // Add scan lines
    chain.add<Scanlines>("lines")
        .input("quant")
        .spacing(3)
        .intensity(0.25f)
        .thickness(0.4f);

    // Subtle noise overlay
    chain.add<Noise>("noise")
        .scale(100.0f)
        .speed(10.0f);

    chain.add<Composite>("noisy")
        .inputA("lines")
        .inputB("noise")
        .mode(BlendMode::Add)
        .opacity(0.05f);

    // Slight blur for softness
    chain.add<Blur>("soft")
        .input("noisy")
        .radius(0.5f);

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
    flash.input(&noise);
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

## Trigger Callbacks (Audio-Visual Sync)

Use `onTrigger()` callbacks to automatically sync audio and visual events. No manual polling in update() needed.

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
    kickSeq.setPattern(0b0001000100010001);  // Kick on 1,5,9,13

    auto& kick = chain.add<Kick>("kick");

    // Visual
    auto& noise = chain.add<Noise>("noise");
    auto& flash = chain.add<Flash>("flash");
    flash.input(&noise);

    auto& particles = chain.add<Particles>("particles");
    particles.emitRate(0.0f);  // Only burst on trigger

    // === The key: onTrigger callback ===
    kickSeq.onTrigger([&](float velocity) {
        kick.trigger();                  // Audio
        flash.trigger(velocity);         // Visual flash
        particles.burst(30);             // Visual particles
    });

    chain.output("flash");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Just advance clock - callbacks handle the rest!
    if (chain.get<Clock>("clock").triggered()) {
        chain.get<Sequencer>("kickSeq").advance();
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

**Callback signatures:**
- `Sequencer::onTrigger(std::function<void(float velocity)>)` - with velocity
- `Sequencer::onTrigger(std::function<void()>)` - simple, no velocity
- `Euclidean::onTrigger(std::function<void()>)` - no velocity

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
    bloom.input(&noise);

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
    chain.add<Shape>("shape")
        .type(ShapeType::Star)
        .size(0.1f)
        .position(0.5f, 0.5f)
        .color(1.0f, 0.3f, 0.5f);

    // Feedback creates the tunnel
    chain.add<Feedback>("tunnel")
        .input("shape")
        .decay(0.98f)
        .zoom(1.02f)      // Slight zoom creates depth
        .rotate(0.01f)    // Rotation adds spiral
        .mix(0.95f);

    // Color shift for rainbow effect
    chain.add<HSV>("rainbow")
        .input("tunnel")
        .hueShift(0.002f)  // Shifts each frame
        .saturation(1.2f);

    // Bloom for glow
    chain.add<Bloom>("glow")
        .input("rainbow")
        .threshold(0.3f)
        .intensity(0.8f)
        .radius(10.0f);

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
    chain.add<VideoPlayer>("video").file("assets/background.mov");

    // Animated noise pattern
    chain.add<Noise>("noise")
        .scale(3.0f)
        .speed(0.3f)
        .type(NoiseType::Simplex);

    // Colorize the noise
    chain.add<HSV>("colored")
        .input("noise")
        .hueShift(0.6f)
        .saturation(0.8f);

    // Blend noise with video
    chain.add<Composite>("blend")
        .inputA("video")
        .inputB("colored")
        .mode(BlendMode::Overlay)
        .opacity(0.3f);

    // Add logo/watermark
    auto& logo = chain.add<Image>("logo");
    logo.file = "assets/logo.png";

    chain.add<Composite>("final")
        .inputA("blend")
        .inputB("logo")
        .mode(BlendMode::Over)
        .opacity(0.8f);

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
    chain.add<Noise>("base")
        .scale(2.0f)
        .speed(0.1f)
        .type(NoiseType::Simplex)
        .octaves(3);

    // Second noise for variation
    chain.add<Noise>("detail")
        .scale(8.0f)
        .speed(0.2f)
        .type(NoiseType::Perlin);

    // Combine noise layers
    chain.add<Composite>("combined")
        .inputA("base")
        .inputB("detail")
        .mode(BlendMode::Multiply)
        .opacity(1.0f);

    // Animated color gradient
    chain.add<Ramp>("colors")
        .hueSpeed(0.05f)
        .saturation(0.7f)
        .type(RampType::Radial);

    // Apply colors to noise
    chain.add<Composite>("colored")
        .inputA("combined")
        .inputB("colors")
        .mode(BlendMode::Overlay)
        .opacity(1.0f);

    // Smooth it out
    chain.add<Blur>("smooth")
        .input("colored")
        .radius(3.0f);

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
    chain.add<Noise>("glitchNoise")
        .scale(1.0f)
        .speed(5.0f)
        .type(NoiseType::Value);

    // Pixelate the noise for blocky glitches
    auto& blocks = chain.add<Pixelate>("blocks")
        .input("glitchNoise")
        .size(20);

    // Displace the image
    chain.add<Displace>("displaced")
        .source(&src)
        .map(&blocks)
        .strength(0.1f);

    // Heavy chromatic aberration
    chain.add<ChromaticAberration>("rgb")
        .input("displaced")
        .amount(0.015f);

    // Quantize for digital look
    chain.add<Quantize>("quant")
        .input("rgb")
        .levels(16);

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
    chain.add<VideoPlayer>("src").file("assets/video.mov");

    // Soft glow
    chain.add<Bloom>("glow")
        .input("src")
        .threshold(0.4f)
        .intensity(1.5f)
        .radius(20.0f);

    // Desaturate slightly
    auto& desat = chain.add<HSV>("desat")
        .input("glow")
        .saturation(0.6f)
        .value(1.1f);

    // Subtle noise for displacement
    auto& warpNoise = chain.add<Noise>("warpNoise")
        .scale(5.0f)
        .speed(0.2f);

    // Gentle warping
    chain.add<Displace>("warp")
        .source(&desat)
        .map(&warpNoise)
        .strength(0.02f);

    // Heavy blur for dreamy softness
    chain.add<Blur>("soft")
        .input("warp")
        .radius(5.0f);

    // Blend sharp and soft
    chain.add<Composite>("dream")
        .inputA("warp")
        .inputB("soft")
        .mode(BlendMode::Screen)
        .opacity(0.5f);

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
    chain.add<Noise>("turb")
        .scale(4.0f)
        .speed(0.8f)
        .type(NoiseType::Simplex)
        .octaves(6);

    // Vertical gradient for fire shape
    chain.add<Gradient>("grad")
        .mode(GradientMode::Linear)
        .angle(90.0f)
        .colorA(1.0f, 1.0f, 1.0f)
        .colorB(0.0f, 0.0f, 0.0f);

    // Multiply to shape flames
    chain.add<Composite>("shaped")
        .inputA("turb")
        .inputB("grad")
        .mode(BlendMode::Multiply);

    // Fire colors
    chain.add<HSV>("colored")
        .input("shaped")
        .hueShift(-0.05f)  // Shift toward orange/red
        .saturation(1.5f)
        .value(1.2f);

    // Bloom for glow
    chain.add<Bloom>("glow")
        .input("colored")
        .threshold(0.3f)
        .intensity(1.0f)
        .radius(8.0f);

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
    chain.add<Noise>("pattern")
        .scale(3.0f)
        .speed(0.3f)
        .type(NoiseType::Worley);

    // Colorize
    chain.add<HSV>("colored")
        .input("pattern")
        .hueShift(0.3f)
        .saturation(1.0f);

    // Kaleidoscope mirror
    chain.add<Mirror>("kaleido")
        .input("colored")
        .mode(MirrorMode::Kaleidoscope)
        .segments(8);  // 8-fold symmetry

    // Transform for rotation
    chain.add<Transform>("spin")
        .input("kaleido");

    // Feedback for trails
    chain.add<Feedback>("trails")
        .input("spin")
        .decay(0.95f)
        .mix(0.3f);

    chain.output("trails");
}

void update(Context& ctx) {
    // Animate rotation
    auto& spin = ctx.chain().get<Transform>("spin");
    spin.rotate(static_cast<float>(ctx.time()) * 0.1f);
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
    video.loop(true);

    auto& noise = chain.add<Noise>("overlay");
    noise.setResolution(400, 400);
    noise.scale = 3.0f;

    // Canvas acts as FBO - renders to its own texture
    auto& canvas = chain.add<Canvas>("layer");
    canvas.size(1920, 1080);

    // Apply effects to the composited result
    auto& blur = chain.add<Blur>("blur");
    blur.input(&canvas);
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
5. **Match your chain resolution** to output - Higher res = sharper but slower
6. **Watch performance** - Blur and feedback are expensive; keep passes low
7. **State preservation** - Feedback and video playback state survives hot-reloads automatically

---

## Drum Machine with Visual Triggers

Sequenced drums with synchronized visual feedback. Audio triggers fire both sound and visual events.

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

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.steps = 16;
    kickSeq.setPattern(0b0001000100010001);  // Four on floor

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.steps = 16;
    snareSeq.setPattern(0b0000000100000001);  // Backbeat

    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.steps = 16;
    hatSeq.hits = 7;

    // Drum synths
    auto& kick = chain.add<Kick>("kick");
    auto& snare = chain.add<Snare>("snare");
    auto& hihat = chain.add<HiHat>("hihat");

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
    kickFlash.input(&noise);
    kickFlash.decay = 0.85f;
    kickFlash.color.set(1.0f, 1.0f, 1.0f);

    auto& snareFlash = chain.add<Flash>("snareFlash");
    snareFlash.input(&kickFlash);
    snareFlash.decay = 0.9f;
    snareFlash.color.set(1.0f, 0.5f, 0.2f);

    chain.output("snareFlash");

    // Connect triggers: audio + visual fire together
    auto* chainPtr = &chain;

    kickSeq.onTrigger([chainPtr](float vel) {
        chainPtr->get<Kick>("kick").trigger();
        chainPtr->get<Flash>("kickFlash").trigger(vel);
    });

    snareSeq.onTrigger([chainPtr](float vel) {
        chainPtr->get<Snare>("snare").trigger();
        chainPtr->get<Flash>("snareFlash").trigger(vel);
    });

    hatSeq.onTrigger([chainPtr]() {
        chainPtr->get<HiHat>("hihat").trigger();
    });
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& clock = chain.get<Clock>("clock");

    if (clock.triggered()) {
        chain.get<Sequencer>("kickSeq").advance();
        chain.get<Sequencer>("snareSeq").advance();
        chain.get<Euclidean>("hatSeq").advance();
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
    particles.emitter(EmitterShape::Disc);
    particles.position(0.5f, 0.5f);
    particles.emitterSize(0.1f);
    particles.maxParticles(500);
    particles.life(2.0f);
    particles.size(0.02f, 0.005f);
    particles.color(0.2f, 0.8f, 1.0f, 1.0f);
    particles.colorEnd(1.0f, 0.3f, 0.5f, 0.0f);

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&particles);
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
    particles.emitRate(bass * 200.0f);
    if (bass > 0.7f) {
        particles.burst(static_cast<int>(bass * 50));
    }

    // Mid controls velocity
    particles.radialVelocity(0.2f + mid * 0.5f);

    // High controls spread
    particles.spread(90.0f + high * 180.0f);

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
    synth.waveform(Waveform::Saw);
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
    hsv.input(&noise);
    hsv.saturation = 0.8f;

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&hsv);
    bloom.threshold = 0.4f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& synth = chain.get<PolySynth>("synth");
    auto& noise = chain.get<Noise>("noise");
    auto& hsv = chain.get<HSV>("color");
    auto& bloom = chain.get<Bloom>("bloom");

    // Normalized mouse position (0 to 1)
    float mouseX = (ctx.mouseNorm().x + 1.0f) * 0.5f;
    float mouseY = (ctx.mouseNorm().y + 1.0f) * 0.5f;

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

1. **Trigger callbacks** - `seq.onTrigger([&]() { ... })` fires audio + visual events simultaneously
2. **Audio analysis** - `BandSplit` gives bass/mid/high; `Levels` gives RMS/peak
3. **Parameter binding** - Same value drives both domains (mouse → pitch + scale)
4. **Capture chain pointer** - Use `auto* chainPtr = &chain` in callbacks to avoid dangling references
