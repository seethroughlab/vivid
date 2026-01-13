# Tropical God Rays

Radial blur god rays with procedural palm fronds, rendered through Render3D for unified shadows.
Inspired by ISLANDS: Non-Places aesthetic.

## Visual Style

- **Monochromatic teal/cyan palette** - unified color across silhouettes and light
- **Dark silhouettes** - geometry rendered as near-black shapes against bright background
- **Glowing emissive orb** - bright cyan sphere adds ISLANDS aesthetic element
- **Visible light shafts** - radial blur god rays streaming from light source
- **Palm frond animation** - wind-animated procedural foliage

## Operators Used

- **SceneComposer** - Ground, trunk (Cylinder), orb (Sphere)
- **FoliageMesh** - Procedural palm fronds with wind animation
- **CameraOperator** - Low angle looking up through foliage
- **SpotLight** - Overhead light for god rays origin
- **Render3D** - Unified rendering with shadows
- **GodRays** - Radial blur post-processing effect

## Key Concepts

### FoliageMesh + Render3D Architecture

```cpp
// Procedural palm fronds
auto& fronds = chain.add<FoliageMesh>("fronds");
fronds.setPlantType(FoliageMesh::PlantType::PalmFrond);
fronds.baseHeight = TRUNK_HEIGHT;  // Position at palm top
fronds.windStrength = 0.4f;
fronds.castShadow = true;

// Unified rendering
auto& render = chain.add<Render3D>("render3d");
render.setInput(&scene);
render.addProceduralMesh(&fronds);  // Fronds in shadow map!
```

### Radial Blur God Rays

```cpp
auto& godrays = chain.add<GodRays>("godrays");
godrays.setInput(&render);
godrays.setCameraInput(&camera);
godrays.setLightInput(&light);

// Ray parameters
godrays.exposure = 0.25f;     // Overall brightness
godrays.decay = 0.99f;        // Falloff per sample
godrays.density = 1.2f;       // Sample spacing
godrays.weight = 0.5f;        // Per-sample weight
godrays.samples = 120;        // Number of blur samples
godrays.threshold = 0.8f;     // Brightness threshold
godrays.blend = 1.0f;         // Final blend amount
```

### Emissive Light Source

The bright orb serves as the god ray source:
```cpp
// HDR bright cyan orb
auto& orb = scene.add<Sphere>("orb",
    glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)),
    glm::vec4(3.0f, 4.0f, 5.0f, 1.0f));  // HDR values > 1.0
orb.radius(2.5f);
```

## Color Palette

| Element | RGB | Description |
|---------|-----|-------------|
| Background | (0.01, 0.015, 0.02) | Near-black |
| Silhouettes | (0.01, 0.02, 0.025) | Almost black |
| Emissive orb | (3.0, 4.0, 5.0) | HDR bright cyan |
| Spotlight | (0.6, 0.9, 1.0) | Cyan-white |

## Controls

Press **TAB** to show/hide the control panel:

- **Palm Fronds**: count, stem length/curve, leaflet geometry
- **Wind Animation**: strength, speed
- **God Rays**: exposure, decay, density, weight, samples, threshold, blend
- **Spotlight**: intensity, spot angle, spot blend
- **Camera**: auto-orbit toggle, orbit speed

## Technical Notes

### Why Radial Blur?

GodRays uses screen-space radial blur toward the light's screen position:
1. Sample pixels along rays from each pixel toward light
2. Accumulate bright pixels (above threshold)
3. Apply decay for natural falloff
4. Blend additively over scene

### Wind Animation

FoliageMesh wind is applied by Render3D's procedural wind shader:
- UV.y (0-1) controls height factor for displacement
- Multi-frequency sine waves for natural motion
- Per-instance phase offset for variety

### Palm Frond Structure

Each frond is built from:
1. **Curved Stem**: Quadratic curve with configurable droop
2. **Tapered Leaflets**: Triangular pinnae branching alternately
3. **Instance Data**: Transform, scale variation, phase offset
