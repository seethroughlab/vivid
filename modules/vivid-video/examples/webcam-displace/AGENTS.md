# Webcam Displacement

Demonstrates combining video operators with generator operators for real-time effects.

## Features Demonstrated

- **Webcam** - Live camera input
- **Noise** - Procedural displacement map generation
- **Displace** - Spatial displacement effect
- **Vignette** - Post-processing polish

## Key Concepts

### Video + Generator Combination

The power of Vivid comes from mixing different operator types:

```cpp
// Video input
auto& cam = chain.add<vivid::video::Webcam>("cam");

// Generator creates displacement map
auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;
noise.speed = 0.5f;

// Effect combines them
auto& displace = chain.add<Displace>("displace");
displace.source("cam");    // Video as source
displace.map("noise");     // Noise as displacement
displace.strength = 0.05f;
```

### Webcam Setup

```cpp
#include <vivid/video/video.h>

auto& cam = chain.add<vivid::video::Webcam>("cam");
cam.setResolution(1280, 720);  // Request specific resolution
cam.setFrameRate(30.0f);       // Request frame rate

// Webcam auto-selects first available camera
// Use cam.setDeviceIndex(1) for second camera
```

### Displacement Effect

The Displace operator shifts pixels based on a displacement map:

```cpp
auto& displace = chain.add<Displace>("displace");
displace.source("inputOperator");  // What to distort
displace.map("displacementMap");   // Controls the distortion
displace.strength = 0.1f;          // How much to displace (0-1 typical)
```

The displacement map is interpreted as:
- **Red channel** controls X displacement
- **Green channel** controls Y displacement
- 0.5 = no displacement, <0.5 = negative, >0.5 = positive

### Animated Displacement

Animate the noise for organic, flowing distortions:

```cpp
noise.speed = 0.5f;  // Built-in animation speed

// Or manual control via offset
noise.offset.set(0.0f, 0.0f, ctx.time() * 0.3f);
```

### Displacement Strength

```cpp
// Subtle distortion
displace.strength = 0.02f;

// Medium (visible waves)
displace.strength = 0.05f;

// Strong (psychedelic)
displace.strength = 0.15f;
```

## Common Patterns

### Using Gradient as Displacement

```cpp
// Create directional displacement
auto& gradient = chain.add<Gradient>("gradient");
gradient.mode = GradientMode::Linear;
gradient.angle = 0.0f;  // Horizontal
gradient.colorA.set(0.0f, 0.5f, 0.5f, 1.0f);  // No X, neutral Y
gradient.colorB.set(1.0f, 0.5f, 0.5f, 1.0f);  // Max X, neutral Y

displace.map("gradient");
```

### Audio-Reactive Displacement

```cpp
// Modulate strength with bass
auto& bands = chain.add<BandSplit>("bands");
bands.input("fft");

displace.strength.bind(
    [&bands]() { return bands.bass(); },
    0.01f, 0.2f  // Map bass to strength range
);
```

### Multiple Displacement Passes

```cpp
// First pass: large-scale warping
auto& displace1 = chain.add<Displace>("displace1");
displace1.source("cam");
displace1.map("noise_large");
displace1.strength = 0.1f;

// Second pass: fine detail
auto& displace2 = chain.add<Displace>("displace2");
displace2.source("displace1");
displace2.map("noise_fine");
displace2.strength = 0.02f;
```

## Controls

- **Mouse X**: Displacement strength (0.01 to 0.15)
- **Mouse Y**: Noise scale (1.0 to 10.0)
- **Space**: Toggle noise animation
- **R**: Reset to defaults
