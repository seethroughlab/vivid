# Shadow Demo Specification

Real-time shadow mapping demonstration with multiple light types.

## Overview

This example demonstrates:
- Directional light shadows (sun)
- Spot light shadows (flashlight)
- Point light shadows (omnidirectional lamp)
- Soft shadow edges using PCF
- Shadow quality/bias controls

## Scene Description

### Geometry
- Ground plane (10x10 units, receives shadows)
- 3 boxes at different positions (cast shadows)
- 2 spheres (cast shadows)
- 1 torus/ring (cast shadows)

### Lights
1. **Sun** — Directional light from above-left
   - Direction: (-0.5, -1.0, -0.3) normalized
   - Color: warm white (1.0, 0.95, 0.9)
   - Shadow map: 2048x2048

2. **Spotlight** — Focused cone light
   - Position: (3, 4, 2)
   - Target: origin
   - Inner angle: 15°
   - Outer angle: 25°
   - Shadow map: 1024x1024

3. **Lamp** — Point light in scene
   - Position: (-2, 2, 0)
   - Radius: 8 units
   - Shadow cubemap: 512x512 per face

### Camera
- Orbiting camera controlled by time or mouse
- Default: position (5, 4, 5), looking at origin

## Implementation Phases

### Phase 1: Scene Setup
- [ ] Create ground plane mesh
- [ ] Create primitive meshes (box, sphere, torus)
- [ ] Set up basic camera
- [ ] Add directional light (no shadows yet)
- [ ] Verify basic PBR rendering works

### Phase 2: Shadow Map Infrastructure
- [ ] Create depth-only texture format
- [ ] Create shadow map render pass
- [ ] Implement light view-projection matrix calculation
- [ ] Render scene from light's POV (depth only)

### Phase 3: Directional Shadow Mapping
- [ ] Calculate orthographic projection from scene bounds
- [ ] Render shadow map from sun direction
- [ ] Add shadow map sampler to main shader
- [ ] Implement shadow visibility test
- [ ] Apply shadow factor to lighting

### Phase 4: Soft Shadows (PCF)
- [ ] Implement 3x3 PCF kernel
- [ ] Add shadow bias to prevent acne
- [ ] Add normal bias for steep surfaces
- [ ] Test shadow quality at edges

### Phase 5: Spot Light Shadows
- [ ] Calculate perspective projection for spot
- [ ] Render spot shadow map
- [ ] Combine with directional shadows
- [ ] Handle spotlight falloff in shadow

### Phase 6: Point Light Shadows
- [ ] Create cubemap depth texture
- [ ] Render 6 faces for omnidirectional shadows
- [ ] Sample cubemap in main shader
- [ ] Handle point light attenuation

### Phase 7: Polish
- [ ] Add keyboard controls for shadow quality
- [ ] Add controls for bias adjustment
- [ ] Animate light positions
- [ ] Add on-screen debug visualization

## Controls

| Key | Action |
|-----|--------|
| 1-4 | Shadow quality (512, 1024, 2048, 4096) |
| B | Increase shadow bias |
| Shift+B | Decrease shadow bias |
| P | Toggle PCF on/off |
| D | Toggle shadow debug view |
| Space | Pause/resume light animation |

## Expected Output

A scene with multiple objects casting realistic shadows:
- Sharp shadows near contact points
- Soft shadows at distance
- Multiple shadow overlap where lights intersect
- No visible shadow acne or peter panning

## Technical Details

### Shadow Map Format
- Depth32Float for precision
- Comparison sampler for hardware PCF

### Uniform Buffers
```cpp
struct ShadowUniforms {
    glm::mat4 lightViewProj;  // For transforming to light space
    float bias;               // Depth bias
    float normalBias;         // Normal-based bias
    float pcfRadius;          // Soft shadow radius
    float shadowStrength;     // 0-1 shadow intensity
};
```

### Performance Targets
- 60 FPS with 3 shadow-casting lights
- Shadow maps rendered once per frame per light
- PCF adds ~10% overhead vs hard shadows

## References

- PLAN-06-3d.md — Full shadow mapping documentation
- LearnOpenGL Shadow Mapping — https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping
