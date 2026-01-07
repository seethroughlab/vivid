# Distortion Effects

Demonstrates spatial distortion and edge detection operators.

## Operators Used

- **ChromaticAberration** - RGB channel separation (lens fringing)
- **BarrelDistortion** - CRT-style curved screen effect
- **Displace** - Distort using a displacement map
- **Edge** - Sobel edge detection

## Key Concepts

### Chromatic Aberration
Simulates lens imperfection by offsetting RGB channels:
```cpp
auto& chroma = chain.add<ChromaticAberration>("chroma");
chroma.input("source");
chroma.amount = 0.02f;   // Separation distance (0-0.1)
chroma.radial = true;    // Radial from center (true) or directional (false)
chroma.angle = 0.0f;     // Direction angle when radial=false
```

### Barrel Distortion
Curves the image like a CRT monitor:
```cpp
auto& barrel = chain.add<BarrelDistortion>("barrel");
barrel.input("source");
barrel.curvature = 0.15f;  // 0=none, 1=extreme (0-1)
```

### Displacement Mapping
Distorts pixels based on a second texture:
```cpp
auto& dispMap = chain.add<Noise>("dispMap");
dispMap.scale = 3.0f;

auto& displace = chain.add<Displace>("displace");
displace.source("image");    // Texture to distort
displace.map("dispMap");     // Displacement map (R=X, G=Y)
displace.strength = 0.1f;    // Overall displacement (0-1)
displace.strengthX = 1.0f;   // X-axis multiplier (0-2)
displace.strengthY = 1.0f;   // Y-axis multiplier (0-2)
```

### Edge Detection
Highlights edges using the Sobel operator:
```cpp
auto& edge = chain.add<Edge>("edge");
edge.input("source");
edge.strength = 2.0f;     // Edge intensity (0-5)
edge.threshold = 0.1f;    // Minimum edge value to show (0-1)
edge.invert = false;      // true = white background
```

## Common Distortion Workflows

### Glitch Effect
```cpp
chroma.amount = 0.03f;
chroma.radial = false;
chroma.angle = 0.0f;  // Horizontal separation
```

### Retro CRT Look
```cpp
barrel.curvature = 0.1f;
chroma.amount = 0.01f;
// Combine with Scanlines from retro-crt example
```

### Liquid/Water Distortion
```cpp
// Use animated noise as displacement
dispMap.speed = 0.5f;
dispMap.scale = 2.0f;
displace.strength = 0.05f;
```

### Line Drawing Effect
```cpp
edge.strength = 3.0f;
edge.threshold = 0.2f;
edge.invert = true;  // White background
```

## Controls

No interactive controls - animations run automatically.
