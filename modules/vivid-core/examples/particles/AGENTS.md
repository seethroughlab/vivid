# Cosmic Nebula

A visually stunning particle effect demonstrating layered composition and post-processing.

## Operators Used

- **Particles** - GPU particle system with sprite rendering
- **Composite** - Additive blending of particle layers
- **Bloom** - Post-processing glow effect

## Key Concepts

### Layered Composition
Stack multiple particle systems with additive blending:
```cpp
auto& swirl = chain.add<Particles>("swirl");
swirl.colorMode = ColorMode::Rainbow;
swirl.clearColor.set(0, 0, 0.02f, 1);  // Dark background

auto& core = chain.add<Particles>("core");
core.colorMode = ColorMode::Gradient;
core.clearColor.set(0, 0, 0, 0);  // Transparent!

auto& comp = chain.add<Composite>("comp");
comp.input(0, "swirl");
comp.input(1, "core");
comp.mode = BlendMode::Add;  // Additive = glow
```

### Point Attractor Physics
Create swirling motion with central attractor:
```cpp
particles.attractorPosition.set(0.5f, 0.5f);  // Center
particles.attractorStrength = 0.15f;           // Gentle pull
particles.turbulence = 0.8f;                   // Organic jitter
particles.drag = 0.3f;                         // Slow particles down
```

### High Density Effects
Many small particles blend together for smooth appearance:
```cpp
particles.emitRate = 400.0f;      // Dense emission
particles.maxParticles = 8000;    // Large pool
particles.size = 0.008f;          // Small particles
particles.sizeEnd = 0.002f;       // Shrink over lifetime
```

### Bloom Post-Processing
Add ethereal glow to bright particles:
```cpp
auto& bloom = chain.add<Bloom>("bloom");
bloom.input("comp");
bloom.threshold = 0.15f;   // Low threshold catches more
bloom.intensity = 1.2f;    // Moderate glow
bloom.radius = 25.0f;      // Wide soft blur
```

## Controls

- **Mouse X** - Emitter size (nebula spread)
- **Mouse Y** - Attractor strength (push ↔ pull)

## Particle Layers

| Layer | Emitter | Purpose |
|-------|---------|---------|
| Swirl | Ring | Rainbow outer particles with attractor physics |
| Core | Disc | White→cyan gradient, brighter, shorter-lived |

## Tips for Better Particles

1. **Layer multiple systems** - Depth and visual interest
2. **Additive blending** - Natural glow without alpha sorting
3. **Post-process with Bloom** - Ethereal softness
4. **High density, small size** - Hides individual particles
5. **Size over lifetime** - Particles that shrink feel more natural
