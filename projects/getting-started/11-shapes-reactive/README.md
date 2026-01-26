# Lesson 11: Shapes Reactive

Audio-reactive visuals using geometric shapes instead of procedural noise.

## Run

```bash
./build/bin/vivid projects/getting-started/11-shapes-reactive
```

## What You'll Learn

- Using **Shape** operator for geometric primitives
- Using **Ramp** for smooth gradient backgrounds
- **Compositing** multiple shapes with blend modes
- Audio-reactive **size**, **rotation**, and **color**
- Alternative patterns to noise-based visuals

## Controls

- **Cmd/Ctrl+F**: Toggle fullscreen
- Play music or make sounds to see the shapes react!

## Key Concepts

This lesson demonstrates that audio-reactive visuals don't need procedural noise. Instead:

1. **Bass → Shape Size**: Star pulses outward from center
2. **Mids → Ring Size**: Outer ring expands
3. **Highs → Color Shift**: Hue rotates with treble
4. **Time → Rotation**: Continuous spinning animation

## Why Shapes?

| Approach | Best For |
|----------|----------|
| **Shapes** | Clean geometry, precise beats, VJ style |
| **Noise** | Organic textures, flowing patterns |
| **Particles** | Explosive effects, sparkles |
| **Gradients** | Smooth color transitions |

## Try This

1. Change `ShapeType::Star` to `ShapeType::Polygon`
2. Add more concentric rings
3. Try `BlendMode::Screen` instead of `Add`
4. Add a `Flash` operator for beat-synced strobes
