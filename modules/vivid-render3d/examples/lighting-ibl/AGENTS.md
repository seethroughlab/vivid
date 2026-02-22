# Lighting & IBL Example

Demonstrates multiple light types and image-based lighting for realistic 3D scenes.

## Operators Demonstrated

- **PointLight** - Omnidirectional light radiating from a point with distance falloff
- **SpotLight** - Cone-shaped light with direction, angle, and soft edges
- **IBLEnvironment** - Image-based lighting for environment reflections

## Key Concepts

### PointLight
Emits light in all directions from a position, with distance-based falloff:

```cpp
auto& light = chain.add<PointLight>("warm");
light.position(3.0f, 2.0f, 0.0f);
light.color(1.0f, 0.7f, 0.3f);    // Warm orange
light.intensity = 8.0f;            // Brightness
light.range = 15.0f;               // Falloff distance
```

### SpotLight
Cone-shaped light from a position with direction and angle control:

```cpp
auto& spot = chain.add<SpotLight>("spot");
spot.position(0.0f, 6.0f, 0.0f);
spot.direction(0.0f, -1.0f, 0.0f);  // Point downward
spot.color(1.0f, 1.0f, 0.95f);
spot.intensity = 12.0f;
spot.range = 20.0f;
spot.spotAngle = 35.0f;              // Cone angle in degrees
spot.spotBlend = 0.4f;               // Soft edge (0=hard, 1=very soft)
```

### IBL Environment
Provides ambient environment lighting and reflections from a cubemap:

```cpp
// Procedural sky (no file needed)
auto& ibl = chain.add<IBLEnvironment>("ibl");
ibl.setUseDefault();

// Or from HDR file
ibl.setHdrFile("assets/environment.hdr");

// Connect to Render3D
render.setIbl(true);
render.setEnvironmentInput(&ibl);
render.setShowSkybox(true);  // Optional: show as background
```

### Connecting Lights to Render3D
```cpp
auto& render = chain.add<Render3D>("render");
render.setLightInput(&primaryLight);   // Primary light
render.addLight(&light2);             // Additional lights
render.addLight(&light3);
render.setAmbient(0.15f);            // Ambient fill level
```

## Related Operators

- **DirectionalLight** - Parallel rays (sunlight), no position
- **Render3D** - Main 3D rendering pipeline
- **CameraOperator** - Camera positioning and orbit
