# vivid-render3d

3D rendering with procedural geometry, CSG operations, and PBR/flat shading.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

### Primitives
| Operator | Description |
|----------|-------------|
| `Sphere` | Sphere mesh with configurable segments |
| `Box` | Box/cube mesh |
| `Cylinder` | Cylinder mesh |
| `Cone` | Cone mesh |
| `Torus` | Torus/donut mesh |
| `Plane` | Flat plane mesh |

### Scene Management
| Operator | Description |
|----------|-------------|
| `SceneComposer` | Combine meshes into scenes |
| `CameraOperator` | Camera positioning and control |
| `Render3D` | Render scene to texture |
| `InstancedRender3D` | GPU-instanced rendering |

### Materials
| Operator | Description |
|----------|-------------|
| `Material` | Basic material properties |
| `TexturedMaterial` | PBR textures (albedo, normal, metallic, roughness, AO) |

### Lighting
| Operator | Description |
|----------|-------------|
| `DirectionalLight` | Directional light source |
| `IBLEnvironment` | Image-based lighting with HDR environment maps |

### Geometry
| Operator | Description |
|----------|-------------|
| `MeshBuilder` | Programmatic mesh construction |
| `Boolean` | CSG operations (union, subtract, intersect) |
| `GLTFLoader` | Load GLTF/GLB models |

### Post-Processing
| Operator | Description |
|----------|-------------|
| `DepthOfField` | Depth-based blur effect |

## Examples

Minimal, focused examples (~50-100 lines) demonstrating core API patterns:

| Example | Description |
|---------|-------------|
| [3d-basics](examples/3d-basics) | Primitives, CSG, camera control |
| [scene-composition](examples/scene-composition) | Composing meshes into scenes |
| [csg-modeling](examples/csg-modeling) | Boolean operations on meshes |
| [instancing](examples/instancing) | GPU instancing for 20K+ objects |
| [tree-forest](examples/tree-forest) | Procedural trees with instancing |

## Showcase

Rich, complete demonstrations (200+ lines) with creative applications:

| Showcase | Description |
|----------|-------------|
| [gltf-loader](showcase/gltf-loader) | Loading GLTF models with PBR+IBL |
| [raycasting](showcase/raycasting) | Interactive 3D raycasting |

## Examples vs Showcase

- **`examples/`** - Minimal, focused code (~50-100 lines) demonstrating core API patterns. Designed for quick reference and LLM consumption.
- **`showcase/`** - Rich, complete demonstrations (200+ lines) with UI, visuals, and creative applications. Designed for user inspiration.

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/render3d/render3d.h>

using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create a sphere
    chain.add<Sphere>("sphere")
        .radius(1.0f)
        .segments(32);

    // Compose scene
    chain.add<SceneComposer>("scene")
        .add("sphere");

    // Camera
    chain.add<CameraOperator>("camera")
        .position(0.0f, 2.0f, 5.0f)
        .target(0.0f, 0.0f, 0.0f);

    // Directional light
    chain.add<DirectionalLight>("light")
        .direction(-1.0f, -1.0f, -1.0f)
        .intensity(1.0f);

    // Render
    chain.add<Render3D>("render")
        .scene("scene")
        .camera("camera")
        .light("light")
        .shading(Shading::PBR);

    chain.add<Output>("out")
        .input("render");
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

## Shading Modes

- **Flat** - Solid colors, no lighting
- **PBR** - Physically-based rendering with metallic/roughness workflow
- **PBR+IBL** - PBR with image-based lighting from HDR environment maps

## API Reference

See the examples in `modules/vivid-render3d/examples/` for usage patterns.

## Dependencies

- vivid-core
- vivid-io (for texture loading)

## License

MIT
