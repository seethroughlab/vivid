# Vivid Project Ideas

Creative project ideas that showcase Vivid's strengths. Each idea is designed to be achievable in a single session while producing visually impressive results.

---

## 1. Audio Visualizer with Beat Detection

**Strengths:** Audio analysis, real-time reactivity, hot-reload

Create a music visualizer that responds to audio input. Use `BeatDetect` for rhythm, `FFT` for spectrum, and `Levels` for amplitude. Drive shapes, colors, and effects based on the music.

```cpp
// Core operators
chain.add<AudioIn>("mic");
chain.add<BeatDetect>("beat").input("mic");
chain.add<FFT>("fft").input("mic");

// Visuals that react to audio
auto& shape = chain.add<Shape>("shape");
// In update(): shape.size = 0.2f + beat.kick() * 0.5f;
```

**Expand it:** Add multiple visual layers, each responding to different frequency bands.

---

## 2. Infinite Tunnel with Feedback

**Strengths:** Feedback operator, temporal effects, hypnotic visuals

Use `Feedback` with zoom and rotation to create an endless tunnel effect. Add `Noise` or `Shape` as the source and watch patterns spiral infinitely.

```cpp
chain.add<Noise>("source");
chain.add<Feedback>("feedback").input(&source);
feedback.zoom = 1.01f;
feedback.rotate = 0.02f;
feedback.decay = 0.95f;
```

**Expand it:** Make zoom/rotation respond to mouse position or audio.

---

## 3. Generative Art with Noise Layering

**Strengths:** GPU noise, compositing, parameter exploration

Layer multiple `Noise` operators with different scales and speeds, composite them with various blend modes to create evolving abstract art.

```cpp
chain.add<Noise>("macro").scale(2.0f).speed(0.1f);
chain.add<Noise>("detail").scale(8.0f).speed(0.3f);
chain.add<Composite>("blend").mode(BlendMode::Multiply);
chain.add<HSV>("color");  // Add color shifting
```

**Expand it:** Hot-reload to tweak parameters and discover new patterns.

---

## 4. Retro CRT Monitor Effect

**Strengths:** Effect chaining, retro aesthetics, video processing

Apply a chain of retro effects to any input: `Downsample` for pixelation, `Dither` for color reduction, `Scanlines` for that classic look, and `BarrelDistortion` for CRT curvature.

```cpp
chain.add<VideoPlayer>("video").file("input.mov");
chain.add<Downsample>("pixel").factor(4);
chain.add<Dither>("dither").colors(16);
chain.add<Scanlines>("scan").intensity(0.3f);
chain.add<BarrelDistortion>("crt").amount(0.1f);
```

**Expand it:** Add chromatic aberration and vignette for full nostalgia.

---

## 5. Particle System Fireworks

**Strengths:** PointSprites GPU particles, bloom, additive blending

Create a fireworks display using `PointSprites` with thousands of particles, add `Bloom` for glow, and use color gradients for variety.

```cpp
chain.add<PointSprites>("sparks").setCount(10000);
sparks.setColor(1.0f, 0.8f, 0.2f, 1.0f);
sparks.setColor2(1.0f, 0.2f, 0.1f, 1.0f);
chain.add<Bloom>("glow").intensity(1.5f);
```

**Expand it:** Trigger bursts on mouse click or beat detection.

---

## 6. Live Canvas Drawing

**Strengths:** Canvas 2D API, immediate-mode graphics, algorithmic art

Use the `Canvas` operator to draw procedural graphics - spirographs, L-systems, particle trails, or data visualization.

```cpp
auto& canvas = chain.add<Canvas>("canvas");
canvas.size(1920, 1080);

// In update():
for (int i = 0; i < 100; i++) {
    float angle = time * 0.1f + i * 0.1f;
    float x = 640 + cos(angle * 3) * 200 * sin(angle);
    float y = 360 + sin(angle * 2) * 200 * cos(angle);
    canvas.fillCircle(x, y, 3.0f);
}
```

**Expand it:** Combine with feedback for persistent trails.

---

## 7. Video Glitch Art

**Strengths:** Video playback, displacement, real-time manipulation

Load a video and apply glitch effects: `Displace` with noise, `RGB Split`, aggressive `Feedback`, and sudden parameter jumps.

```cpp
chain.add<VideoPlayer>("vid").file("footage.mov");
chain.add<Noise>("glitch").scale(20.0f);
chain.add<Displace>("displace").map(&glitch).amount(50.0f);
// Randomly spike displacement amount for glitch moments
```

**Expand it:** Trigger glitches on audio transients.

---

## 8. Interactive Light Installation (DMX)

**Strengths:** DMX output, hardware integration, physical computing

Control real lighting fixtures from Vivid. Generate colors procedurally and send them to DMX fixtures for an immersive installation.

```cpp
chain.add<DMXOut>("lights").port("/dev/tty.usbserial");
chain.add<Noise>("color_gen");

// In update():
dmx.setChannel(1, static_cast<uint8_t>(r * 255));
dmx.setChannel(2, static_cast<uint8_t>(g * 255));
dmx.setChannel(3, static_cast<uint8_t>(b * 255));
```

**Expand it:** Sync with music using audio analysis.

---

## 9. OSC-Controlled VJ Tool

**Strengths:** OSC input, external control, live performance

Build a visual instrument controlled by TouchOSC, a MIDI controller, or another application. Map OSC messages to visual parameters.

```cpp
chain.add<OSCReceiver>("osc").port(8000);

// In update():
float fader1 = osc.value("/1/fader1");
float fader2 = osc.value("/1/fader2");
noise.scale = 1.0f + fader1 * 10.0f;
bloom.intensity = fader2 * 2.0f;
```

**Expand it:** Add multiple visual scenes and crossfade between them.

---

## 10. 3D Shape Morphing

**Strengths:** Render3D, CSG operations, PBR materials

Create morphing 3D shapes using CSG (Constructive Solid Geometry). Blend between primitives and animate the blend factor.

```cpp
chain.add<CSGShape>("morph")
    .operation(CSGOp::SmoothUnion)
    .shapeA(CSGPrimitive::Sphere)
    .shapeB(CSGPrimitive::Box)
    .blend(0.5f);

// In update():
morph.blend(sin(time) * 0.5f + 0.5f);
```

**Expand it:** Add PBR materials and environment lighting.

---

## Contributing Ideas

Add your own ideas below! Good ideas:
- Showcase a specific Vivid strength
- Are achievable in 1-2 hours
- Produce visually interesting results
- Can be expanded with more features

---

