# Wipeout 2097 Anti-Gravity Racer Generator

## Vision

Create a procedural anti-gravity racing craft inspired by Wipeout 2097's iconic PS1-era aesthetic. This is NOT a PBR showcase—it's a deliberate recreation of late-90s low-poly art with modern procedural techniques. The craft should feel fast, graphic, and slightly imperfect—evoking the PlayStation 1 era.

**Key Aesthetic Pillars:**
- Low-poly faceted geometry (hard edges, no smooth normals)
- Single 256×256 diffuse texture per craft (no PBR maps)
- Vertex-lit or flat shading (no realistic lighting)
- Color blocking with racing decals, team livery, hazard stripes
- Retro post-processing: dithering, low-res render, affine wobble

---

## Geometry Specification

### Overall Craft Structure (~400-600 triangles)

The craft is built from multiple procedural components that together create an aerodynamic, aggressive silhouette:

```
                    ┌─────────────────────────────────────────────┐
                    │                   TOP VIEW                   │
                    │                                             │
                    │         ╱╲                                  │
                    │        ╱  ╲     ← Nose cone (sharp)         │
                    │       ╱    ╲                                │
                    │      ╱      ╲                               │
                    │     ╱   ▓▓   ╲   ← Cockpit (tinted dome)    │
                    │    ╱    ▓▓    ╲                             │
                    │   ╱            ╲                            │
                    │  ╱──────────────╲  ← Main fuselage          │
                    │ ╱                ╲                          │
                    │╱  ┌──┐      ┌──┐  ╲ ← Side pods             │
                    │   │▓▓│      │▓▓│    ← Intake scoops         │
                    │   └──┘      └──┘                            │
                    │    ◯          ◯    ← Engine nacelles        │
                    │   ╱╲          ╱╲   ← Rear fins              │
                    └─────────────────────────────────────────────┘
```

### Component Breakdown

#### 1. Main Fuselage (80-100 tris)
- Long, wedge-shaped body tapering to sharp nose
- Faceted construction: 6-8 segments along length
- Slightly raised center spine for visual interest
- Panel line grooves at segment boundaries
- UV mapped to show team colors on top, darker tones on underside

#### 2. Cockpit Canopy (30-40 tris)
- Low-profile bubble or angular greenhouse
- Positioned forward of center (1/3 from nose)
- Semi-transparent tinted material (not PBR—just alpha + vertex color)
- Frame rails visible as separate geometry

#### 3. Side Pods / Air Intakes (60-80 tris each, ×2)
- Wide, curved pods flanking the fuselage
- Front-facing scoop openings (dark interior)
- Aerodynamic fairing blending into main body
- These carry prominent team logo decals

#### 4. Engine Nacelles (40-50 tris each, ×2)
- Cylindrical or hexagonal exhausts at rear of side pods
- Visible internal rings/chambers (2-3 nested cylinders)
- Glowing exhaust (vertex color emissive, not PBR emission)
- Beveled/chamfered edges

#### 5. Rear Wing/Spoiler (30-40 tris)
- Wide spanning wing across rear
- Endplates at wing tips
- Subtle swept angle (15-25°)
- Mounting pylons to main body

#### 6. Vertical Stabilizer Fins (20-30 tris each, ×2)
- Swept triangular fins rising from engine pods
- Sharp leading edge, thick trailing edge
- Team accent stripes

#### 7. Front Canards/Winglets (15-20 tris each, ×2)
- Small angular wings near nose
- Provide front-end downforce aesthetic
- Angled downward slightly

#### 8. Underbody (40-60 tris)
- Flat anti-gravity pad area (darker, utilitarian)
- Tech panel details (geometric patterns)
- Hover glow mounting points

### Geometry Construction Rules

1. **Faceted polygons only**: Use flat shading (per-face normals), never smooth normals
2. **Quantized curves**: Any curved section uses 6-8 segments max
3. **X-axis symmetry**: Procedural mirroring ensures balance
4. **Vertex colors**: Bake simple lighting into vertex colors for extra style
5. **No micro-details**: Smallest feature should be 3-4 quads minimum

---

## Texture & Material System

### Texture Atlas (256×256 or 512×512)

A single diffuse texture contains all visual information:

```
┌────────────────────────────────────────────────────────────────┐
│  TEAM COLOR BLOCKS    │  SECONDARY COLOR  │  ACCENT STRIPES   │
│  (Primary body areas) │  (Pods, wings)    │  (Racing stripes) │
├───────────────────────┼───────────────────┼───────────────────┤
│  DECAL: Team Logo     │  DECAL: Numbers   │  DECAL: Sponsor   │
│  (EG-SQL, FEISAR)     │  (01-99)          │  (Arrows, Hazard) │
├───────────────────────┼───────────────────┼───────────────────┤
│  PANEL LINES          │  TECH DETAILS     │  GRIME/WEATHERING │
│  (Dark thin stripes)  │  (Vents, bolts)   │  (Dirt, scratches)│
├───────────────────────┼───────────────────┼───────────────────┤
│  COCKPIT GLASS        │  ENGINE GLOW      │  UNDERBODY DARK   │
│  (Tinted gradient)    │  (Orange/blue)    │  (Tech patterns)  │
└────────────────────────────────────────────────────────────────┘
```

### No PBR Maps

- **Diffuse ONLY**: Single color texture
- No normal maps
- No roughness/metallic maps
- No environment reflections
- Specular is a simple hard white highlight (vertex color or shader)

### Color Palette System

Each team uses a constrained palette (8-12 colors):

```
Team FEISAR:  #2B5CAF (Blue), #FFFFFF (White), #1A1A1A (Black), #FFD700 (Gold accent)
Team AG-SYS:  #FFD700 (Yellow), #0066CC (Blue), #FFFFFF (White), #1A1A1A (Black)
Team AURICOM: #CC0000 (Red), #FFFFFF (White), #333333 (Dark gray)
Team QIREX:   #6B0099 (Purple), #00CCCC (Cyan), #1A1A1A (Black)
Team PIRANHA: #333333 (Charcoal), #FF6600 (Orange), #CCCCCC (Silver)
```

### Procedural Texture Generation

The chain should procedurally generate textures with:
1. **Base color blocks**: Large UV regions filled with team primary/secondary
2. **Racing stripes**: Horizontal or diagonal lines (3-5px wide)
3. **Number decals**: Stenciled team numbers (01-99)
4. **Sponsor logos**: Simple geometric shapes suggesting logos
5. **Panel lines**: 1-2px dark lines at geometry seams
6. **Weathering**: Noise-based dirt accumulation at edges

---

## Shading & Rendering

### Vertex-Lit Shader (REQUIRED CORE FEATURE)

The current PBR pipeline must be supplemented with a retro vertex-lit mode:

```cpp
// Shader pseudo-code for vertex-lit rendering
vec3 vertexLight(vec3 position, vec3 normal) {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);

    // 2-3 step quantization for toon/PS1 look
    diffuse = floor(diffuse * 3.0) / 3.0;

    vec3 ambient = vec3(0.3);
    return ambient + diffuse * vec3(0.7);
}
```

**Core features needed:**
- [ ] `ctx.render3DVertexLit()` - New rendering function
- [ ] Flat shading mode (per-face normals)
- [ ] 2-3 step diffuse quantization option
- [ ] Hard specular highlight (no BRDF)

### Flat Shading Option

Geometry should support both:
- **Flat shading**: Each face has uniform lighting (faceted look)
- **Vertex colors**: Pre-baked lighting multiplied with texture

---

## Post-Processing Effects

### Dithering (REQUIRED CORE FEATURE)

Apply ordered dithering to recreate early GPU limitations:

```
Bayer 4x4 matrix:
[ 0,  8,  2, 10]
[12,  4, 14,  6]
[ 3, 11,  1,  9]
[15,  7, 13,  5] / 16.0
```

- [ ] `Dither` operator with configurable pattern (Bayer, blue-noise)
- [ ] Apply to gradients, shadows, semi-transparent areas
- [ ] Adjustable intensity (0-100%)

### Low-Resolution Render

- [ ] Render at reduced resolution (480p, 360p)
- [ ] Upscale with nearest-neighbor filtering
- [ ] Optional CRT scanline overlay

### Affine Texture Warping (Optional)

Simulate PS1's lack of perspective-correct texturing:
- Subtle UV wobble based on vertex depth
- Most visible on large flat surfaces

### Other Post-FX

- [ ] Vignette (subtle edge darkening)
- [ ] Chromatic aberration (1-2px RGB split)
- [ ] Motion streaks (geometry-based trails, not blur)

---

## Audio Reactivity

### Input Processing
- Bass (20-150 Hz): Hover height oscillation
- Mids (150-2000 Hz): Engine glow intensity
- Highs (2000+ Hz): Particle trail density
- Beat detection: Trigger flash effects

### Visual Responses
- Engine exhaust brightness pulses with audio
- Craft subtle banking/tilting with beat
- Background elements (if any) react to spectrum

---

## Assets To Download

### Textures (Place in `assets/textures/`)

| Asset | Description | Source/Notes |
|-------|-------------|--------------|
| `team_feisar.png` | Blue/white team livery | 256×256, create or source |
| `team_agsys.png` | Yellow/blue team livery | 256×256 |
| `team_auricom.png` | Red/white team livery | 256×256 |
| `decal_numbers.png` | Racing numbers 0-9 | 128×128 sprite sheet |
| `decal_arrows.png` | Directional arrows | 64×64 |
| `decal_hazard.png` | Yellow/black hazard stripes | 64×64 tileable |
| `decal_sponsors.png` | Generic sponsor logos | 256×64 sprite sheet |
| `grime_overlay.png` | Dirt/weathering noise | 256×256 tileable |
| `panel_lines.png` | Tech panel patterns | 128×128 tileable |

### Decal Stencils (Place in `assets/stencils/`)

| Stencil | Description |
|---------|-------------|
| `logo_feisar.png` | FEISAR team logo |
| `logo_agsys.png` | AG-SYS team logo |
| `logo_auricom.png` | AURICOM team logo |
| `text_pilot.png` | Pilot name stencil |
| `stripes_racing.png` | Racing stripe patterns |

### Reference Colors (Hex codes for procedural generation)

```cpp
// Team FEISAR
const glm::vec3 FEISAR_PRIMARY   = {0.17f, 0.36f, 0.69f}; // #2B5CAF
const glm::vec3 FEISAR_SECONDARY = {1.00f, 1.00f, 1.00f}; // #FFFFFF
const glm::vec3 FEISAR_ACCENT    = {1.00f, 0.84f, 0.00f}; // #FFD700

// Team AG-SYS
const glm::vec3 AGSYS_PRIMARY    = {1.00f, 0.84f, 0.00f}; // #FFD700
const glm::vec3 AGSYS_SECONDARY  = {0.00f, 0.40f, 0.80f}; // #0066CC
const glm::vec3 AGSYS_ACCENT     = {1.00f, 1.00f, 1.00f}; // #FFFFFF
```

---

## Procedural Generation Parameters

### Hull Parameters
```cpp
struct HullParams {
    float length = 4.0f;          // Total craft length
    float width = 2.5f;           // Maximum width at pods
    float height = 0.8f;          // Main body height
    float noseTaper = 0.7f;       // How sharp the nose is (0-1)
    float spineHeight = 0.15f;    // Center ridge height
    int lengthSegments = 8;       // Faceting along length
};
```

### Wing Parameters
```cpp
struct WingParams {
    float span = 3.0f;            // Total wingspan
    float chord = 0.6f;           // Wing depth
    float sweep = 25.0f;          // Sweep angle in degrees
    float dihedral = 5.0f;        // Upward angle
    float thickness = 0.08f;      // Wing thickness
};
```

### Engine Parameters
```cpp
struct EngineParams {
    float diameter = 0.5f;        // Exhaust diameter
    float length = 1.2f;          // Nacelle length
    int segments = 6;             // Hexagonal (6) or octagonal (8)
    float glowIntensity = 1.0f;   // Base glow brightness
    glm::vec3 glowColor = {1.0f, 0.4f, 0.1f}; // Orange exhaust
};
```

### Team Selection
```cpp
enum class Team {
    FEISAR,   // Blue/White
    AG_SYS,   // Yellow/Blue
    AURICOM,  // Red/White
    QIREX,    // Purple/Cyan
    PIRANHA   // Black/Orange
};
```

---

## Implementation Phases

### Phase 1: Core Geometry
- [ ] Complex multi-part fuselage builder
- [ ] Parametric wing generator
- [ ] Engine nacelle with internal detail
- [ ] Fin and canard systems
- [ ] Cockpit with frame geometry

### Phase 2: Texture System
- [ ] UV mapping for all components
- [ ] Procedural color block generation
- [ ] Racing stripe placement
- [ ] Number and logo stenciling
- [ ] Panel line overlay

### Phase 3: Retro Shading (REQUIRES CORE CHANGES)
- [ ] Vertex-lit shader in runtime
- [ ] Flat shading mode
- [ ] Diffuse quantization (toon steps)
- [ ] Hard specular highlights

### Phase 4: Post-Processing (REQUIRES CORE CHANGES)
- [ ] Dithering operator
- [ ] Low-res render option
- [ ] Scanline/CRT effect
- [ ] Vignette and chromatic aberration

### Phase 5: Audio & Polish
- [ ] Audio-reactive engine glow
- [ ] Hover animation
- [ ] Camera movement
- [ ] Background environment

---

## Required Core Runtime Features

These features must be added to the Vivid runtime to achieve the full aesthetic:

### High Priority

1. **Vertex-Lit Rendering Pipeline**
   - New `render3DVertexLit()` function
   - Simple N·L diffuse calculation
   - Optional quantization for toon look
   - Vertex color support (pre-baked lighting)

2. **Flat Shading Mode**
   - Per-face normals instead of per-vertex
   - Sharp, faceted appearance
   - Toggle on existing render functions

3. **Dithering Post-Process**
   - Bayer matrix or blue-noise pattern
   - Applies to final image
   - Configurable strength

### Medium Priority

4. **Low-Resolution Render Target**
   - Render to smaller buffer
   - Nearest-neighbor upscale
   - Exposed as operator parameter

5. **Texture Atlas System**
   - Single texture with UV regions
   - Procedural UV coordinate generation
   - Sprite sheet support for decals

### Lower Priority

6. **Affine Texture Wobble**
   - PS1-style texture warping
   - Depth-dependent UV distortion

7. **CRT/Scanline Effect**
   - Horizontal line overlay
   - Optional curvature distortion

---

## File Structure

```
examples/wipeout-vehicle/
├── chain.cpp           # Main procedural generation code
├── SPEC.md             # This specification
├── CLAUDE.md           # AI assistant context
├── GOAL.txt            # Aesthetic reference document
├── reference/          # Visual reference images
│   ├── alexandre-etendard-01.gif
│   ├── ... (8 reference images)
├── assets/
│   ├── textures/       # Team liveries, decals
│   └── stencils/       # Logo stencils
└── build/              # Compiled output (generated)
```
