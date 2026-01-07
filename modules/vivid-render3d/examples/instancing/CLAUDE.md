# Instancing

Demonstrates GPU-instanced rendering of thousands of objects with PBR materials.

## Operators Used

- **InstancedRender3D** - GPU-instanced rendering
- **Sphere** - Base mesh geometry
- **TexturedMaterial** - PBR material with textures
- **CameraOperator** - First-person camera
- **DirectionalLight** - Scene lighting
- **Noise** - Procedural star background
- **Composite** - Layer asteroids over stars

## Key Concepts

### Basic Instancing
```cpp
// Create base mesh
auto& sphere = chain.add<Sphere>("asteroid")
    .radius(0.15f)
    .segments(16)
    .computeTangents();  // Required for normal maps

// Create instanced renderer
auto& instanced = chain.add<InstancedRender3D>("asteroids")
    .mesh(&sphere)
    .cameraInput(&camera)
    .lightInput(&sun);

// Reserve capacity for performance
instanced.reserve(20000);

// In update(), add instances each frame
instanced.clearInstances();
for (int i = 0; i < count; i++) {
    Instance3D inst;
    inst.transform = glm::translate(glm::mat4(1.0f), position) *
                     glm::rotate(glm::mat4(1.0f), angle, axis) *
                     glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    inst.color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    inst.metallic = 0.2f;
    inst.roughness = 0.8f;
    instanced.addInstance(inst);
}
```

### PBR Textured Materials
```cpp
auto& rockMaterial = chain.add<TexturedMaterial>("rock")
    .baseColor("assets/materials/rock/base_color.png")
    .normal("assets/materials/rock/normal.png")
    .metallic("assets/materials/rock/metallic.png")
    .roughness("assets/materials/rock/roughness.png")
    .ao("assets/materials/rock/ambient_occlusion.png");

// Apply to instanced renderer
instanced.material(&rockMaterial);
```

### Multiple Lights
```cpp
auto& sun = chain.add<DirectionalLight>("sun")
    .direction(0.2f, 0.5f, 1.0f)
    .color(Color::fromHex("#FFF2E6"))
    .intensity(1.5f);

auto& fill = chain.add<DirectionalLight>("fill")
    .direction(0.0f, 0.3f, -1.0f)
    .color(Color::SteelBlue)
    .intensity(0.5f);

instanced.lightInput(&sun)
         .addLight(&fill);
```

### First-Person Camera
```cpp
auto& camera = chain.add<CameraOperator>("camera")
    .fov(70.0f)
    .farPlane(300.0f);

// In update() - set position and look-at directly
camera.position(posX, posY, posZ);
camera.target(targetX, targetY, targetZ);
```

### Transparent Background for Compositing
```cpp
// Render with transparent clear color
instanced.clearColor(Color::Transparent);

// Composite over background
auto& final = chain.add<Composite>("final")
    .inputA("stars")       // Background
    .inputB("asteroids")   // Foreground with alpha
    .mode(BlendMode::Over);
```

### Per-Instance Properties
```cpp
struct Instance3D {
    glm::mat4 transform;  // Position, rotation, scale
    glm::vec4 color;      // RGBA color multiplier
    float metallic;       // PBR metallic (0-1)
    float roughness;      // PBR roughness (0-1)
};
```

## Performance Tips

- Call `reserve(count)` with expected instance count
- Use `clearInstances()` at start of each frame
- Minimize per-instance state (use uniforms for shared data)
- Lower mesh segment count for distant objects
- Use frustum culling for off-screen objects

## Controls

- **V**: Toggle vsync

Camera automatically flies through the asteroid field.
