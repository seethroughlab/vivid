# Webcam Glitch Example

Demonstrates live camera capture with a chain of glitch effects for a retro VHS/CRT aesthetic.

## Effect Chain

```
Webcam -> Chromatic Aberration -> Pixelate -> Scanlines
```

- **Chromatic Aberration**: RGB channel separation (radial mode, slowly rotating)
- **Pixelate**: Subtle blocky mosaic effect
- **Scanlines**: CRT-style horizontal lines with RGB sub-pixel simulation

## Usage

```bash
./build/bin/vivid-runtime examples/webcam
```

## What You'll See

Live video feed from your default camera with:
- Color fringing at the edges (chromatic aberration)
- Slight blockiness (pixelation)
- Horizontal scan lines with RGB phosphor pattern (CRT effect)

## Parameters

Edit `chain.cpp` to adjust the effect intensity:

```cpp
float chromaAmount_ = 0.012f;    // RGB separation (0.0-0.05)
float pixelSize_ = 3.0f;         // Block size (1 = none, higher = blockier)
float scanDensity_ = 400.0f;     // Lines per screen (100-800)
float scanIntensity_ = 0.25f;    // Line darkness (0.0-1.0)
```

## Effect Modes

### Chromatic Aberration
- Mode 0: Directional (along angle)
- Mode 1: Radial from center (default - stronger at edges)
- Mode 2: Barrel distortion style

### Scanlines
- Mode 0: Simple dark lines
- Mode 1: Alternating bright/dark (phosphor style)
- Mode 2: RGB sub-pixel simulation (default - authentic CRT)

## Combining with Other Effects

You can add more effects to the chain:

```cpp
// Add feedback for motion trails
Context::ShaderParams feedbackParams;
feedbackParams.param0 = 0.92f;  // decay
ctx.runShader("shaders/feedback.wgsl", &output_, feedback_, feedbackParams);
```
