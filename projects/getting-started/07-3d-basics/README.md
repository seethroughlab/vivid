# Lesson 07: 3D Basics

Introduction to 3D rendering with scenes, cameras, and lighting.

## What You'll Learn

- Creating a 3D scene with `SceneComposer`
- Adding primitive shapes (Box, Sphere, etc.)
- Setting up a camera
- Basic lighting
- Rendering to texture

## Prerequisites

- Completed Lesson 06: Video Input

## Run It

```bash
./build/bin/vivid projects/getting-started/07-3d-basics
```

You should see a spinning cube with simple lighting!

## Walkthrough

### The 3D Pipeline

3D rendering in Vivid follows this pattern:

```
[Scene] + [Camera] + [Light] → [Render3D] → [Output]
```

### Creating a Scene

```cpp
#include <vivid/render3d/render3d.h>

auto& scene = SceneComposer::create(chain, "scene");
```

The `SceneComposer` is a container for all your 3D geometry.

### Adding Shapes

Add primitives with position (transform) and color:

```cpp
scene.add<Box>("box",
    glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)),  // Position
    glm::vec4(0.8f, 0.3f, 0.2f, 1.0f)  // Color (RGBA)
).size(1.0f, 1.0f, 1.0f);
```

Available primitives: `Box`, `Sphere`, `Cylinder`, `Cone`, `Torus`

### Camera Setup

The `CameraOperator` provides orbital camera control:

```cpp
auto& camera = chain.add<CameraOperator>("camera");
camera.orbitCenter(0, 0, 0);  // Look-at point
camera.distance(5.0f);         // Distance from center
camera.elevation(0.3f);        // Vertical angle
camera.azimuth(0.0f);          // Horizontal angle
camera.fov(50.0f);             // Field of view
```

### Lighting

A `DirectionalLight` provides sun-like illumination:

```cpp
auto& light = chain.add<DirectionalLight>("light");
light.direction(1, 2, 1);           // Direction (normalized)
light.color(1.0f, 1.0f, 1.0f);      // White light
light.intensity = 1.0f;
```

### Rendering

`Render3D` combines scene, camera, and light into a texture:

```cpp
auto& render = chain.add<Render3D>("render3d");
render.setInput(&scene);
render.setCameraInput(&camera);
render.setLightInput(&light);
render.setClearColor(0.1f, 0.1f, 0.15f);  // Background
```

## Try It

1. **Change the shape**: Replace `Box` with `Sphere` or `Torus`
2. **Add more shapes**: Create multiple primitives at different positions
3. **Change colors**: Experiment with RGBA values
4. **Adjust lighting**: Change light direction and intensity
5. **Animate rotation**: Modify the transform in update()

## Coordinate System

- **X**: Right
- **Y**: Up
- **Z**: Toward camera (in default view)

## Transforms with GLM

```cpp
#include <glm/gtc/matrix_transform.hpp>

// Translation
glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z))

// Rotation (angle in radians)
glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0))

// Scale
glm::scale(glm::mat4(1.0f), glm::vec3(sx, sy, sz))

// Combine: translate, then rotate
auto transform = glm::translate(...) * glm::rotate(...);
```

## Next Steps

- **Lesson 08**: Combining modules (audio + 3D, video + 3D)
- **Deep dive**: `modules/vivid-render3d/examples/` for GLTF, PBR, CSG
