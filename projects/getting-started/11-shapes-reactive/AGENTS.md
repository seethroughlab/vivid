# Lesson 11: Shapes Reactive

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Audio: AudioIn, BandSplit
- Core: Shape, Ramp, Composite, HSV, Bloom

## Lesson Focus
Audio-reactive visuals using geometric shapes instead of procedural noise.

## Key Concepts
- **Shape**: Geometric primitives (Star, Ellipse, Polygon) with audio-reactive size
- **Ramp**: Smooth gradients without noise
- **Composite**: Layer multiple shapes with blend modes
- **Center-based scaling**: Shapes pulse outward from center naturally

## Alternative Approaches

Instead of always using Noise, consider:

| Operator | Use Case | Audio Mapping |
|----------|----------|---------------|
| **Shape** | Geometric primitives | Size ← bass, rotation ← time, color ← bands |
| **Ramp** | Smooth gradients | Radius ← bass, color stops ← frequency bands |
| **Particles** | Point-based effects | Burst on beat, emit rate ← energy |
| **Flash** | Beat-synced intensity | Trigger on kick/snare |
| **Feedback** | Motion trails | Decay ← energy, zoom ← bass |
| **Plexus** | Network/node effects | Connection distance ← mid |

## When to Use Noise vs Shapes

**Use Noise when:**
- You want organic, flowing textures
- Displacement/warping effects
- Fire, smoke, water simulations

**Use Shapes when:**
- Clean geometric aesthetics
- Precise beat synchronization
- Center-outward pulsing effects
- VJ/club style visuals

## Shape Types
```cpp
ShapeType::Ellipse   // Circle/oval
ShapeType::Rectangle // Box
ShapeType::Triangle
ShapeType::Star      // Configurable points
ShapeType::Polygon   // N-sided
ShapeType::Line
```

## Suggested Modifications

1. **Add particles**: Burst on beat detection
   ```cpp
   auto& particles = chain.add<Particles>("particles");
   particles.emitRate = 0.0f;
   // In update: particles.burst(50);
   ```

2. **Add flash overlay**: Strobe on kick
   ```cpp
   auto& flash = chain.add<Flash>("flash");
   flash.decay = 0.85f;
   // In update: flash.trigger();
   ```

3. **Multiple concentric shapes**: Layer rings at different sizes

4. **Feedback trails**: Add motion blur effect
   ```cpp
   auto& fb = chain.add<Feedback>("fb");
   fb.decay = 0.9f;
   fb.zoom = 1.01f;
   ```

## Troubleshooting
- **Shapes not visible**: Check position is (0.5, 0.5) for center
- **No pulsing**: Ensure bass value is being read correctly
- **Colors too dim**: Increase bloom intensity or shape alpha

## Previous
10-project-organization: Structuring larger projects
