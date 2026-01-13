# Streetlight Fog

Atmospheric fog effect inspired by ISLANDS: Non-Places.

A solitary streetlight in atmospheric haze. Demonstrates depth-based
fog post-processing with the VolumetricLighting operator.

## Operators Used

- **SceneComposer** - Scene container with ground and streetlight geometry
- **CameraOperator** - Low-angle camera position
- **PointLight** - Warm streetlight at lamp position
- **DirectionalLight** - Dim ambient moon/sky light
- **Render3D** - Base scene render with depth output
- **VolumetricLighting** - Depth-based fog post-process

## Key Concepts

### VolumetricLighting Setup
```cpp
// Render3D MUST have depth output enabled
auto& render = chain.add<Render3D>("render");
render.setDepthOutput(true);  // Required!

// Create volumetric lighting post-process
auto& volumetric = chain.add<VolumetricLighting>("volumetric");
volumetric.input(&render);           // Connect to Render3D
volumetric.lightInput(&streetlight); // Light source (for future ray marching)
volumetric.cameraInput(&camera);     // Camera reference

// Fog parameters
volumetric.density = 0.015f;     // Fog density (affects falloff)
volumetric.intensity = 0.4f;     // Fog blend strength
volumetric.fogColor[0] = 0.015f; // Dark blue-gray fog
volumetric.fogColor[1] = 0.02f;
volumetric.fogColor[2] = 0.03f;
```

### Fog Color
Sets the color that distant objects fade toward:
```cpp
volumetric.fogColor[0] = 0.015f;  // R - dark
volumetric.fogColor[1] = 0.02f;   // G
volumetric.fogColor[2] = 0.03f;   // B - slightly blue
```

### Density and Intensity
- `density`: Controls how quickly fog builds with distance (Beer-Lambert)
- `intensity`: Controls the blend strength toward fog color

### Moody Atmosphere Tips
- Use very low ambient (0.02-0.05)
- Use near-black clear color
- Strong contrast between lit and unlit areas
- Warm light + cool fog creates depth
- Subtle camera motion adds life

## Visual Style

Inspired by ISLANDS: Non-Places - liminal spaces, isolation, contemplation.
The fog creates atmospheric depth, while the warm streetlight provides
a focal point of comfort in the darkness.

## Future Enhancements

The VolumetricLighting operator is designed for full ray marching-based
god rays. The current implementation provides depth-based fog. Future
versions will add:
- True volumetric light shafts via ray marching
- Henyey-Greenstein phase function for anisotropic scattering
- Shadow-occluded volumetrics

## Controls

No interactive controls - camera sways slowly, light flickers subtly.
