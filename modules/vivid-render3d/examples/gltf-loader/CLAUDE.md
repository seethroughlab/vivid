# GLTF Loader

Demonstrates loading and rendering 3D models from GLTF/GLB files with PBR and IBL.

## Operators Used

- **GLTFLoader** - Load GLTF/GLB 3D models
- **SceneComposer** - Container for 3D geometry
- **CameraOperator** - Orbital camera with auto-fit
- **DirectionalLight** - Scene lighting
- **IBLEnvironment** - HDR environment for reflections
- **Render3D** - PBR renderer

## Key Concepts

### Loading GLTF Models
```cpp
auto& model = chain.add<GLTFLoader>("model")
    .file("assets/models/robot.glb")
    .loadTextures(true)        // Load embedded textures
    .computeTangents(true)     // Required for normal maps
    .scale(1.0f);              // Scale factor

// Add to scene
auto& scene = SceneComposer::create(chain, "scene");
scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

// Check if loaded
if (model.isLoaded()) {
    Bounds3D bounds = model.bounds();
    float radius = bounds.radius();
    glm::vec3 center = bounds.center();
}
```

### IBL Environment (Image-Based Lighting)
```cpp
auto& ibl = chain.add<IBLEnvironment>("ibl")
    .hdrFile("assets/hdris/studio.hdr");

// Enable in renderer
auto& render = chain.add<Render3D>("render")
    .ibl(true)
    .environmentInput(&ibl)
    .showSkybox(true);  // Show environment as background
```

### PBR Rendering
```cpp
auto& render = chain.add<Render3D>("render")
    .input("scene")
    .cameraInput(&camera)
    .lightInput(&sun)
    .shadingMode(ShadingMode::PBR)  // Physically-based rendering
    .ibl(true)
    .environmentInput(&ibl)
    .metallic(0.0f)     // Default metallic (overridden by textures)
    .roughness(0.5f)    // Default roughness
    .clearColor(Color::fromHex("#1A1A26"));
```

### Camera Auto-Fit
```cpp
void fitCameraToModel(CameraOperator& camera, const Bounds3D& bounds) {
    float radius = bounds.radius();
    float fovRad = glm::radians(camera.fov());
    float distance = radius / std::sin(fovRad * 0.5f);
    distance *= 1.5f;  // Add padding

    camera.orbitCenter(bounds.center());
    camera.distance(distance);
}

// Usage
if (model.isLoaded()) {
    fitCameraToModel(camera, model.bounds());
}
```

### Switching Models at Runtime
```cpp
// Get model paths
std::vector<std::string> models = findModels("assets/models");

// Switch model
model.file(models[newIndex]);
needsFit = true;  // Re-fit camera
```

## GLTF Features Supported

- GLTF 2.0 and GLB binary format
- Embedded and external textures
- PBR metallic-roughness workflow
- Normal maps
- Multiple meshes per file
- Node hierarchy and transforms
- Animations (via separate operator)

## Controls

- **SPACE**: Cycle through models in assets/models/
- **V**: Toggle vsync

Place .glb or .gltf files in `assets/models/` folder.
