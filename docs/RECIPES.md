# Vivid Recipes

Complete chain.cpp examples for common visual effects.

## Table of Contents

1. [VHS/Retro Look](#vhsretro-look)
2. [Feedback Tunnel](#feedback-tunnel)
3. [Video with Overlay Effects](#video-with-overlay-effects)
4. [Animated Background](#animated-background)
5. [Glitch Effect](#glitch-effect)
6. [Dream Sequence](#dream-sequence)
7. [Fire/Plasma](#fireplasma)
8. [Kaleidoscope](#kaleidoscope)

---

## VHS/Retro Look

Classic VHS tape aesthetic with scan lines, color bleeding, and noise.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Source - video or image
    chain->add<Video>("src").path("assets/video.mov");

    // Chromatic aberration (color bleeding)
    chain->add<ChromaticAberration>("chroma")
        .input("src")
        .amount(0.004f)
        .angle(0.0f);

    // Reduce color depth
    chain->add<Quantize>("quant")
        .input("chroma")
        .levels(32);

    // Add scan lines
    chain->add<Scanlines>("lines")
        .input("quant")
        .spacing(3)
        .intensity(0.25f)
        .thickness(0.4f);

    // Subtle noise overlay
    chain->add<Noise>("noise")
        .scale(100.0f)
        .speed(10.0f);

    chain->add<Composite>("noisy")
        .inputA("lines")
        .inputB("noise")
        .mode(BlendMode::Add)
        .opacity(0.05f);

    // Slight blur for softness
    chain->add<Blur>("soft")
        .input("noisy")
        .radius(0.5f);

    chain->add<Output>("out").input("soft");
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

---

## Feedback Tunnel

Infinite tunnel effect using frame feedback with zoom and rotation.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Seed shape in the center
    chain->add<Shape>("shape")
        .type(ShapeType::Star)
        .size(0.1f)
        .position(0.5f, 0.5f)
        .color(1.0f, 0.3f, 0.5f);

    // Feedback creates the tunnel
    chain->add<Feedback>("tunnel")
        .input("shape")
        .decay(0.98f)
        .zoom(1.02f)      // Slight zoom creates depth
        .rotate(0.01f)    // Rotation adds spiral
        .mix(0.95f);

    // Color shift for rainbow effect
    chain->add<HSV>("rainbow")
        .input("tunnel")
        .hueShift(0.002f)  // Shifts each frame
        .saturation(1.2f);

    // Bloom for glow
    chain->add<Bloom>("glow")
        .input("rainbow")
        .threshold(0.3f)
        .intensity(0.8f)
        .radius(10.0f);

    chain->add<Output>("out").input("glow");
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

---

## Video with Overlay Effects

Composite video with animated graphics overlay.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1920, 1080);

    // Background video
    chain->add<Video>("video").path("assets/background.mov");

    // Animated noise pattern
    chain->add<Noise>("noise")
        .scale(3.0f)
        .speed(0.3f)
        .type(NoiseType::Simplex);

    // Colorize the noise
    chain->add<HSV>("colored")
        .input("noise")
        .hueShift(0.6f)
        .saturation(0.8f);

    // Blend noise with video
    chain->add<Composite>("blend")
        .inputA("video")
        .inputB("colored")
        .mode(BlendMode::Overlay)
        .opacity(0.3f);

    // Add logo/watermark
    chain->add<Image>("logo").path("assets/logo.png");

    chain->add<Composite>("final")
        .inputA("blend")
        .inputB("logo")
        .mode(BlendMode::Over)
        .opacity(0.8f);

    chain->add<Output>("out").input("final");
}

void update(Context& ctx) {
    chain->process();
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

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1920, 1080);

    // Base noise layer
    chain->add<Noise>("base")
        .scale(2.0f)
        .speed(0.1f)
        .type(NoiseType::Simplex)
        .octaves(3);

    // Second noise for variation
    chain->add<Noise>("detail")
        .scale(8.0f)
        .speed(0.2f)
        .type(NoiseType::Perlin);

    // Combine noise layers
    chain->add<Composite>("combined")
        .inputA("base")
        .inputB("detail")
        .mode(BlendMode::Multiply)
        .opacity(1.0f);

    // Animated color gradient
    chain->add<Ramp>("colors")
        .hueSpeed(0.05f)
        .saturation(0.7f)
        .type(RampType::Radial);

    // Apply colors to noise
    chain->add<Composite>("colored")
        .inputA("combined")
        .inputB("colors")
        .mode(BlendMode::Overlay)
        .opacity(1.0f);

    // Smooth it out
    chain->add<Blur>("smooth")
        .input("colored")
        .radius(3.0f);

    chain->add<Output>("out").input("smooth");
}

void update(Context& ctx) {
    chain->process();
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

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Source
    chain->add<Image>("src").path("assets/photo.jpg");

    // Horizontal displacement noise
    chain->add<Noise>("glitchNoise")
        .scale(1.0f)
        .speed(5.0f)
        .type(NoiseType::Value);

    // Pixelate the noise for blocky glitches
    chain->add<Pixelate>("blocks")
        .input("glitchNoise")
        .size(20);

    // Displace the image
    chain->add<Displace>("displaced")
        .source(&chain->get<Image>("src"))
        .map(&chain->get<Pixelate>("blocks"))
        .strength(0.1f);

    // Heavy chromatic aberration
    chain->add<ChromaticAberration>("rgb")
        .input("displaced")
        .amount(0.015f);

    // Quantize for digital look
    chain->add<Quantize>("quant")
        .input("rgb")
        .levels(16);

    chain->add<Output>("out").input("quant");
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

---

## Dream Sequence

Soft, ethereal look for dreamlike visuals.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Source
    chain->add<Video>("src").path("assets/video.mov");

    // Soft glow
    chain->add<Bloom>("glow")
        .input("src")
        .threshold(0.4f)
        .intensity(1.5f)
        .radius(20.0f);

    // Desaturate slightly
    chain->add<HSV>("desat")
        .input("glow")
        .saturation(0.6f)
        .value(1.1f);

    // Subtle noise for displacement
    chain->add<Noise>("warpNoise")
        .scale(5.0f)
        .speed(0.2f);

    // Gentle warping
    chain->add<Displace>("warp")
        .source(&chain->get<HSV>("desat"))
        .map(&chain->get<Noise>("warpNoise"))
        .strength(0.02f);

    // Heavy blur for dreamy softness
    chain->add<Blur>("soft")
        .input("warp")
        .radius(5.0f);

    // Blend sharp and soft
    chain->add<Composite>("dream")
        .inputA("warp")
        .inputB("soft")
        .mode(BlendMode::Screen)
        .opacity(0.5f);

    chain->add<Output>("out").input("dream");
}

void update(Context& ctx) {
    chain->process();
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

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Base turbulent noise
    chain->add<Noise>("turb")
        .scale(4.0f)
        .speed(0.8f)
        .type(NoiseType::Simplex)
        .octaves(6);

    // Vertical gradient for fire shape
    chain->add<Gradient>("grad")
        .mode(GradientMode::Linear)
        .angle(90.0f)
        .colorA(1.0f, 1.0f, 1.0f)
        .colorB(0.0f, 0.0f, 0.0f);

    // Multiply to shape flames
    chain->add<Composite>("shaped")
        .inputA("turb")
        .inputB("grad")
        .mode(BlendMode::Multiply);

    // Fire color ramp (black -> red -> yellow -> white)
    chain->add<Ramp>("fireColors")
        .type(RampType::Linear)
        .hueSpeed(0.0f)
        .saturation(1.0f);

    // Apply fire colors
    chain->add<HSV>("colored")
        .input("shaped")
        .hueShift(-0.05f)  // Shift toward orange/red
        .saturation(1.5f)
        .value(1.2f);

    // Bloom for glow
    chain->add<Bloom>("glow")
        .input("colored")
        .threshold(0.3f)
        .intensity(1.0f)
        .radius(8.0f);

    chain->add<Output>("out").input("glow");
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

---

## Kaleidoscope

Mirrored kaleidoscope effect with animated source.

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Animated source pattern
    chain->add<Noise>("pattern")
        .scale(3.0f)
        .speed(0.3f)
        .type(NoiseType::Worley);

    // Colorize
    chain->add<HSV>("colored")
        .input("pattern")
        .hueShift(0.3f)
        .saturation(1.0f);

    // Kaleidoscope mirror
    chain->add<Mirror>("kaleido")
        .input("colored")
        .kaleidoscope(8);  // 8-fold symmetry

    // Rotate slowly
    chain->add<Transform>("spin")
        .input("kaleido")
        .rotate(0.01f);  // Rotates over time

    // Feedback for trails
    chain->add<Feedback>("trails")
        .input("spin")
        .decay(0.95f)
        .mix(0.3f);

    chain->add<Output>("out").input("trails");
}

void update(Context& ctx) {
    // Animate rotation
    chain->get<Transform>("spin").rotate(ctx.time() * 0.1f);
    chain->process();
}

VIVID_CHAIN(setup, update)
```

---

## Tips for Creating Your Own Effects

1. **Layer noise** - Multiple noise sources at different scales create organic patterns
2. **Use feedback sparingly** - High decay values (0.95+) create trails, lower values fade quickly
3. **Displacement adds movement** - Even subtle displacement (0.01-0.05) adds life
4. **Bloom sells it** - Bloom makes colors pop and creates atmosphere
5. **Match your chain resolution** to output - Higher res = sharper but slower
6. **Watch performance** - Blur and feedback are expensive; keep passes low
