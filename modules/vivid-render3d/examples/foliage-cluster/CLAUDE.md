# Foliage Cluster

GPU-instanced procedural fronds with wind animation, rendered through Render3D for unified shadows.

## Operators Used

- **FoliageMesh** - Procedural frond mesh generation (no rendering)
- **Render3D** - Unified rendering with shadow support
- **CameraOperator** - 3D camera
- **DirectionalLight** - Sun and sky fill lighting
- **Bloom** - Subtle post-processing

## Key Concepts

### New Architecture (FoliageMesh + Render3D)

```cpp
auto& camera = chain.add<CameraOperator>("camera");
auto& sun = chain.add<DirectionalLight>("sun");

// Mesh generation only - no rendering
auto& foliage = chain.add<FoliageMesh>("foliage");
foliage.setPlantType(FoliageMesh::PlantType::Fern);
foliage.frondCount = 200;
foliage.castShadow = true;

// Unified rendering with shadows
auto& render = chain.add<Render3D>("render3d");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&sun);
render.addProceduralMesh(&foliage);  // Foliage participates in shadow map!
```

### Plant Type Presets

```cpp
// Many leaflets, moderate droop - classic fern look
foliage.setPlantType(FoliageMesh::PlantType::Fern);

// Longer fronds, heavy droop, slower sway
foliage.setPlantType(FoliageMesh::PlantType::PalmFrond);

// Simple tapered blade (no leaflets, like GrassMesh)
foliage.setPlantType(FoliageMesh::PlantType::Grass);

// Use current parameter values
foliage.setPlantType(FoliageMesh::PlantType::Custom);
```

### Frond Geometry Parameters
```cpp
// Stem (rachis) shape
foliage.stemLength = 1.0f;    // Length of the stem
foliage.stemCurve = 0.4f;     // Curvature/droop (0=straight, 1=heavy droop)

// Leaflets (pinnae)
foliage.leafletPairs = 10;    // Number of leaflet pairs along stem
foliage.leafletWidth = 0.08f; // Width at leaflet base
foliage.leafletLength = 0.2f; // Length of leaflets
foliage.leafletAngle = 50.0f; // Angle from stem (degrees)

// Size variation
foliage.sizeVariation = 0.3f; // Random scale variation (0-0.5)
```

### Wind Parameters
```cpp
// Horizontal displacement amount
foliage.windStrength = 0.35f;

// Animation speed
foliage.windSpeed = 1.0f;
```

## Benefits of New Architecture

| Feature | FoliageCluster (old) | FoliageMesh + Render3D |
|---------|---------------------|------------------------|
| Shadow casting | No | Yes |
| Shadow receiving | No | Yes |
| Depth testing with scene | Composite required | Automatic |
| Lighting | Duplicate code | Unified |

## How It Works

1. **FoliageMesh**: Generates procedural frond mesh + instance transforms
2. **Render3D**: Uploads mesh to GPU, renders with wind shader
3. **Shadow Pass**: Fronds included in shadow map
4. **Main Pass**: Fronds rendered with lighting and shadows

## Preset Comparison

| Preset | Stem Length | Curve | Leaflets | Wind | Character |
|--------|-------------|-------|----------|------|-----------|
| Fern | 0.8m | 0.35 | 10 pairs | 0.35 | Classic fern |
| PalmFrond | 1.5m | 0.5 | 12 pairs | 0.25 | Heavy, tropical |
| Grass | 0.5m | 0.2 | 0 | 0.5 | Simple blade |

## Performance

- **150 fronds** - 60fps (default)
- **500 fronds** - 60fps on most GPUs
- Wind animation is entirely GPU-based
- Unified render pass is more efficient than compositing

## Controls

- **TAB** - Toggle ImGui panel
- **Plant Type dropdown** - Switch presets
- **Frond Geometry sliders** - Adjust stem and leaflet shape
- **Wind sliders** - Adjust animation
- **Camera controls** - Orbit settings

## Migration from FoliageCluster

```cpp
// OLD (deprecated)
auto& foliage = chain.add<FoliageCluster>("foliage");
foliage.setCameraInput(&camera);
foliage.setLightInput(&sun);

// NEW
auto& foliage = chain.add<FoliageMesh>("foliage");
render.addProceduralMesh(&foliage);
```
