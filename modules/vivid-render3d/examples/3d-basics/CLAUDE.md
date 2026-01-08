# 3D Basics

Demonstrates the node-based 3D geometry workflow with primitives and CSG operations.

## Operators Used

- **SceneComposer** - Container for 3D geometry
- **Box, Sphere, Cylinder, Cone, Torus** - Primitive shapes
- **Boolean** - CSG operations (union, subtract, intersect)
- **CameraOperator** - Orbital camera control
- **Render3D** - 3D scene renderer

## Key Concepts

### Scene Composition
```cpp
// Create scene composer - entry point for all geometry
auto& scene = SceneComposer::create(chain, "scene");

// Add primitives directly to scene with transform and color
scene.add<Torus>("torus",
    glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)),
    glm::vec4(0.9f, 0.4f, 0.8f, 1.0f))  // Pink
    .outerRadius(0.5f)
    .innerRadius(0.15f);

scene.add<Cylinder>("cyl",
    glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)),
    glm::vec4(0.3f, 0.9f, 0.4f, 1.0f))  // Green
    .radius(0.3f)
    .height(1.5f)
    .flatShading(true);
```

### Primitive Shapes
```cpp
// Box
auto& box = chain.add<Box>("box")
    .size(1.2f, 1.2f, 1.2f)
    .flatShading(true);

// Sphere
auto& sphere = chain.add<Sphere>("sphere")
    .radius(0.85f)
    .segments(24);

// Cylinder
auto& cyl = chain.add<Cylinder>("cyl")
    .radius(0.3f)
    .height(1.5f)
    .segments(24);

// Cone
auto& cone = chain.add<Cone>("cone")
    .radius(0.4f)
    .height(1.0f)
    .segments(24);

// Torus (donut)
auto& torus = chain.add<Torus>("torus")
    .outerRadius(0.5f)
    .innerRadius(0.15f)
    .segments(32)
    .rings(16);
```

### CSG Boolean Operations
```cpp
// Create inputs (NOT added to scene directly)
auto& box = chain.add<Box>("box").size(1.2f, 1.2f, 1.2f);
auto& sphere = chain.add<Sphere>("sphere").radius(0.85f);

// Create boolean operation
auto& hollowCube = chain.add<Boolean>("hollowCube")
    .inputA("box")
    .inputB("sphere")
    .operation(BooleanOp::Subtract)  // Box minus sphere
    .flatShading(true);

// Add CSG result to scene
scene.add(&hollowCube,
    glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)),
    glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Light blue

// Boolean operations:
// BooleanOp::Union      - Combine shapes
// BooleanOp::Subtract   - Remove B from A
// BooleanOp::Intersect  - Keep only overlap
```

### Camera Control
```cpp
auto& camera = chain.add<CameraOperator>("camera")
    .orbitCenter(0.0f, 0.0f, 0.0f)  // Look-at point
    .distance(8.0f)                  // Distance from center
    .elevation(0.3f)                 // Vertical angle (radians)
    .azimuth(0.0f)                   // Horizontal angle
    .fov(50.0f)                      // Field of view
    .nearPlane(0.1f)
    .farPlane(100.0f);

// Animate orbit
camera.azimuth(ctx.time() * 0.2f);
```

### Lighting
```cpp
// Create directional light (sun-like)
auto& sun = chain.add<DirectionalLight>("sun");
sun.direction(1, 2, 1);        // Will be normalized
sun.color(1.0f, 1.0f, 1.0f);   // White light
sun.intensity = 1.0f;           // Full brightness
```

### Rendering
```cpp
auto& render = chain.add<Render3D>("render");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&sun);         // Use light operator
render.setShadingMode(ShadingMode::Flat);
render.setAmbient(0.2f);
render.setClearColor(0.08f, 0.08f, 0.12f);
render.setResolution(1920, 1080);   // Render target size
```

### Animating Scene Objects
```cpp
// Access scene entries to modify transforms
auto& scene = chain.get<SceneComposer>("scene");
auto& entries = scene.entries();

// Animate first entry (torus)
entries[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0, 0)) *
                       glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));
```

## Controls

No interactive controls - camera orbits automatically.
