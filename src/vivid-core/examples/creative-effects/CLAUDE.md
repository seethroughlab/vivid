# Creative Effects

Demonstrates experimental and stylized visual effects.

## Operators Used

- **Pixelate** - Mosaic/retro pixel art effect
- **Plexus** - Animated particle network visualization
- **FilmGrain** - Vintage analog film grain
- **Flash** - Beat-synced strobe effect

## Key Concepts

### Pixelate
Creates a mosaic by sampling in blocks:
```cpp
auto& pixelate = chain.add<Pixelate>("pixelate");
pixelate.input("source");
pixelate.size.set(16.0f, 16.0f);  // 16x16 pixel blocks (1-100)
```

### Plexus Network
GPU-accelerated particle network with proximity connections:
```cpp
auto& plexus = chain.add<Plexus>("plexus");

// Node configuration
plexus.setNodeCount(200);
plexus.setNodeSize(0.005f);
plexus.setNodeColor(0.0f, 0.8f, 1.0f, 1.0f);

// Connection configuration
plexus.setConnectionDistance(0.1f);  // Max distance for lines
plexus.setLineWidth(1.0f);
plexus.setLineColor(0.0f, 0.6f, 0.9f, 0.4f);

// Physics
plexus.setTurbulence(0.1f);
plexus.setDrag(0.5f);
plexus.setCenterAttraction(0.02f);
plexus.setSpread(0.8f);

// 3D mode
plexus.setEnable3D(true);
plexus.setCameraDistance(2.5f);
plexus.setAutoRotate(0.2f);

// Appearance
plexus.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
```

### Film Grain
Adds analog film texture:
```cpp
auto& grain = chain.add<FilmGrain>("grain");
grain.input("source");
grain.intensity = 0.2f;   // Grain visibility (0-1)
grain.size = 1.5f;        // Grain size (0.5-4, lower = finer)
grain.speed = 24.0f;      // Animation speed (0-60)
grain.colored = 0.3f;     // Color variation (0=mono, 1=full color)
```

### Flash Effect
Triggered strobe with decay:
```cpp
auto& flash = chain.add<Flash>("flash");
flash.input("source");
flash.decay = 0.9f;             // Decay rate (0.5-0.995)
flash.color.set(1, 0.9, 0.8);   // Warm white
flash.mode = 0;                  // 0=Additive, 1=Screen, 2=Replace

// In update():
if (beat.triggered()) {
    flash.trigger();       // Full intensity
    flash.trigger(0.5f);   // Half intensity
}

float currentIntensity = flash.intensity();  // Read current level
```

## Related Temporal Effects

For time-based effects, see `time-machine` example which demonstrates:

### FrameCache
Stores a rolling history of frames:
```cpp
auto& cache = chain.add<FrameCache>("cache");
cache.input("video");
cache.frameCount = 64;  // Cache ~2 seconds at 30fps
```

### TimeMachine
Temporal displacement using a grayscale map:
```cpp
auto& timeMachine = chain.add<TimeMachine>("tm");
timeMachine.cache(&cache);
timeMachine.displacementMap(&gradient);  // Dark=old, bright=new
timeMachine.depth = 1.0f;    // How deep into cache (0-1)
timeMachine.invert = false;  // Invert direction
```

## Common Patterns

### Retro Video Game Look
```cpp
pixelate.size.set(8.0f, 8.0f);
// Combine with Quantize from color-grading
```

### VJ Strobe
```cpp
flash.decay = 0.85f;  // Fast decay
flash.mode = 0;       // Additive for bright flash
// Trigger on beat detection
```

### Cinematic Film Look
```cpp
grain.intensity = 0.1f;
grain.size = 1.0f;
grain.colored = 0.0f;  // Mono grain
// Combine with Vignette
```

## Controls

No interactive controls - animations run automatically.
