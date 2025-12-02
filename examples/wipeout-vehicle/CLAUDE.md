# Project Context for Claude

## About This Project

Procedural Wipeout 2097-style anti-gravity racer generator. This is a **retro aesthetic showcase**, NOT a PBR demo. The goal is to recreate the PS1-era visual style with modern procedural techniques: low-poly faceted geometry, single diffuse textures, vertex-lit shading, and retro post-processing effects.

## Design Philosophy

- **NO PBR**: This example deliberately avoids metallic/roughness workflows
- **Faceted geometry**: Use flat shading with per-face normals for that chunky PS1 look
- **Single 256×256 texture**: All visual detail comes from one diffuse map per craft
- **Vertex lighting**: Simple N·L diffuse with optional quantization (toon steps)
- **Limited palette**: 8-12 colors per team, high contrast

## Key Files

- `chain.cpp` - Main procedural generation code
- `SPEC.md` - Comprehensive specification with geometry, textures, core features needed
- `GOAL.txt` - Aesthetic description and reference for PS1-era Wipeout style
- `reference/` - 8 visual reference images of detailed craft designs

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    GEOMETRY GENERATION                          │
├─────────────────────────────────────────────────────────────────┤
│  Fuselage  │  Side Pods  │  Engine Nacelles  │  Wings/Fins     │
│  (80 tri)  │  (60×2 tri) │  (40×2 tri)       │  (30+20×2 tri)  │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    TEXTURE GENERATION                           │
├─────────────────────────────────────────────────────────────────┤
│  Team colors  │  Racing stripes  │  Decals  │  Panel lines     │
│  (blocks)     │  (procedural)    │  (logos) │  (seams)         │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    RETRO RENDERING (needs core work)            │
├─────────────────────────────────────────────────────────────────┤
│  Vertex-lit shader  │  Flat shading  │  Dithering  │  Low-res  │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    AUDIO REACTIVITY                             │
├─────────────────────────────────────────────────────────────────┤
│  Bass → Hover  │  Mids → Engine glow  │  Beat → Flash effects  │
└─────────────────────────────────────────────────────────────────┘
```

## Key APIs Used

### Geometry
- `ctx.createMesh(vertices, indices)` - Build custom faceted geometry
- Use `Vertex3D` with per-face normals (duplicate vertices for flat shading)
- X-axis mirroring for symmetrical craft

### Rendering (current, needs replacement)
- `ctx.render3DPBR()` - Current PBR (to be replaced with vertex-lit)
- **NEEDED**: `ctx.render3DVertexLit()` for retro look

### Post-Processing (needs core work)
- `Bloom` - Currently used, may be reduced/removed for retro look
- **NEEDED**: `Dither` operator for ordered dithering
- **NEEDED**: Low-res render target with nearest-neighbor upscale

### Audio
- `AudioIn` operator - FFT analysis for bass/mids/highs

## Geometry Building Pattern (Flat Shading)

For faceted PS1-style geometry, duplicate vertices so each face has its own normals:

```cpp
// Helper: Add a flat-shaded triangle (each face has unique vertices)
void addTriangle(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                 glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 normal) {
    uint32_t base = verts.size();
    verts.push_back({p0, normal, {0, 0}});
    verts.push_back({p1, normal, {1, 0}});
    verts.push_back({p2, normal, {0, 1}});
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

// Helper: Add a flat-shaded quad (two triangles, same normal)
void addQuad(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
             glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 normal) {
    addTriangle(verts, indices, p0, p1, p2, normal);
    addTriangle(verts, indices, p0, p2, p3, normal);
}
```

## Team Color Palettes

```cpp
// FEISAR: Blue/White racing team
const glm::vec3 FEISAR_PRIMARY   = {0.17f, 0.36f, 0.69f}; // #2B5CAF
const glm::vec3 FEISAR_SECONDARY = {1.00f, 1.00f, 1.00f}; // #FFFFFF

// AG-SYS: Yellow/Blue team
const glm::vec3 AGSYS_PRIMARY    = {1.00f, 0.84f, 0.00f}; // #FFD700
const glm::vec3 AGSYS_SECONDARY  = {0.00f, 0.40f, 0.80f}; // #0066CC

// AURICOM: Red/White team
const glm::vec3 AURICOM_PRIMARY  = {0.80f, 0.00f, 0.00f}; // #CC0000
const glm::vec3 AURICOM_SECONDARY= {1.00f, 1.00f, 1.00f}; // #FFFFFF
```

## Required Core Features (See SPEC.md for details)

1. **Vertex-Lit Rendering** - Simple N·L diffuse, optional quantization
2. **Flat Shading Mode** - Per-face normals for faceted look
3. **Dithering Post-Process** - Bayer 4×4 ordered dithering
4. **Low-Res Render** - 480p/360p with nearest-neighbor upscale

## Conventions

- Use `// GOAL:` comments to describe intent
- Use `// === SECTION ===` to mark code sections
- Maintain faceted/low-poly style (no smooth normals)
- Keep triangle count in 400-600 range total
- Update SPEC.md checkboxes when completing tasks
