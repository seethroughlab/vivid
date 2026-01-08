# Edge Glow

Creates neon outline effects using edge detection and bloom.

## Assets

- `assets/photo.jpg` - High-contrast photo for edge detection

## Operators Used

- **Image** - Loads source photo from assets
- **Edge** - Sobel edge detection
- **Brightness** - Contrast/brightness adjustment
- **HSV** - Color tinting
- **Bloom** - Glow effect
- **Composite** - Layer combination

## Key Concepts

### Edge Detection (Sobel)
Detects gradients/edges in the image:
```cpp
auto& edges = chain.add<Edge>("edges");
edges.input("source");
edges.strength = 2.0f;     // Edge intensity (0-5)
edges.threshold = 0.05f;   // Minimum edge value (0-1)
edges.invert = false;      // false=white on black, true=black on white
```

### Brightness/Contrast
Adjust edge visibility before bloom:
```cpp
auto& bright = chain.add<Brightness>("bright");
bright.input("edges");
bright.brightness = 0.1f;  // Add brightness (-1 to 1)
bright.contrast = 1.5f;    // Multiply contrast (0-4)
```

### Neon Glow Pipeline
Complete neon effect chain:
```cpp
// 1. Detect edges
auto& edges = chain.add<Edge>("edges");
edges.input("source");
edges.strength = 2.0f;

// 2. Boost and color the edges
auto& hsv = chain.add<HSV>("hsv");
hsv.input("edges");
hsv.hueShift = 0.5f;      // Shift to desired color
hsv.saturation = 2.0f;    // Boost color intensity

// 3. Add glow
auto& glow = chain.add<Bloom>("glow");
glow.input("hsv");
glow.threshold = 0.2f;    // Low threshold for full glow
glow.intensity = 2.5f;    // Strong glow
glow.radius = 20.0f;      // Wide spread

// 4. Composite over original
auto& final = chain.add<Composite>("final");
final.inputA("source");
final.inputB("glow");
final.mode(BlendMode::Add);
```

## Controls

- **Mouse X** - Edge strength (0.5-4.0)
- **Mouse Y** - Edge threshold (0-0.3)

## Common Presets

### Subtle Outline
```cpp
edges.strength = 1.0f;
edges.threshold = 0.15f;
glow.intensity = 1.0f;
glow.radius = 8.0f;
```

### Intense Neon
```cpp
edges.strength = 3.0f;
edges.threshold = 0.02f;
glow.intensity = 3.0f;
glow.radius = 30.0f;
```

### Technical Drawing
```cpp
edges.strength = 2.0f;
edges.threshold = 0.1f;
edges.invert = true;  // Black lines on white
// No bloom
```

### Animated Color Cycling
```cpp
hsv.hueShift = std::fmod(ctx.time() * 0.1f, 1.0f);
```
