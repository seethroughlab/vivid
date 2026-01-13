# Foliage System Enhancement Plan

Advanced vegetation rendering with textures, seasons, interaction, LOD, and new plant types.

## Status

| Phase | Feature | Status |
|-------|---------|--------|
| 4 | Textured Billboards | **COMPLETE** |
| 5 | Seasonal Variations | Pending |
| 6 | Interactive Vegetation | Pending |
| 7 | LOD System | Pending |
| 8 | Flowers & Berries | Pending |
| 9 | Vines & Climbing Plants | Pending |

## Overview

Six phases building on the existing ProceduralMesh system (GrassMesh, FoliageMesh, TreeMesh):

| Phase | Feature | Complexity | Dependencies |
|-------|---------|------------|--------------|
| 4 | Textured Billboards | Medium | None |
| 5 | Seasonal Variations | Low | Phase 4 (optional) |
| 6 | Interactive Vegetation | Medium | None |
| 7 | LOD System | High | None |
| 8 | Flowers & Berries | Medium | Phase 4 |
| 9 | Vines & Climbing Plants | High | None |

---

## Phase 4: Textured Leaf Billboards

Add texture support to procedural meshes for alpha-masked leaves with natural appearance.

### Current State
- Billboards are solid-colored quads
- `color.w > 0` signals billboard mode (value = size)
- No texture sampling in procedural wind shader

### Goals
- Alpha-masked leaf textures (maple, oak, pine needle clusters)
- Normal-mapped leaves for better lighting
- Multiple leaf texture variants per tree

### Files to Modify

| File | Changes |
|------|---------|
| `include/vivid/render3d/procedural_mesh.h` | Add `getLeafTexture()` virtual method |
| `include/vivid/render3d/tree_mesh.h` | Add texture path parameter, leaf texture variants |
| `src/tree_mesh.cpp` | Load leaf texture, assign UV regions per billboard |
| `src/renderer.cpp` | Extend shader with texture sampler, alpha discard |
| `src/renderer.h` | Add texture view to `ProceduralMeshGPU` struct |

### Shader Changes

```wgsl
// Add to uniforms
@group(1) @binding(1) var leafTexture: texture_2d<f32>;
@group(1) @binding(2) var leafSampler: sampler;

// In fragment shader
if (isBillboard) {
    let texColor = textureSample(leafTexture, leafSampler, uv);
    if (texColor.a < 0.5) { discard; }
    finalColor = texColor.rgb * leafColor;
}
```

### ProceduralMesh Interface Extension

```cpp
// New virtual methods
virtual WGPUTextureView getLeafTextureView() const { return nullptr; }
virtual bool hasLeafTexture() const { return false; }
```

### Assets Needed
- `assets/textures/leaves/maple.png` (256x256, alpha)
- `assets/textures/leaves/oak.png`
- `assets/textures/leaves/birch.png`
- `assets/textures/leaves/pine_cluster.png`
- `assets/textures/leaves/palm_frond.png`

### Example

```cpp
auto& trees = chain.add<TreeMesh>("trees");
trees.setTreeType(TreeMesh::TreeType::Deciduous);
trees.setLeafTexture("assets/textures/leaves/maple.png");
```

---

## Phase 5: Seasonal Color Variations

Time-based or parameter-driven foliage color transitions.

### Goals
- Spring: Light green with occasional blossoms
- Summer: Deep green, full canopy
- Autumn: Orange, red, yellow gradient
- Winter: Bare branches (optional leaf removal)

### Files to Modify

| File | Changes |
|------|---------|
| `include/vivid/render3d/tree_mesh.h` | Add `Season` enum, `setSeason()`, seasonal color presets |
| `include/vivid/render3d/foliage_mesh.h` | Add seasonal colors |
| `include/vivid/render3d/grass_mesh.h` | Add seasonal colors (summer green → autumn yellow) |
| `src/tree_mesh.cpp` | Seasonal preset application |
| `src/renderer.cpp` | Add seasonal blend factor uniform for transitions |

### Season System

```cpp
enum class Season { Spring, Summer, Autumn, Winter, Custom };

struct SeasonColors {
    glm::vec3 leafPrimary;    // Main leaf color
    glm::vec3 leafSecondary;  // Variation color
    float leafDensityMult;    // 0.0 for winter bare
    float blossomChance;      // Spring flowers
};

void TreeMesh::setSeason(Season season);
void TreeMesh::setSeasonBlend(float t);  // 0-1 transition between seasons
```

### Seasonal Color Presets

| Season | Primary | Secondary | Density | Blossoms |
|--------|---------|-----------|---------|----------|
| Spring | (0.5, 0.7, 0.3) | (0.9, 0.6, 0.7) | 0.7 | 0.15 |
| Summer | (0.15, 0.4, 0.1) | (0.2, 0.5, 0.15) | 1.0 | 0.0 |
| Autumn | (0.8, 0.4, 0.1) | (0.9, 0.2, 0.1) | 0.8 | 0.0 |
| Winter | (0.4, 0.35, 0.3) | (0.5, 0.4, 0.35) | 0.0 | 0.0 |

### Example

```cpp
auto& trees = chain.add<TreeMesh>("trees");
trees.setSeason(TreeMesh::Season::Autumn);

// Animated transition
trees.setSeasonBlend(sin(time * 0.1) * 0.5 + 0.5);  // Cycle seasons
```

---

## Phase 6: Interactive Vegetation

Vegetation responds to player/object proximity by bending away.

### Goals
- Grass parts around player position
- Bushes/ferns compress when walked through
- Gradual return to rest position
- Multiple interaction points supported

### Files to Modify

| File | Changes |
|------|---------|
| `include/vivid/render3d/procedural_mesh.h` | Add interaction point interface |
| `include/vivid/render3d/grass_mesh.h` | Add `setInteractionPoints()` |
| `include/vivid/render3d/foliage_mesh.h` | Add interaction support |
| `src/renderer.cpp` | Extend shader with interaction displacement |
| `src/renderer.h` | Add interaction uniform buffer |

### Interaction System

```cpp
struct InteractionPoint {
    glm::vec3 position;
    float radius;        // Effect falloff radius
    float strength;      // Push strength (1.0 = full bend)
};

// ProceduralMesh interface
virtual void setInteractionPoints(const std::vector<InteractionPoint>& points);
virtual const std::vector<InteractionPoint>& getInteractionPoints() const;
```

### Shader Extension

```wgsl
struct InteractionUniforms {
    points: array<vec4f, 8>,  // xyz = position, w = radius
    strengths: array<f32, 8>,
    count: u32,
}

fn applyInteraction(worldPos: vec3f) -> vec3f {
    var displacement = vec3f(0.0);
    for (var i = 0u; i < interaction.count; i++) {
        let point = interaction.points[i].xyz;
        let radius = interaction.points[i].w;
        let dist = distance(worldPos.xz, point.xz);
        if (dist < radius) {
            let factor = 1.0 - (dist / radius);
            let pushDir = normalize(worldPos.xz - point.xz);
            displacement.xz += pushDir * factor * interaction.strengths[i];
        }
    }
    return displacement;
}
```

### Example

```cpp
auto& grass = chain.add<GrassMesh>("grass");
grass.setInteractionPoints({
    {playerPos, 1.5f, 1.0f},      // Player
    {npcPos, 1.0f, 0.8f},         // NPC
});
```

---

## Phase 7: LOD System for Trees

Distance-based detail reduction for large forests.

### Goals
- LOD 0: Full geometry (< 20m)
- LOD 1: Reduced branches, larger leaf clusters (20-50m)
- LOD 2: Billboard impostor (50-100m)
- LOD 3: Point sprite (> 100m)
- Smooth blending between LOD levels

### Files to Create

| File | Description |
|------|-------------|
| `include/vivid/render3d/lod_system.h` | LOD management class |
| `src/lod_system.cpp` | LOD selection, impostor generation |

### Files to Modify

| File | Changes |
|------|---------|
| `include/vivid/render3d/tree_mesh.h` | Add LOD mesh storage, impostor texture |
| `src/tree_mesh.cpp` | Generate multi-LOD meshes |
| `src/renderer.cpp` | LOD selection in render loop |

### LOD System Design

```cpp
struct LODLevel {
    float minDistance;
    float maxDistance;
    Mesh mesh;                    // nullptr for impostor
    WGPUTextureView impostor;     // For billboard LOD
};

class LODSystem {
public:
    void registerMesh(const std::string& id, const std::vector<LODLevel>& levels);
    int selectLOD(const std::string& id, float distance);
    void generateImpostor(TreeMesh& tree, int resolution = 256);
};
```

### Impostor Generation
- Render tree from 8 angles (45° increments)
- Store in texture atlas (2048x256 for 8 views)
- Select view based on camera angle
- Crossfade between adjacent views

### Example

```cpp
auto& trees = chain.add<TreeMesh>("trees");
trees.enableLOD(true);
trees.setLODDistances({20.0f, 50.0f, 100.0f});  // Transition distances
```

---

## Phase 8: Flowers & Berry Details

Small decorative elements on bushes and ground cover.

### Goals
- Procedural flower placement on FoliageMesh
- Berry clusters on bushes
- Wildflowers in grass fields
- Variety through randomization

### Files to Create

| File | Description |
|------|-------------|
| `include/vivid/render3d/flower_mesh.h` | Standalone flower patches |

### Files to Modify

| File | Changes |
|------|---------|
| `include/vivid/render3d/foliage_mesh.h` | Add flower/berry attachment points |
| `src/foliage_mesh.cpp` | Generate flower billboards at branch tips |
| `include/vivid/render3d/grass_mesh.h` | Add wildflower density parameter |
| `src/grass_mesh.cpp` | Scatter flowers among grass blades |

### Flower Types

```cpp
enum class FlowerType {
    Daisy,        // White petals, yellow center
    Rose,         // Multi-petal spiral
    Tulip,        // Cup shape
    Wildflower,   // Simple 5-petal
    Berry,        // Cluster of small spheres
};

struct FlowerParams {
    FlowerType type;
    float density;        // Per meter squared
    float size;           // Billboard size
    glm::vec3 color;      // Primary color
    float colorVariation; // Hue shift range
};
```

### FoliageMesh Extension

```cpp
// Add to FoliageMesh
void setFlowers(const FlowerParams& params);
void setBerries(float density, glm::vec3 color);
```

### Example

```cpp
auto& bushes = chain.add<FoliageMesh>("bushes");
bushes.setPlantType(FoliageMesh::PlantType::Fern);
bushes.setFlowers({FlowerType::Wildflower, 0.5f, 0.08f, {1.0f, 0.9f, 0.2f}, 0.2f});

auto& grass = chain.add<GrassMesh>("grass");
grass.setWildflowerDensity(0.02f);  // 2% of grass positions get flowers
```

---

## Phase 9: Vines & Climbing Plants

Path-following vegetation that clings to surfaces.

### Goals
- Vines that follow arbitrary paths/surfaces
- Ivy that covers walls
- Hanging vines from trees/ceilings
- Root systems visible above ground

### Files to Create

| File | Description |
|------|-------------|
| `include/vivid/render3d/vine_mesh.h` | VineMesh class |
| `src/vine_mesh.cpp` | Path-following geometry generation |

### Vine System Design

```cpp
enum class VineType {
    Ivy,          // Wall-climbing, dense leaves
    Hanging,      // Hangs down, sparse leaves
    Creeper,      // Ground-covering
    Roots,        // Thick, no leaves
};

class VineMesh : public ProceduralMesh {
public:
    void setVineType(VineType type);

    // Path definition
    void setPath(const std::vector<glm::vec3>& points);
    void setPathFromSpline(const Spline& spline);
    void attachToSurface(const Mesh& surface, glm::vec3 startPoint);

    // Parameters
    Param<float> thickness;
    Param<float> leafDensity;
    Param<float> leafSize;
    Param<float> branchChance;   // Secondary vine spawning
    Param<int> maxBranches;

    // Colors
    float vineColor[3];
    float leafColor[3];
};
```

### Path Following Algorithm

1. Input: Control points or surface mesh
2. Generate smooth spline through points
3. Create tube geometry along spline (tapered)
4. Spawn leaf billboards along length
5. Optionally branch at random intervals
6. For surfaces: use normal for leaf orientation

### Example

```cpp
auto& vines = chain.add<VineMesh>("vines");
vines.setVineType(VineMesh::VineType::Ivy);
vines.setPath({
    {0, 0, 0}, {0, 2, 0.5}, {0.5, 4, 1}, {1, 6, 0.5}
});
vines.thickness = 0.05f;
vines.leafDensity = 20;
```

---

## Implementation Priority

**Recommended order:**

1. **Phase 4 (Textures)** - Foundation for natural appearance
2. **Phase 5 (Seasons)** - Quick win, builds on existing color system
3. **Phase 8 (Flowers)** - Visual interest, moderate complexity
4. **Phase 6 (Interaction)** - Gameplay integration
5. **Phase 9 (Vines)** - New geometry type
6. **Phase 7 (LOD)** - Performance optimization (only if needed)

---

## Verification

### Phase 4 Test
```bash
./build/bin/vivid modules/vivid-render3d/examples/tree-forest
# Check: Leaves have natural shape with alpha edges
```

### Phase 5 Test
```cpp
// In update(): cycle through seasons
float t = fmod(ctx.time() * 0.1f, 4.0f);
trees.setSeason(static_cast<TreeMesh::Season>(int(t)));
```

### Phase 6 Test
```cpp
// Move mouse to push grass
auto mouseWorld = camera.screenToWorld(ctx.mouseX(), ctx.mouseY(), 0.0f);
grass.setInteractionPoints({{mouseWorld, 2.0f, 1.0f}});
```

### Phase 7 Test
```cpp
// Verify LOD transitions at distance
camera.position(0, 5, 150);  // Should see impostors
camera.position(0, 5, 10);   // Should see full detail
```

### Phase 8 Test
```bash
./build/bin/vivid modules/vivid-render3d/examples/flower-meadow
# Check: Flowers scattered naturally, varied colors
```

### Phase 9 Test
```cpp
auto& vines = chain.add<VineMesh>("vines");
vines.setVineType(VineMesh::VineType::Hanging);
vines.setPath({{0,10,0}, {0,5,2}, {0,0,3}});
// Check: Vine hangs naturally with leaves
```
