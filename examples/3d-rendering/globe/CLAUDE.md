# Globe

A rotating Earth with PBR lighting and post-processing. Demonstrates textured 3D rendering with cinematic presentation.

## Vision

A beautiful, slowly rotating Earth floating in space. Warm sunlight illuminates one hemisphere while cool fill light provides subtle detail in the shadows. The planet is tilted like real Earth and drifts gently as the camera bobs.

## Technical Approach

- High-detail sphere (64 segments) with Earth texture
- Two-light setup: warm key light (sun) and cool fill light (bounce)
- PBR shading with roughness/metallic material properties
- Post-processing: subtle bloom for atmosphere, vignette for cinematic framing

## Operators

| Operator | Purpose |
|----------|---------|
| Sphere | Earth geometry |
| TexturedMaterial | Earth texture with PBR properties |
| SceneComposer | Scene graph with transforms |
| CameraOperator | Orbit camera with gentle bob |
| DirectionalLight (x2) | Sun key + blue fill |
| Render3D | PBR rendering |
| Bloom | Subtle atmosphere glow |
| CRTEffect | Vignette only (other params zeroed) |

## Controls

- **Space** - Toggle auto-rotation
- **Tab** - Open parameter controls
