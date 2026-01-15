# Lesson 7: 3D Basics

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Render3D: SceneComposer, Box, Sphere, Cylinder, Cone, Torus, CameraOperator, DirectionalLight, Render3D
- Core: Bloom

## Lesson Focus
3D rendering with scenes, cameras, lighting, and primitive shapes.

## Key Concepts
- **SceneComposer**: Container for 3D geometry
- **Primitives**: Box, Sphere, Cylinder, Cone, Torus
- **CameraOperator**: Orbital camera for viewing
- **DirectionalLight**: Sun-like illumination
- **Render3D**: Renders scene to texture

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
glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z))  // Position
glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0))  // Rotate Y
glm::scale(glm::mat4(1.0f), glm::vec3(s, s, s))  // Scale
// Combine: translate * rotate * scale
```

## Suggested Modifications
1. Add more shapes with different transforms
2. Change shading: `render.setShadingMode(ShadingMode::Smooth)`
3. Apply 2D effects to 3D output: `bloom.input("render3d")`

## Troubleshooting
- **Black screen**: Check camera distance and look-at point
- **Can't see object**: Check if object is inside camera near/far planes
- **Wrong colors**: Make sure alpha (4th component) is 1.0

## Next
08-combining-modules: Making audio-reactive 3D scenes
