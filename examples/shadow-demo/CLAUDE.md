# Shadow Demo - Claude Code Context

## About This Example

Demonstrates real-time shadow mapping in Vivid with directional, spot, and point lights.

## Features to Implement

1. **Directional Light Shadows** — Sun-like parallel light with shadow map
2. **Spot Light Shadows** — Flashlight-style cone with perspective shadow
3. **Point Light Shadows** — Omnidirectional shadows using cubemap
4. **PCF Soft Shadows** — Blurred shadow edges for realism
5. **Interactive Controls** — Adjust light position, shadow quality, bias

## Key Files

- `chain.cpp` — Main demo code with shadow setup
- `SPEC.md` — Detailed specification and task tracking

## Scene Setup

```
Ground Plane
    |
    +-- Box (casts shadow)
    +-- Sphere (casts shadow)
    +-- Torus (casts shadow)
    |
Directional Light (sun) -----> Shadow Map (2D)
Spot Light (flashlight) -----> Shadow Map (2D)
Point Light (lamp) ----------> Shadow Cubemap (6-face)
```

## Implementation Notes

### Shadow Map Pass
Render scene from light's perspective, depth-only:
- Directional: Orthographic projection covering scene bounds
- Spot: Perspective projection with light's cone angle
- Point: 6 perspective renders for cubemap faces

### Main Pass
Sample shadow map to determine visibility:
- Transform fragment position to light space
- Compare depth against shadow map
- Apply PCF for soft edges

### Common Issues
- **Shadow Acne**: Solved with depth bias
- **Peter Panning**: Balance bias to avoid floating shadows
- **Aliasing**: Use higher resolution or PCF

## API Reference (When Implemented)

```cpp
// Configure shadows on render
ctx.render3DPBR(mesh, material)
    .shadows(true)
    .shadowResolution(2048)
    .shadowBias(0.001f)
    .pcfRadius(1.5f);

// Per-light shadow control
DirectionalLight sun;
sun.castShadows = true;
sun.shadowStrength = 1.0f;
```
