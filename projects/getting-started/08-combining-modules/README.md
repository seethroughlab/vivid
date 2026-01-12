# Lesson 08: Combining Modules

Learn how to integrate multiple Vivid modules for powerful creative effects.

## What You'll Learn

- Using audio analysis to drive 3D parameters
- Applying 2D effects to 3D renders
- Integration patterns across modules
- Building complex, reactive systems

## Prerequisites

- Completed Lessons 05-07 (Audio, Video, 3D)

## Run It

```bash
./build/bin/vivid projects/getting-started/08-combining-modules
```

Play some music and watch the 3D scene react!

## Walkthrough

### The Integration Pattern

Vivid modules work seamlessly together:

```
[Audio Analysis] ─┐
                  ├─→ [3D Scene] ─→ [2D Effects] ─→ [Output]
[Video Input] ────┘
```

### Audio-Reactive 3D

In this example, audio analysis drives the 3D scene:

```cpp
// Get audio values
float bass = bands.bass();
float high = bands.high();

// Use audio to drive 3D parameters
float scale = 1.0f + bass * 0.5f;           // Bass makes cube bigger
float rotationSpeed = 0.5f + high * 2.0f;    // Highs speed up rotation
```

### 3D Output to 2D Effects

The Render3D output is just a texture - apply any 2D effect:

```cpp
auto& render = chain.add<Render3D>("render3d");
// ... setup scene, camera, light ...

auto& bloom = chain.add<Bloom>("bloom");
bloom.input("render3d");
bloom.intensity = 0.3f;

chain.output("bloom");
```

### Dynamic Material Colors

Change object colors based on audio:

```cpp
// In update():
auto& scene = chain.get<SceneComposer>("scene");
auto& entries = scene.entries();

// Make cube color pulse with bass
entries[0].color = glm::vec4(
    0.5f + bass * 0.5f,    // Red from bass
    0.3f + mid * 0.4f,     // Green from mids
    0.2f + high * 0.6f,    // Blue from highs
    1.0f
);
```

## Common Integration Patterns

### Pattern 1: Audio → Size/Scale
```cpp
float bass = bands.bass();
cube.size(1.0f + bass * 0.5f, 1.0f + bass * 0.5f, 1.0f + bass * 0.5f);
```

### Pattern 2: Audio → Rotation Speed
```cpp
float energy = levels.rms();
camera.azimuth(camera.azimuth() + energy * 0.05f);
```

### Pattern 3: Audio → Post-Processing
```cpp
float high = bands.high();
bloom.intensity = 0.2f + high * 0.5f;
```

### Pattern 4: Video as Texture (Advanced)
```cpp
// Use webcam as texture on 3D object
auto& cam = chain.add<Webcam>("cam");
// Note: Requires material setup - see vivid-render3d examples
```

## Try It

1. **Adjust sensitivity**: Change the multipliers for audio values
2. **Add more shapes**: Create multiple objects reacting differently
3. **Change effects**: Try `ChromaticAberration` instead of bloom
4. **Use beat detection**: Flash on beats using `BeatDetect`

## Tips for Audio-Reactive Work

- **Smooth it**: Use high smoothing values (0.85+) for gentle motion
- **Map ranges**: Map 0-1 audio values to useful parameter ranges
- **Layer effects**: Combine subtle changes for organic feel
- **Test with music**: Different genres need different tuning

## Next Steps

- **Lesson 09**: Creating custom operators
- **Explore**: `projects/showcase/` for complex integrated projects
