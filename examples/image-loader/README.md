# Image Loader Example

Demonstrates loading an image with alpha transparency, applying animated noise displacement, and compositing over an animated gradient background.

## Usage

1. Place an image file in the `assets/` folder:
   - Supported formats: PNG, JPG, BMP
   - For best results, use a PNG with transparency (alpha channel)

2. Run the example:
   ```bash
   ./build/bin/vivid-runtime examples/image-loader
   ```

## What You'll See

- **Animated gradient background** - Colorful radial gradient that shifts colors over time
- **Image with alpha** - Your image composited over the gradient (transparent areas show the gradient)
- **Noise displacement** - Flowing, liquid-like distortion applied to the image

This proves that alpha transparency is working correctly!

## Effect Parameters

Edit `chain.cpp` to adjust the effect:

```cpp
float displacementAmount_ = 0.03f;   // Displacement strength (0.01 - 0.1)
float noiseScale_ = 4.0f;            // Noise pattern size (1.0 - 10.0)
float noiseSpeed_ = 0.5f;            // Noise animation speed (0.1 - 2.0)
float gradientSpeed_ = 1.0f;         // Background gradient animation speed
```

## Sample Images

Try these types of images for interesting effects:
- Logo with transparent background
- Silhouette or cutout shape
- Text rendered as PNG with transparency
- Photo with alpha mask

## Shaders

- `image_over_gradient.wgsl` - Combined shader that:
  1. Generates animated HSV-based radial gradient
  2. Applies simplex noise displacement to image UVs
  3. Composites image over gradient using alpha blending
  4. Fades edges smoothly where displacement samples outside bounds
