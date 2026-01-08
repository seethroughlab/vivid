# Time Effects

Creates slit-scan and temporal displacement effects using frame caching.

## Assets

- `assets/movie.mp4` - Video source with movement (essential for time effects)

## Operators Used

- **VideoPlayer** - Plays video as texture source (from vivid-video module)
- **FrameCache** - Stores N frames of history
- **TimeMachine** - Samples from cache based on grayscale map
- **Gradient** - Displacement maps for different time effects
- **Noise** - Organic displacement patterns

## Key Concepts

### Frame Cache
Stores a rolling buffer of frames:
```cpp
auto& cache = chain.add<FrameCache>("cache");
cache.input("source");         // What to cache
cache.frameCount = 60;         // ~2 seconds at 30fps
```

Parameters:
- `frameCount` (int, 2-128, default 32) - Number of frames to store

### Time Machine
Samples from cache using a displacement map:
```cpp
auto& tm = chain.add<TimeMachine>("timemachine");
tm.cache(&cache);              // Reference to FrameCache
tm.displacementMap("gradient"); // Grayscale controls time offset
tm.depth = 1.0f;               // How deep into cache (0-1)
tm.offset = 0.0f;              // Bias offset (0-1)
tm.invert = false;             // Flip dark/bright meaning
```

The displacement map controls which frame each pixel reads from:
- **Black pixels** → Oldest frames (past)
- **White pixels** → Newest frames (present)

### Classic Slit-Scan (Horizontal)
Horizontal slices show different times:
```cpp
auto& gradient = chain.add<Gradient>("gradient");
gradient.mode = GradientMode::Linear;
gradient.direction.set(0.0f, 1.0f);  // Vertical gradient
gradient.colorA.set(0, 0, 0, 1);     // Bottom = past
gradient.colorB.set(1, 1, 1, 1);     // Top = present

auto& slit = chain.add<TimeMachine>("slit");
slit.cache(&cache);
slit.displacementMap(&gradient);
```

### Radial Time Warp
Center shows present, edges show past:
```cpp
auto& radial = chain.add<Gradient>("radial");
radial.mode = GradientMode::Radial;
radial.colorA.set(1, 1, 1, 1);  // Center = present
radial.colorB.set(0, 0, 0, 1);  // Edge = past
```

### Organic Time Distortion
Noise-based displacement for surreal effects:
```cpp
auto& noise = chain.add<Noise>("noise");
noise.scale = 2.0f;
noise.speed = 0.3f;  // Slowly evolving

auto& organic = chain.add<TimeMachine>("organic");
organic.cache(&cache);
organic.displacementMap(&noise);
organic.depth = 0.8f;
```

## Controls

- **Mouse X** - Time depth (how far back in time)
- **Mouse Y** - Offset bias (shift toward newer/older frames)

## Use Cases

1. **Music Videos** - Classic slit-scan looks
2. **Dance/Movement** - Trail effects that follow motion
3. **Psychedelic Visuals** - Organic time warping
4. **Data Visualization** - Show change over time
5. **Video Feedback** - Combine with Feedback operator

## Performance Note

Higher `frameCount` uses more VRAM. For high-resolution output:
- 1080p × 60 frames ≈ 475 MB
- 4K × 60 frames ≈ 1.9 GB

Consider reducing `frameCount` or output resolution for performance.
