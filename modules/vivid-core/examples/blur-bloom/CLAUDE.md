# Blur & Bloom

Demonstrates glow effects and cinematic post-processing.

## Operators Used

- **Blur** - Gaussian blur with configurable radius
- **Bloom** - Glow effect on bright areas
- **Vignette** - Edge darkening for cinematic look
- **Shape** - Bright shapes as bloom source
- **Composite** - Combine shapes with background

## Key Concepts

### Gaussian Blur
```cpp
auto& blur = chain.add<Blur>("blur");
blur.input("source");
blur.radius = 10.0f;  // Blur radius in pixels (0-50)
blur.passes = 2;      // More passes = smoother blur (1-10)
```

### Bloom Effect
Bloom extracts bright pixels, blurs them, and blends back:
```cpp
auto& bloom = chain.add<Bloom>("bloom");
bloom.input("source");
bloom.threshold = 0.5f;   // Only pixels above this brightness glow (0-1)
bloom.intensity = 1.5f;   // Glow strength multiplier (0-5)
bloom.radius = 15.0f;     // Glow spread in pixels (1-50)
bloom.passes = 2;         // Blur smoothness (1-8)
```

### Vignette
Darkens edges for a focused, cinematic look:
```cpp
auto& vignette = chain.add<Vignette>("vignette");
vignette.input("source");
vignette.intensity = 0.6f;   // Darkening strength (0-2)
vignette.softness = 0.5f;    // Gradient width (0-2)
vignette.roundness = 1.0f;   // 0=rectangular, 1=circular
```

## Common Effect Chains

### Dreamy Glow
```cpp
bloom.threshold = 0.3f;   // Low threshold = more glow
bloom.intensity = 2.0f;   // Strong glow
bloom.radius = 25.0f;     // Wide spread
```

### Cinematic Look
```cpp
bloom.threshold = 0.6f;
bloom.intensity = 1.0f;
bloom.radius = 10.0f;
vignette.intensity = 0.8f;  // Strong edge darkening
```

### Soft Focus
```cpp
blur.radius = 3.0f;   // Subtle blur
bloom.threshold = 0.7f;
bloom.intensity = 0.5f;
```

## Controls

No interactive controls - animations run automatically.
