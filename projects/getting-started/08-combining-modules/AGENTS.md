# Lesson 8: Combining Modules

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Audio: AudioIn, BandSplit, BeatDetect
- Render3D: SceneComposer, Box, Sphere, Torus, Render3D
- Core: Bloom, ChromaticAberration

## Lesson Focus
Integrating multiple Vivid modules: audio-reactive 3D with post-processing.

## Key Concepts
- **Module Integration**: All modules produce/consume textures
- **Audio -> Parameters**: Map audio values to any numeric parameter
- **3D -> 2D Pipeline**: Render3D output feeds into 2D effects
- **Dynamic Updates**: Change scene properties every frame

## Integration Patterns
| Source | Target | Example |
|--------|--------|---------|
| Audio bass | Object scale | `1.0 + bass * 0.5` |
| Audio RMS | Rotation speed | `0.3 + rms * 2.0` |
| Audio high | Bloom intensity | `0.2 + high * 0.4` |
| Beat | Flash/trigger | `if (beat.beat())` |
| 3D render | 2D effects | `bloom.input("render3d")` |

## Audio Smoothing Tips
| Smoothing | Feel | Use Case |
|-----------|------|----------|
| 0.5 | Quick, punchy | Beat triggers |
| 0.85 | Balanced | Scale changes |
| 0.95 | Smooth, ambient | Color shifts |

## Suggested Modifications
1. Add beat-triggered effects: `if (beat.beat()) { /* flash */ }`
2. Use video as scene background with Composite
3. Multiple objects reacting to different frequency bands
4. Add ChromaticAberration driven by bass

## Troubleshooting
- **Too jittery**: Increase audio smoothing
- **Delayed response**: Decrease audio smoothing
- **Values too extreme**: Reduce multipliers
- **Changes not visible**: Increase multipliers or check parameter ranges

## Next
09-custom-operators: Creating your own operators
