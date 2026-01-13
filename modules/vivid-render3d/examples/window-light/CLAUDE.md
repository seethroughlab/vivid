# Window Light

Volumetric lighting with directional light shadows streaming through a window.

Demonstrates shadow-occluded god rays using **DirectionalLight** (parallel sun rays).

## Visual Style

- **Interior scene** - Dark room with window opening
- **Directional sunlight** - Parallel rays creating defined light shafts
- **Dusty atmosphere** - Warm-tinted volumetric fog
- **Shadow occlusion** - Window frame and furniture cast shadows into the light beams
- **PBR materials** - Realistic wood and furniture surfaces

## Operators Used

- **SceneComposer** - Room geometry (walls, floor, furniture)
- **CameraOperator** - Interior view looking toward window
- **DirectionalLight** - Sunlight with shadow casting
- **Render3D** - PBR shading, shadows enabled, depth output
- **VolumetricLighting** - Ray marching with shadow occlusion

## Key Concepts

### Directional Light God Rays
```cpp
auto& sun = chain.add<DirectionalLight>("sun");
sun.direction(0.6f, -0.7f, 0.4f);  // Angled sunlight
sun.color(1.0f, 0.95f, 0.85f);     // Warm sunlight
sun.intensity = 2.5f;
sun.castShadow(true);

auto& render = chain.add<Render3D>("render3d");
render.setShadows(true);
render.setShadowMapResolution(2048);
```

### Shadow-Occluded Volumetrics
```cpp
auto& volumetric = chain.add<VolumetricLighting>("volumetric");
volumetric.input(&render);
volumetric.lightInput(&sun);
volumetric.cameraInput(&camera);

// Dusty atmosphere
volumetric.density = 0.06f;
volumetric.intensity = 1.2f;
volumetric.anisotropy = 0.7f;  // Forward scatter toward camera

// Shadow occlusion - creates light shaft gaps
volumetric.useShadows = true;
volumetric.shadowStrength = 1.0f;
```

### DirectionalLight vs SpotLight for Volumetrics

**DirectionalLight** (this example):
- Creates subtle atmospheric haze filling the space
- Light comes uniformly from one direction (no beam shape)
- Shadow occlusion affects overall fog brightness
- No distinct "god ray shafts" - more of a general atmosphere
- Ideal for: outdoor scenes, fill lighting, atmospheric haze

**SpotLight** (streetlight-fog, tropical-godrays):
- Creates dramatic, visible god ray shafts
- Cone shape defines a clear light volume
- Shadow occlusion creates distinct gaps/bands in the beam
- Ideal for: dramatic god rays, stage lighting, flashlights

**Recommendation**: Use SpotLight for dramatic god ray effects. Use DirectionalLight
for overall atmospheric fog without distinct light shafts.

## Scene Structure

```
Room
├── Floor (dark wood)
├── Back wall (with window opening)
│   ├── Left portion
│   ├── Right portion
│   ├── Top (above window)
│   ├── Bottom (below window)
│   └── Window frame (dividers)
├── Side walls
└── Furniture
    ├── Table with legs
    ├── Vase
    ├── Chair
    └── Standing lamp
```

## Controls

Press **TAB** to show/hide the control panel:

- **Volumetric**: density, intensity, anisotropy, ray steps, fog color
- **Shadow Occlusion**: enable/disable, bias, strength
- **Directional Light**: intensity, direction (xyz), color
- **Camera**: auto-orbit toggle

## Technical Notes

### Light Direction

The direction vector points FROM the light source. For sunlight coming through a window:
- Positive X component: Light coming from the right
- Negative Y component: Light coming from above (angled down)
- Positive Z component: Light coming from behind the window

### Anisotropy

High positive anisotropy (0.7) creates forward scattering - light preferentially
scatters toward the camera when looking toward the light source, making the
god rays more visible from inside the room.

### Shadow Map Coverage

DirectionalLight shadow maps use orthographic projection that must cover the
entire scene. The shadow map resolution (2048) provides good detail for the
window frame and furniture shadows in the volumetric fog.
