# Video Displacement Example

Demonstrates video playback with animated noise displacement. The video is distorted using a Noise texture as a displacement map, creating a fluid, organic warping effect.

## Pipeline

```
VideoFile → ─────────────────┐
                             ├→ Displacement → Output
Noise (animated) → ──────────┘
```

This demonstrates the **node composition** approach: instead of a specialized "noise displacement" mode, we chain a Noise node into the Displacement node's map input.

## Usage

1. Place a video file in the `assets/` folder (or use files from `examples/video-playback/assets/`)
   - Supported formats: MP4, MOV, M4V, AVI, MKV, WebM
   - HAP codec is supported for high-performance playback

2. Run the example:
   ```bash
   ./build/bin/vivid-runtime examples/video-displacement
   ```

## What You'll See

- **Video playback** - Loops continuously
- **Noise displacement** - Flowing, liquid-like distortion applied to each frame
- The displacement animates over time, creating a "melting" or "underwater" effect

## Effect Parameters

Edit `chain.cpp` to adjust the effect:

```cpp
// Noise parameters
noise_
    .scale(3.0f)      // Pattern size (larger = bigger patterns)
    .speed(0.3f)      // Animation speed
    .octaves(2);      // Noise complexity

// Displacement strength
float displacementAmount_ = 0.04f;
```

## Using Different Displacement Sources

You can replace the Noise node with any texture source:
- **Gradient** - Directional displacement
- **ImageFile** - Image-based displacement map
- **Another VideoFile** - Video-driven displacement
