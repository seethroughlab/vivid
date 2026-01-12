# Lesson 07: 3D Basics

This lesson introduces 3D rendering with scenes, cameras, and lighting.

## Lesson Objectives

1. Create a 3D scene with SceneComposer
2. Add primitive shapes
3. Set up camera and lighting
4. Render to texture output

## Key Concepts

- **SceneComposer**: Container for 3D geometry
- **Primitives**: Box, Sphere, Cylinder, Cone, Torus
- **CameraOperator**: Orbital camera for viewing
- **DirectionalLight**: Sun-like illumination
- **Render3D**: Renders scene to texture

## What the Code Demonstrates

- Minimal 3D scene setup
- Single rotating cube
- Basic orbital camera animation
- Simple directional lighting

## Suggested Modifications

1. **Add more shapes**:
   ```cpp
   scene.add<Sphere>("sphere",
       glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0, 0)),
       glm::vec4(0.2f, 0.6f, 0.9f, 1.0f))
       .radius(0.5f)
       .segments(24);
   ```

2. **Use different primitives**:
   ```cpp
   // Torus (donut)
   scene.add<Torus>("torus", transform, color)
       .outerRadius(0.5f)
       .innerRadius(0.15f);

   // Cylinder
   scene.add<Cylinder>("cyl", transform, color)
       .radius(0.3f)
       .height(1.0f);

   // Cone
   scene.add<Cone>("cone", transform, color)
       .radius(0.4f)
       .height(1.0f);
   ```

3. **Change shading mode**:
   ```cpp
   render.setShadingMode(ShadingMode::Smooth);  // vs Flat
   ```

4. **Add multiple lights** (not directly - use ambient):
   ```cpp
   render.setAmbient(0.3f);  // Ambient fill
   ```

5. **Apply 2D effects to the 3D output**:
   ```cpp
   auto& bloom = chain.add<Bloom>("bloom");
   bloom.input("render3d");
   bloom.threshold = 0.5f;
   chain.output("bloom");
   ```

## Available Primitives

| Primitive | Key Properties |
|-----------|----------------|
| `Box` | `.size(w, h, d)` |
| `Sphere` | `.radius(r)`, `.segments(n)` |
| `Cylinder` | `.radius(r)`, `.height(h)`, `.segments(n)` |
| `Cone` | `.radius(r)`, `.height(h)`, `.segments(n)` |
| `Torus` | `.outerRadius(r)`, `.innerRadius(r)`, `.segments(n)`, `.rings(n)` |

## Transform Quick Reference

```cpp
// Position at (x, y, z)
glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z))

// Rotate around Y axis
glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0))

// Scale uniformly
glm::scale(glm::mat4(1.0f), glm::vec3(s, s, s))

// Combine (order matters: translate * rotate * scale)
auto transform = glm::translate(...) * glm::rotate(...);
```

## Common Issues

- **Black screen**: Check camera distance and look-at point
- **Can't see object**: Check if object is inside camera near/far planes
- **Wrong colors**: Make sure alpha (4th component) is 1.0

## Next Lesson

08-combining-modules: Making audio-reactive 3D scenes
