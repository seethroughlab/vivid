# Lesson 08: Combining Modules

This lesson demonstrates integrating multiple Vivid modules together.

## Lesson Objectives

1. Use audio analysis to drive 3D scene parameters
2. Apply 2D post-processing to 3D renders
3. Understand cross-module integration patterns
4. Build reactive, dynamic systems

## Key Concepts

- **Module Integration**: All modules produce/consume textures
- **Audio → Parameters**: Map audio values to any numeric parameter
- **3D → 2D Pipeline**: Render3D output feeds into 2D effects
- **Dynamic Updates**: Change scene properties every frame

## What the Code Demonstrates

- Audio capture and frequency analysis
- 3D scene with audio-reactive transforms
- Post-processing with audio-reactive bloom
- Color changes based on audio frequencies

## Suggested Modifications

1. **Add beat-triggered effects**:
   ```cpp
   auto& beat = chain.add<BeatDetect>("beat");
   beat.input("audio");

   // In update():
   if (beat.beat()) {
       // Flash, spawn particles, change colors, etc.
   }
   ```

2. **Use video as scene background**:
   ```cpp
   auto& cam = chain.add<Webcam>("cam");
   // Composite 3D render over video
   auto& comp = chain.add<Composite>("comp");
   comp.inputA("cam");
   comp.inputB("render3d");
   comp.mode = BlendMode::Screen;
   ```

3. **Multiple objects with different reactions**:
   ```cpp
   // Cube reacts to bass
   entries[0].transform = ... scale by bass ...

   // Sphere reacts to mids
   entries[1].transform = ... scale by mid ...

   // Torus reacts to highs
   entries[2].transform = ... rotate faster with high ...
   ```

4. **Add chromatic aberration**:
   ```cpp
   auto& chroma = chain.add<ChromaticAberration>("chroma");
   chroma.input("bloom");
   chroma.strength = 0.01f + bass * 0.02f;
   ```

## Integration Patterns Reference

| Source | Target | Example |
|--------|--------|---------|
| Audio bass | Object scale | `1.0 + bass * 0.5` |
| Audio RMS | Rotation speed | `0.3 + rms * 2.0` |
| Audio high | Bloom intensity | `0.2 + high * 0.4` |
| Beat | Flash/trigger | `if (beat.beat())` |
| Video | 3D texture | Material texture input |
| 3D render | 2D effects | `bloom.input("render3d")` |

## Audio Smoothing Tips

| Smoothing | Feel | Use Case |
|-----------|------|----------|
| 0.5 | Quick, punchy | Beat triggers |
| 0.85 | Balanced | Scale changes |
| 0.95 | Smooth, ambient | Color shifts |

## Common Issues

- **Too jittery**: Increase audio smoothing
- **Delayed response**: Decrease audio smoothing
- **Values too extreme**: Reduce multipliers
- **Changes not visible**: Increase multipliers or check parameter ranges

## Next Lesson

09-custom-operators: Creating your own operators
