# Image Loader Example

Demonstrates loading an image with alpha transparency, applying animated noise displacement, and compositing over an animated gradient background.

## Pipeline

```
ImageFile → ─────────────────┐
                             ├→ Displacement → ─┐
Noise (animated) → ──────────┘                  ├→ Composite → Output
                                                │
Gradient (animated) → ──────────────────────────┘
```

This demonstrates **node composition**: the Displacement uses a Noise texture as its map, and the Composite blends the displaced image over an animated gradient.

## Usage

1. Place an image file in the `assets/` folder:
   - Supported formats: PNG, JPG, BMP
   - For best results, use a PNG with transparency (alpha channel)

2. Run the example:
   ```bash
   ./build/bin/vivid examples/image-loader
   ```

## What You'll See

- **Animated gradient background** - Colorful radial gradient that shifts colors over time
- **Image with alpha** - Your image composited over the gradient (transparent areas show the gradient)
- **Noise displacement** - Flowing, liquid-like distortion applied to the image

This demonstrates alpha transparency working correctly with the built-in Composite operator.

## Effect Parameters

Edit `chain.cpp` to adjust the effect:

```cpp
// Noise parameters for displacement map
noise_
    .scale(4.0f)      // Pattern size
    .speed(0.5f)      // Animation speed
    .octaves(2);      // Noise complexity

// Displacement strength
float displacementAmount_ = 0.03f;
```

## Sample Images

Try these types of images for interesting effects:
- Logo with transparent background
- Silhouette or cutout shape
- Text rendered as PNG with transparency
- Photo with alpha mask
