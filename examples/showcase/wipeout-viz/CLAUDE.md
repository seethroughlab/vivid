# Wipeout 2029 Showcase

A flagship demonstration of Vivid's capabilities through a procedural anti-gravity racing craft generator, inspired by the iconic Wipeout 2097 PS1 aesthetic.

## Vision

Create a visually stunning, fully procedural anti-gravity racing craft that demonstrates Vivid's core strengths:

- **Procedural Mesh Generation** - Complex multi-part geometry built with CSG boolean operations
- **Procedural Textures** - Canvas-based livery generation with team colors, racing numbers, sponsor decals
- **Retro Rendering Pipeline** - PS1-era visual style: textured PBR, flat normals, dithering, scanlines, CRT effects
- **Audio Reactivity** - Engine glow, hover oscillation, visual effects driven by audio analysis
- **Real-time Interactivity** - Hot-reload, team switching, wireframe debug mode

The aesthetic goal is NOT photorealism. It's a deliberate recreation of late-90s low-poly art with modern procedural techniques. The craft should feel **fast**, **graphic**, and **slightly imperfect** - evoking the PlayStation 1 era without directly imitating it.

**IMPORTANT: PS1 Aesthetic Clarification**

The retro PS1 look does NOT come from absence of textures. Original Wipeout crafts had rich, detailed textures including:
- Team liveries with sponsor decals
- Panel lines and surface detail
- Grime, weathering, and wear marks
- Bold racing numbers and graphics

The authentic PS1 aesthetic comes from:
1. **Rich textures** - Full team liveries with sponsors, panel lines, weathering
2. **Post-processing pipeline** - Downsample (low resolution), dithering, CRT effects
3. **Low-poly geometry** - Flat normals for faceted look
4. **Affine texture mapping** (optional) - UV wobble/warping

VertexLit (solid colors, no textures) is kept as a **Debug Mode** for development, NOT as the PS1 style.

---

## Craft Geometry Redesign (Phase 6) ✅ COMPLETE

**Goal:** Complete rewrite of craft geometry to match Wipeout reference images - a unified wedge/arrowhead shape.

**Solution:** Rebuilt main body using CUSTOM VERTICES (addVertex/addTriangle/addQuad) instead of assembled primitive boxes. This creates a true unified arrowhead planform.

### Key Insight: UNIBODY DESIGN

**The craft is ONE unified shape, not parts assembled together.**

### Detailed Reference Analysis (alexandre-etendard-03.gif, 05.gif)

**Top-down view (ref_05):**
```
              *                    <- very long needle nose (~40% of length)
             /|\
            / | \
           /  |  \
          /   |   \
         /   [C]   \               <- cockpit in middle
        /     |     \
       /      |      \
      /    [=====]    \            <- wide rear section
     /   [N]     [N]   \           <- large corner nacelles
    /___/           \___\          <- W-SHAPED trailing edge
```

**Front view (ref_03 left):**
- Flat, low profile
- Wings angle down slightly
- Central cockpit bubble
- Sharp nose point

**Rear view (ref_03 right):**
- WIDE with two large nacelles at corners
- W-shaped trailing edge (center sweeps FORWARD)
- Central spine between nacelles

### Critical Shape Features

1. **VERY LONG NOSE**: ~40% of total length, needle-like taper
2. **W-SHAPED TRAILING EDGE**: NOT straight - sweeps forward between nacelles
3. **LARGE CORNER NACELLES**: Prominent, at the outermost rear corners
4. **CONTINUOUS SWEEP**: Body widens smoothly from nose to max width at rear
5. **WIDEST AT REAR**: Maximum width is at the trailing edge, not middle

### Geometry Proportions

- Length: ~3.0 units (nose to tail)
- Max width: ~1.4 units (at rear trailing edge)
- Height: ~0.12 units (very flat, blade-like)
- Nose section: ~1.2 units (40% of length)
- Wing/body section: ~1.8 units

### New Class Structure

```cpp
class Craft {
    DeltaBody body;           // Main wide triangular body with integrated nacelles
    NoseNeedle nose;          // Long front needle (~0.7 units)
    LowCockpit cockpit;       // Recessed canopy in central spine
    VerticalFin fin;          // Small rear stabilizer
    HoverPad x4;              // Corner hover emitters
    EngineExhaust x2;         // Glow geometry for emissive material
};
```

### Visual Target (Top View)

```
            /\
           /  \
          /    \
         / [C]  \        [C] = cockpit
        /   ||   \
       /    ||    \
      /=====||=====\     ===== = integrated nacelles
     /______||______\
    <-------||-------->
         needle
```

### Success Criteria ✅

1. ✅ Unified arrowhead planform (two wedges laid flat)
2. ✅ Craft is LONGER than it is wide (~2.7:1 ratio)
3. ✅ Engine nacelles integrated at rear
4. ✅ Low profile (height 0.10)
5. ✅ Long tapered nose
6. ✅ Angular, faceted surfaces for PS1 aesthetic
7. ✅ Surface details: tubes, pipes, vents
8. ✅ Unibody design - no separate wing pieces

---

## Reference Material

### Design Language

The Wipeout aesthetic is defined by:

1. **Retro-Futuristic Minimalism** - Clean, aerodynamic forms built from simplified polygons
2. **Bold Graphic Silhouettes** - Strong top-view shapes, wide side-pods, slim fuselages
3. **Flat Color Blocking** - Large areas of solid team colors, high contrast
4. **Racing Decals** - Team numbers, sponsor logos, hazard stripes, panel lines
5. **Imperfect Rendering** - Low resolution, dithering, texture wobble, CRT effects (NOT absence of textures)

### Visual References

Located in `vivid_v1/examples/wipeout-vehicle/reference/`:
- Alexandre Etendard concept art (8 animated GIFs showing detailed craft designs)
- Tumblr reference images of original Wipeout craft
- Wireframe breakdowns showing geometry construction
- Bloom/glow reference for engine effects

### Team Color Palettes

| Team | Primary | Secondary | Accent |
|------|---------|-----------|--------|
| FEISAR | #2B5CAF Blue | #FFFFFF White | #FFD700 Gold |
| AG-SYS | #FFD700 Yellow | #0066CC Blue | #FFFFFF White |
| AURICOM | #CC0000 Red | #FFFFFF White | #333333 Dark Gray |
| QIREX | #6B0099 Purple | #00CCCC Cyan | #1A1A1A Black |
| PIRANHA | #333333 Charcoal | #FF6600 Orange | #CCCCCC Silver |

---

## Implementation Status

### Core Operators (ALREADY IMPLEMENTED)

All the core rendering operators exist in `addons/vivid-effects-2d/`:

| Operator | File | Features |
|----------|------|----------|
| `Dither` | `dither.h` | Bayer 2x2/4x4/8x8, configurable levels (2-256), strength blend |
| `Downsample` | `downsample.h` | Resolution control, FilterMode::Nearest for retro pixelation |
| `Scanlines` | `scanlines.h` | Spacing, thickness, intensity |
| `CRTEffect` | `crt_effect.h` | Curvature, vignette, scanlines, bloom, chromatic aberration |
| `Canvas` | `canvas.h` | Rects, circles, lines, triangles, TTF text rendering |

Rendering features in `addons/vivid-render3d/`:

| Feature | Status | Notes |
|---------|--------|-------|
| VertexLit shading | Done | Mode 3 in `renderer.cpp`, simple N·L diffuse |
| Flat shading | Done | Mode 1, per-face normals |
| Toon shading | Done | Mode 4, quantized steps |
| PBR Materials | Done | Full metallic-roughness workflow |
| Boolean/CSG | Done | `boolean.h`, union/subtract/intersect |
| MeshBuilder | Done | `mesh_builder.h`, procedural geometry |

**Working Example:** `examples/2d-effects/retro-crt/chain.cpp` demonstrates the full retro pipeline:
```cpp
downsample.input(&hsv).resolution(320, 240).filter(FilterMode::Nearest);
dither.input(&downsample).pattern(DitherPattern::Bayer4x4).levels(16);
scanlines.input(&dither).spacing(3).thickness(0.4f).intensity(0.25f);
crt.input(&scanlines).curvature(0.15f).vignette(0.4f).bloom(0.15f);
```

### Reference Material

The `vivid_v1/examples/wipeout-vehicle/` directory contains early experiments and reference assets:
- Reference images: `reference/*.jpg`, `reference/*.gif` (Alexandre Etendard concept art)
- Fonts: `fonts/` (racing-style TTF files)
- Grime textures: `textures/grime/*.jpg`
- Environment HDR: `environment.hdr`

These assets can be reused, but the code should be written fresh using Vivid v2 patterns.

### What Needs To Be Built

1. **Procedural Craft Geometry**
   - Multi-part mesh: fuselage, cockpit, side pods, engines, fins, wings
   - Use MeshBuilder or CSG Boolean operations
   - Flat normals for faceted PS1 look
   - UV mapping for livery texture

2. **Canvas-Based Livery System**
   - Use Canvas operator to draw team liveries
   - Color blocking, racing stripes, team numbers
   - TTF font rendering for numbers/text
   - 5 team palettes (FEISAR, AG-SYS, AURICOM, QIREX, PIRANHA)

3. **Retro Post-Processing Chain**
   - Downsample → Dither → CRTEffect
   - VertexLit shading mode on 3D render

4. **Interactivity**
   - Orbit camera
   - Team switching (1-5 keys)
   - Audio reactivity (optional)

---

## Implementation Phases

### Phase 1: Minimal Craft

**Goal:** Get a basic craft rendering with retro effects

- [ ] Create `chain.cpp` with basic v2 structure
- [ ] Build simple craft geometry using MeshBuilder (fuselage + pods)
- [ ] Apply solid color material
- [ ] Add Downsample → Dither → CRTEffect chain
- [ ] Verify retro pipeline works

### Phase 2: Full Geometry

**Goal:** Complete multi-part craft with proper detail

- [ ] Fuselage with tapered profile and spine ridge
- [ ] Cockpit canopy (angular, faceted)
- [ ] Side pods with intake scoops
- [ ] Engine nacelles (hexagonal, with internal rings)
- [ ] Rear wing with endplates
- [ ] Vertical stabilizer fins
- [ ] Front canards
- [ ] All parts use flat normals for PS1 look
- [ ] UV mapping for livery texture

### Phase 3: Livery System ✅ COMPLETE

**Goal:** Procedural team liveries using Canvas

- [x] Canvas operator generates 1024x1024 livery texture
- [x] Team color palettes (5 teams: FEISAR, AG-SYS, AURICOM, QIREX, PIRANHA)
- [x] Color blocking for body regions
- [x] Racing stripes (horizontal/diagonal)
- [x] Team numbers with TTF font (space age.ttf)
- [x] Panel line details
- [x] Sponsor logos (geometric representations)
- [x] Weathering effects (grime, scratches, exhaust stains)
- [x] Apply livery texture to craft mesh via TexturedMaterial
- [x] Grime texture overlay for realistic weathering
- [x] Team switching with 1-5 keys

### Phase 4: Rendering & Shading

**Goal:** Authentic PS1-era visual style

- [x] PBR textured shading as default (rich textures = PS1 style)
- [x] Emissive engine glow
- [x] Downsample to 480x270 with nearest-neighbor
- [x] Dither with Bayer4x4 pattern
- [x] CRTEffect (scanlines, vignette, bloom, chromatic)
- [x] Debug mode toggle (V key) - shows untextured wireframe-style view

### Phase 5: Interactivity & Polish

**Goal:** Complete showcase experience

- [x] Orbit camera with mouse drag
- [x] Hover animation (sine wave oscillation)
- [x] Audio reactivity (bass → hover, mids → engine glow)
- [x] Background gradient or grid floor (Tron-style grid)
- [x] UI overlay (team name, mode indicator)

---

## Technical Requirements

### Core Runtime Status

All required operators are already implemented:

| Feature | Status | Location |
|---------|--------|----------|
| Dither Operator | **Done** | `addons/vivid-effects-2d/src/dither.cpp` |
| Downsample Operator | **Done** | `addons/vivid-effects-2d/src/downsample.cpp` |
| CRTEffect Operator | **Done** | `addons/vivid-effects-2d/src/crt_effect.cpp` |
| Scanlines Operator | **Done** | `addons/vivid-effects-2d/src/scanlines.cpp` |
| Canvas (TTF text) | **Done** | `addons/vivid-effects-2d/src/canvas.cpp` |
| VertexLit shading | **Done** | `addons/vivid-render3d/src/renderer.cpp` (mode 3) |
| Boolean/CSG | **Done** | `addons/vivid-render3d/src/boolean.cpp` |
| FilterMode::Nearest | **Done** | Used in Downsample |

**Only missing (optional):** Affine texture wobble shader (PS1-style UV distortion)

### Rendering Pipeline Order

```
┌──────────────┐
│ 3D Scene     │  Render3D with Vertex-Lit material
└──────┬───────┘
       ↓
┌──────────────┐
│ Downsample   │  480x270, FilterMode::Nearest
└──────┬───────┘
       ↓
┌──────────────┐
│ Dither       │  Bayer4x4, 32 levels
└──────┬───────┘
       ↓
┌──────────────┐
│ CRTEffect    │  Scanlines, vignette, chromatic
└──────┬───────┘
       ↓
┌──────────────┐
│ Output       │  Final composite
└──────────────┘
```

---

## Success Criteria

The Wipeout 2029 Showcase will be considered complete when:

1. **Visual Quality** - Craft looks polished and professional, matching reference aesthetic
2. **Performance** - Runs at 60fps on M1 Mac, mid-range Windows PC
3. **Interactivity** - Smooth camera, instant team switching, responsive audio
4. **Educational Value** - Code is readable, well-commented, demonstrates Vivid patterns
5. **Extensibility** - Easy to create new teams, craft variants, livery styles

---

## File Structure

```
examples/wipeout-showcase/
├── chain.cpp              # Main showcase entry point
├── craft_geometry.h       # Procedural mesh generation
├── craft_geometry.cpp
├── livery_generator.h     # Livery texture system
├── livery_generator.cpp
├── retro_effects.h        # Post-processing setup
├── audio_reactive.h       # Audio response utilities
├── SPEC.md                # Detailed specification
├── CLAUDE.md              # AI assistant context
├── assets/
│   ├── fonts/
│   │   ├── racing.ttf     # Team numbers
│   │   └── ui.ttf         # UI text
│   ├── textures/
│   │   ├── grime/         # Weathering overlays
│   │   └── environment/   # HDR sky maps
│   └── reference/         # Visual references
└── README.md              # User-facing documentation
```

---

## References

- `vivid_v1/examples/wipeout-vehicle/` - Existing prototype implementation
- `vivid_v1/examples/wipeout-vehicle/SPEC.md` - Detailed geometry/texture specification
- `vivid_v1/examples/wipeout-vehicle/GOAL.txt` - Aesthetic description
- `ROADMAP.md` - Core feature requirements, API examples
- Alexandre Etendard's Wipeout concept art (reference images)
