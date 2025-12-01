# Project Context for Claude

## About This Project
Audio-reactive Wipeout 2097-style hover vehicle generator. A showcase example demonstrating multiple Vivid features: procedural 3D geometry, PBR materials, decals, bloom, and audio-driven animation.

## Key Files
- `chain.cpp` - Main chain code with vehicle generation and rendering
- `SPEC.md` - Project specification and task checklist

## Key APIs Used
- `ctx.createMesh(vertices, indices)` - Build custom geometry
- `ctx.createCube/Sphere/Cylinder()` - Primitive shapes
- `ctx.render3DPBR()` - PBR rendering with materials
- `ctx.createDecal()` - Projected decals for livery
- `AudioIn` operator - Audio FFT analysis (bass/mid/high bands)
- `Bloom` operator - Glow post-processing
- `Particles` operator - 2D particle exhaust (composited over 3D)

## Geometry Building Pattern
```cpp
std::vector<Vertex3D> verts;
std::vector<uint32_t> indices;
// Add vertices with position, normal, uv
verts.push_back({pos, normal, uv});
// Add triangle indices
indices.push_back(0); indices.push_back(1); indices.push_back(2);
// Create mesh
Mesh3D mesh = ctx.createMesh(verts, indices);
```

## Conventions
- Use `// GOAL:` comments to describe intent
- Use `// === SECTION ===` to mark code sections
- Update SPEC.md checkboxes when completing tasks
