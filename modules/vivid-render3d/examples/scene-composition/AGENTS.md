# Scene Composition Example

Demonstrates multi-object 3D scene composition with coordinated rendering.

## Operators Demonstrated

- **SceneComposer** - Combines meshes into a renderable scene
- **Render3D** - PBR renderer for 3D scenes
- **CameraOperator** - Camera positioning and projection

## Key Concepts

### Creating a Scene
```cpp
// Use create() to enable add<T>() method
auto& scene = SceneComposer::create(chain, "scene");

// Add meshes with transform and color
scene.add<Box>("box",
    glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)),  // Position
    glm::vec4(1.0f, 0.5f, 0.3f, 1.0f)                      // Color
).size(1.0f);

scene.add<Sphere>("ball").radius(0.5f);
```

### Transform Matrices
```cpp
// Identity (no transform)
glm::mat4 identity(1.0f);

// Translation
glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));

// Rotation (angle in radians, axis)
glm::mat4 r = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0));

// Scale
glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));

// Combined (order: scale → rotate → translate)
glm::mat4 combined = t * r * s;
```

### Animating Entries
```cpp
void update(Context& ctx) {
    auto& scene = chain.get<SceneComposer>("scene");

    // Update transform by entry index (order added)
    scene.setEntryTransform(0, newTransform);
    scene.setEntryColor(0, newColor);
}
```

### Root Transform
Apply a single transform to the entire scene:
```cpp
// Hover animation for entire scene
glm::mat4 hover = glm::translate(glm::mat4(1.0f),
    glm::vec3(0, sin(time) * 0.1f, 0));
scene.setRootTransform(hover);
```

### Rendering Setup
```cpp
auto& render = chain.add<Render3D>("render");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&sun);
render.setClearColor(0.1f, 0.12f, 0.15f);
render.setAmbient(0.2f);  // Ambient light level
```

### Camera Control
```cpp
auto& camera = chain.add<CameraOperator>("camera");
camera.setPosition(5.0f, 4.0f, 5.0f);
camera.setTarget(0.0f, 1.0f, 0.0f);
camera.setFOV(45.0f);

// Orbit around target
float angle = time * 0.2f;
camera.setPosition(
    cos(angle) * distance,
    height,
    sin(angle) * distance
);
```

### Lighting
```cpp
// Directional (sun)
auto& sun = chain.add<DirectionalLight>("sun");
sun.setDirection(-0.5f, -1.0f, -0.3f);
sun.setColor(1.0f, 0.95f, 0.9f);
sun.setIntensity(1.2f);

// Point light
auto& point = chain.add<PointLight>("lamp");
point.setPosition(2.0f, 3.0f, 0.0f);
point.setColor(1.0f, 0.8f, 0.6f);
point.setRange(10.0f);
```

## Available Primitives

```cpp
scene.add<Box>("box").size(1.0f);
scene.add<Sphere>("sphere").radius(0.5f).segments(24);
scene.add<Cylinder>("cyl").radius(0.3f).height(2.0f);
scene.add<Cone>("cone").radius(0.5f).height(1.0f);
scene.add<Torus>("torus").radius(1.0f).tubeRadius(0.3f);
scene.add<Plane>("plane").segments(1);
```

## Related Operators

- **GLTFLoader** - Load 3D models
- **Boolean** - CSG operations (union, subtract, intersect)
- **InstancedRender3D** - Render thousands of meshes
- **Fog** - Depth-based atmospheric fog
