# Globe

A rotating Earth with PBR lighting, procedural noise displacement, and post-processing. Demonstrates textured 3D rendering with vertex shader displacement.

## Vision

A beautiful, slowly rotating Earth floating in space with terrain-like surface displacement. Warm sunlight illuminates one hemisphere while cool fill light provides subtle detail in the shadows. The planet is tilted like real Earth and drifts gently as the camera bobs.

## Technical Approach

- High-detail sphere (128 segments) with Earth texture
- Procedural Simplex noise for vertex displacement (terrain effect)
- Three-light setup: warm key light (sun), cool fill light (bounce), and rim light
- PBR shading with roughness/metallic material properties
- Vertex shader displacement using noise texture sampling
- Post-processing: subtle bloom for atmosphere, vignette for cinematic framing

## Operators

| Operator | Purpose |
|----------|---------|
| Sphere | Earth geometry (128 segments for smooth displacement) |
| Noise | Procedural Simplex noise for terrain displacement |
| TexturedMaterial | Earth texture with PBR properties |
| SceneComposer | Scene graph with transforms |
| CameraOperator | Orbit camera with gentle bob |
| DirectionalLight (x3) | Sun key + blue fill + rim light |
| Render3D | PBR rendering with displacement |
| Bloom | Subtle atmosphere glow |
| CRTEffect | Vignette only (other params zeroed) |

## Controls

- **Space** - Toggle auto-rotation
- **D** - Toggle displacement
- **Up/Down** - Adjust displacement amplitude
- **Tab** - Open parameter controls
