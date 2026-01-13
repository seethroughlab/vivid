# Tree Forest

L-System procedural trees with billboard leaf clusters, rendered through Render3D for unified shadows.

## Operators Used

- **TreeMesh** - L-System procedural tree mesh generation (no rendering)
- **Render3D** - Unified rendering with shadow support
- **CameraOperator** - 3D camera
- **DirectionalLight** - Sun and sky fill lighting
- **Bloom** - Dreamy post-processing

## Key Concepts

### TreeMesh + Render3D Architecture

```cpp
auto& camera = chain.add<CameraOperator>("camera");
auto& sun = chain.add<DirectionalLight>("sun");

// Mesh generation only - no rendering
auto& trees = chain.add<TreeMesh>("trees");
trees.setTreeType(TreeMesh::TreeType::Deciduous);
trees.treeCount = 5;
trees.castShadow = true;

// Unified rendering with shadows
auto& render = chain.add<Render3D>("render3d");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&sun);
render.addProceduralMesh(&trees);  // Trees in shadow map!
```

### Tree Type Presets

```cpp
// Spreading crown with multiple main branches
trees.setTreeType(TreeMesh::TreeType::Deciduous);

// Pyramidal shape with horizontal branch layers
trees.setTreeType(TreeMesh::TreeType::Conifer);

// Tall trunk with frond crown at top
trees.setTreeType(TreeMesh::TreeType::Palm);

// Drooping branches with cascading foliage
trees.setTreeType(TreeMesh::TreeType::Willow);

// Dense shrub-like structure
trees.setTreeType(TreeMesh::TreeType::Bushy);

// Use current parameter values
trees.setTreeType(TreeMesh::TreeType::Custom);
```

### L-System Parameters

```cpp
// Tree structure
trees.trunkHeight = 2.0f;        // Initial trunk segment height
trees.trunkRadius = 0.12f;       // Trunk radius at base
trees.lsystemIterations = 4;     // More iterations = more branches
trees.branchAngle = 25.0f;       // Branching angle in degrees
trees.lengthScale = 0.9f;        // Length reduction per level
trees.radiusScale = 0.7f;        // Radius reduction per level
```

### Billboard Leaf Clusters

```cpp
// Leaves are billboard quads that face the camera
trees.leafDensity = 8;           // Billboards per cluster
trees.leafSize = 0.25f;          // Size of each billboard
trees.clusterRadius = 0.35f;     // Cluster spread radius
```

### Wind Animation

```cpp
// Branch sway
trees.windStrength = 0.2f;       // Overall wind strength
trees.windSpeed = 0.8f;          // Animation speed

// Leaf flutter (additional high-frequency motion)
trees.leafFlutter = 0.3f;        // Flutter intensity
```

### Leaf Textures (Alpha-Masked Billboards)

```cpp
// Load leaf texture for natural-looking foliage
trees.setLeafTexture("assets/textures/leaf_maple.png");

// Texture should be PNG with alpha channel:
// - Alpha = 1 for leaf pixels
// - Alpha = 0 for transparent pixels
// Fragments with alpha < 0.5 are discarded

// Clear texture (revert to solid color)
trees.clearLeafTexture();

// Check if texture loaded successfully
if (trees.hasLeafTexture()) {
    // Texture is active
}
```

## How It Works

### L-System Grammar

Trees are generated using L-System grammar expansion:

| Symbol | Meaning |
|--------|---------|
| `F` | Draw branch segment forward |
| `+/-` | Yaw rotation (around Y) |
| `^/&` | Pitch rotation (tilt up/down) |
| `/\` | Roll rotation |
| `[/]` | Push/pop state (branch point) |
| `L` | Leaf cluster (billboard) |
| `!` | Decrease radius |

**Example Grammar (Deciduous):**
```
Axiom: FFFA
Rule: A → [&FFA]////[&FFA]///////[&FFA]
```

### Turtle Interpretation

The L-System string is interpreted by a "turtle" that moves through 3D space:

1. Start at origin, facing up (Y+)
2. Process each symbol:
   - `F`: Move forward, generate branch cylinder
   - `[`: Save current state (position, orientation, size)
   - `]`: Restore saved state
   - `L`: Generate billboard cluster at current position
3. Result: Complete tree mesh with branches and leaves

### Billboard Rendering

Leaves use camera-facing billboards:
1. Vertex position is cluster center
2. UV coordinates (0-1, 0-1) define quad corners
3. Shader expands quads using camera right/up vectors
4. `color.w > 0` signals billboard (value = size)

## Preset Comparison

| Preset | Trunk | Iterations | Leaves | Character |
|--------|-------|------------|--------|-----------|
| Deciduous | 2.0m | 4 | Dense clusters | Spreading crown |
| Conifer | 3.0m | 7 | Sparse, layered | Pyramidal |
| Palm | 4.0m | 1 | Large fronds | Tropical |
| Willow | 2.5m | 3 | Cascading | Drooping |
| Bushy | 0.8m | 3 | Dense all-over | Shrub |

## Performance

- **5 trees** - 60fps (default)
- **20 trees** - 60fps on most GPUs
- **30+ trees** - May need fewer L-System iterations
- Wind animation is entirely GPU-based
- Billboard expansion happens in vertex shader

## Controls

Press **TAB** to show/hide the control panel:

- **Tree Type**: Switch between presets
- **Tree Structure**: trunk height/radius, L-System iterations, angles, scales
- **Leaf Clusters**: density, size, radius
- **Wind Animation**: strength, speed, flutter
- **Forest**: tree count, field dimensions
- **Color**: trunk base/tip colors, leaf color
- **Camera**: auto-orbit toggle, orbit parameters

## Combining with Other Vegetation

```cpp
// Ground layer: grass
auto& grass = chain.add<GrassMesh>("grass");
grass.bladeCount = 10000;

// Understory: ferns
auto& ferns = chain.add<FoliageMesh>("ferns");
ferns.setPlantType(FoliageMesh::PlantType::Fern);
ferns.frondCount = 200;

// Canopy: trees
auto& trees = chain.add<TreeMesh>("trees");
trees.setTreeType(TreeMesh::TreeType::Deciduous);
trees.treeCount = 5;

// Unified rendering - all cast/receive shadows
auto& render = chain.add<Render3D>("render3d");
render.addProceduralMesh(&grass);
render.addProceduralMesh(&ferns);
render.addProceduralMesh(&trees);
```

## Visual Tips

- Lower iterations for performance or stylized look
- Higher leaf density for fuller canopy
- Increase trunk radius for older trees
- Warmer leaf colors for autumn
- Cooler trunk colors for birch/aspen
- Add Fog operator for depth atmosphere
