# Grass Field

GPU-instanced grass with wind animation, rendered through Render3D for unified shadows.

## Operators Used

- **GrassMesh** - Procedural grass blade mesh generation (no rendering)
- **Render3D** - Unified rendering with shadow support
- **CameraOperator** - 3D camera
- **DirectionalLight** - Sun and sky fill lighting
- **Bloom** - Dreamy post-processing

## Key Concepts

### New Architecture (GrassMesh + Render3D)

```cpp
auto& camera = chain.add<CameraOperator>("camera");
auto& sun = chain.add<DirectionalLight>("sun");

// Mesh generation only - no rendering
auto& grass = chain.add<GrassMesh>("grass");
grass.fieldWidth = 15.0f;
grass.fieldDepth = 15.0f;
grass.bladeCount = 10000;
grass.castShadow = true;

// Unified rendering with shadows
auto& render = chain.add<Render3D>("render3d");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&sun);
render.addProceduralMesh(&grass);  // Grass participates in shadow map!
```

### Field Parameters
```cpp
// Field dimensions
grass.fieldWidth = 15.0f;    // X extent
grass.fieldDepth = 15.0f;    // Z extent
grass.bladeCount = 10000;    // Total blades
```

### Blade Parameters
```cpp
grass.bladeHeight = 0.4f;       // Average blade height
grass.bladeWidth = 0.03f;       // Blade width at base
grass.heightVariation = 0.3f;   // Random height variation (0-0.5)
```

### Wind Parameters
```cpp
// Displacement amount (0 = no wind, 2 = strong gusts)
grass.windStrength = 0.5f;

// Animation speed (1 = normal, 5 = very fast)
grass.windSpeed = 1.0f;
```

### Color Gradient
Grass blades interpolate from base color (root) to tip color:
```cpp
grass.baseColor[0] = 0.15f;  // Dark green
grass.baseColor[1] = 0.35f;
grass.baseColor[2] = 0.08f;

grass.tipColor[0] = 0.3f;    // Lighter green
grass.tipColor[1] = 0.55f;
grass.tipColor[2] = 0.15f;
```

## Benefits of New Architecture

| Feature | GrassField (old) | GrassMesh + Render3D |
|---------|-----------------|----------------------|
| Shadow casting | No | Yes |
| Shadow receiving | No | Yes |
| Depth testing with scene | Composite required | Automatic |
| Lighting | Duplicate code | Unified |

## How It Works

1. **GrassMesh**: Generates blade mesh + instance transforms
2. **Render3D**: Uploads mesh to GPU, renders with wind shader
3. **Shadow Pass**: Grass included in shadow map
4. **Main Pass**: Grass rendered with lighting and shadows

## Performance

- **10,000 blades** - 60fps on most GPUs
- **50,000 blades** - May need lower resolution
- Wind animation is entirely GPU-based (no CPU per-frame cost)
- Unified render pass is more efficient than compositing

## Controls

- **TAB** - Toggle ImGui panel
- **Wind sliders** - Adjust animation
- **Field sliders** - Size and density
- **Blade sliders** - Height and width
- **Color pickers** - Base and tip colors
- **Camera controls** - Orbit settings

## Combining with FoliageMesh

```cpp
// Ground layer: grass
auto& grass = chain.add<GrassMesh>("grass");
grass.bladeCount = 10000;

// Middle layer: ferns
auto& ferns = chain.add<FoliageMesh>("ferns");
ferns.setPlantType(FoliageMesh::PlantType::Fern);
ferns.frondCount = 200;

// Unified rendering - both cast/receive shadows
auto& render = chain.add<Render3D>("render3d");
render.addProceduralMesh(&grass);
render.addProceduralMesh(&ferns);
```

## Migration from GrassField

```cpp
// OLD (deprecated)
auto& grass = chain.add<GrassField>("grass");
grass.setCameraInput(&camera);
grass.setLightInput(&sun);

// NEW
auto& grass = chain.add<GrassMesh>("grass");
render.addProceduralMesh(&grass);
```

## Visual Tips

- Lower wind speed for calm meadow
- Higher strength + speed for stormy weather
- Warmer tip color for sun-bleached look
- Cooler base color for shaded roots
- Add Fog operator for depth atmosphere
