# Edge Glow

Creates neon outline effects using edge detection, colorization, and bloom.

## Operators Used

- **Image** - Source image
- **Edge** - Sobel edge detection (outputs grayscale)
- **SolidColor** - Solid color for colorizing edges
- **Composite** - Multiply to colorize, Add to blend
- **Bloom** - Glow effect on colored edges
- **Brightness** - Darken original for contrast

## Key Concepts

### Why Edge + HSV Doesn't Work
Edge detection outputs **grayscale** (white edges on black). HSV adjustments only shift existing colors - they can't add color to grayscale. You must colorize first.

### Colorizing Grayscale Edges
Use SolidColor + Composite(Multiply):
```cpp
// Edge outputs white on black (grayscale)
auto& edges = chain.add<Edge>("edges");
edges.input("source");

// Create the neon color
auto& neonColor = chain.add<SolidColor>("neonColor");
neonColor.color.set(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan

// Multiply: white edges * cyan = cyan edges
auto& coloredEdges = chain.add<Composite>("coloredEdges");
coloredEdges.inputA("edges");
coloredEdges.inputB("neonColor");
coloredEdges.mode = BlendMode::Multiply;
```

### Complete Neon Glow Pipeline
```cpp
// 1. Detect edges
auto& edges = chain.add<Edge>("edges");
edges.input("source");
edges.strength = 3.0f;
edges.threshold = 0.08f;

// 2. Colorize with solid color
auto& neonColor = chain.add<SolidColor>("neonColor");
neonColor.color.set(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta

auto& coloredEdges = chain.add<Composite>("coloredEdges");
coloredEdges.inputA("edges");
coloredEdges.inputB("neonColor");
coloredEdges.mode = BlendMode::Multiply;

// 3. Apply bloom for glow
auto& glow = chain.add<Bloom>("glow");
glow.input("coloredEdges");
glow.threshold = 0.1f;
glow.intensity = 3.0f;
glow.radius = 25.0f;

// 4. Darken original for contrast
auto& darkened = chain.add<Brightness>("darkened");
darkened.input("source");
darkened.brightness = -0.3f;

// 5. Add glow over darkened original
auto& final = chain.add<Composite>("final");
final.inputA("darkened");
final.inputB("glow");
final.mode = BlendMode::Add;
```

### Animating Neon Color
Cycle through hues with manual HSV-to-RGB conversion:
```cpp
float hue = std::fmod(ctx.time() * 0.15f, 1.0f);
float r, g, b;
int i = static_cast<int>(hue * 6.0f);
float f = hue * 6.0f - i;
switch (i % 6) {
    case 0: r = 1; g = f;     b = 0; break;
    case 1: r = 1-f; g = 1;   b = 0; break;
    case 2: r = 0; g = 1;     b = f; break;
    case 3: r = 0; g = 1-f;   b = 1; break;
    case 4: r = f; g = 0;     b = 1; break;
    default: r = 1; g = 0;    b = 1-f; break;
}
neonColor.color.set(r, g, b, 1.0f);
```

## Controls

No interactive controls - color and glow animate automatically.

## Common Variations

### Static Cyan Neon
```cpp
neonColor.color.set(0.0f, 1.0f, 1.0f, 1.0f);
```

### Hot Pink Neon
```cpp
neonColor.color.set(1.0f, 0.2f, 0.8f, 1.0f);
```

### Green Matrix Style
```cpp
neonColor.color.set(0.0f, 1.0f, 0.3f, 1.0f);
edges.strength = 4.0f;  // Thinner, sharper lines
```
