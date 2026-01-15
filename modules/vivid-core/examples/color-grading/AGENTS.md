# Color Grading

Demonstrates color correction and stylization operators.

## Operators Used

- **VideoPlayer** - Video source
- **HSV** - Hue shift, saturation, value adjustment
- **Brightness** - Brightness and contrast control
- **Quantize** - Reduce color palette (posterization)

## Key Concepts

### HSV Adjustment
```cpp
auto& hsv = chain.add<HSV>("hsv");
hsv.input("source");
hsv.hueShift = 0.15f;    // Shift hue (0-1 wraps)
hsv.saturation = 1.2f;   // >1 increases, <1 decreases
hsv.value = 1.0f;        // Brightness multiplier
```

### Brightness & Contrast
```cpp
auto& bright = chain.add<Brightness>("bright");
bright.input("source");
bright.brightness = 0.1f;   // -1 to 1, offset
bright.contrast = 1.2f;     // >1 increases contrast
```

### Color Quantization (Posterization)
```cpp
auto& quant = chain.add<Quantize>("quant");
quant.input("source");
quant.levels = 8;  // Number of color levels per channel
```

## Common Color Grading Workflows

### Warm/Cool Shift
```cpp
hsv.hueShift = 0.05f;   // Warm (toward orange)
hsv.hueShift = -0.05f;  // Cool (toward blue)
```

### High Contrast Look
```cpp
bright.contrast = 1.5f;
bright.brightness = -0.1f;  // Compensate for increased brightness
```

### Retro Palette
```cpp
quant.levels = 4;  // Reduced color palette
hsv.saturation = 1.3f;  // Boost saturation
```

## Controls

No interactive controls - animations run automatically.
