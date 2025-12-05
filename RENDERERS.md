# Vivid 3D Rendering Architecture

## Overview

Vivid currently has **two separate 3D rendering operators** that cannot be used together in the same scene:

1. **`Render3D`** - For procedural/programmatic geometry
2. **`GLTFViewer`** - For loading and displaying GLTF model files

This document explains why they exist as separate systems, the technical reasons they can't currently interoperate, and options for consolidation.

---

## The Two Renderers

### Render3D (`runtime/src/operators/render3d.cpp`)

**Purpose:** Render procedurally-created geometry with PBR materials.

**Key characteristics:**
- Uses DiligentFX's `PBR_Renderer` class
- Works with Vivid's `Mesh` class (created via `MeshUtils::createSphere()`, `createBox()`, etc.)
- Materials are `PBRMaterial` objects that you load/configure manually
- Scene is built programmatically: `addObject()`, `addLight()`, `setEnvironment()`
- You control every aspect: transforms, materials, UV scaling

**Typical usage:**
```cpp
Mesh sphere;
sphere.create(ctx.device(), MeshUtils::createSphere(32, 16, 0.5f));

PBRMaterial bronzeMat;
bronzeMat.loadFromDirectory(ctx, "assets/materials/bronze-bl", "bronze");

render3d->addObject(&sphere, transform);
render3d->getObject(0)->material = &bronzeMat;
render3d->setEnvironment(iblEnv.get());
```

### GLTFViewer (`runtime/src/operators/gltf_viewer.cpp`)

**Purpose:** Load and display complete GLTF/GLB model files.

**Key characteristics:**
- Uses DiligentFX's `GLTF_PBR_Renderer` class (different from `PBR_Renderer`!)
- Works with `GLTFModel` class that wraps DiligentFX's GLTF loader
- Materials are embedded in the GLTF file and auto-loaded
- Handles GLTF-specific features: node hierarchy, animations, skinning, morph targets
- Scene comes from the file; you just position the camera and lights

**Typical usage:**
```cpp
gltfViewer->loadModel(ctx, "assets/models/DamagedHelmet.gltf");
gltfViewer->loadEnvironment(ctx, "assets/hdris/environment.hdr");
gltfViewer->camera().setOrbit(glm::vec3(0), 3.0f, 45.0f, 20.0f);
```

---

## Why Two Different Renderers?

### 1. Different DiligentFX Classes

DiligentFX provides two separate rendering systems:

| Class | Purpose | Shader System |
|-------|---------|---------------|
| `PBR_Renderer` | Generic PBR rendering with custom vertex formats | Flexible, configurable |
| `GLTF_PBR_Renderer` | GLTF-specific rendering with GLTF vertex format | GLTF-optimized |

These are **not interchangeable**. They have:
- Different shader pipelines
- Different constant buffer layouts
- Different resource binding patterns
- Different vertex input layouts

### 2. Different Vertex Formats

**Render3D's `Vertex3D`:**
```cpp
struct Vertex3D {
    glm::vec3 position;   // 12 bytes
    glm::vec3 normal;     // 12 bytes
    glm::vec2 uv;         // 8 bytes
    glm::vec3 tangent;    // 12 bytes
};  // 44 bytes total
```

**GLTF's vertex format:**
- Variable depending on what's in the file
- May include: multiple UV sets, vertex colors, joint indices, joint weights
- Handled automatically by `GLTF_PBR_Renderer`

### 3. Different Material Systems

**Render3D / PBRMaterial:**
- You explicitly load texture files
- You set metallic/roughness values
- You control UV tiling
- Materials are separate objects you assign to meshes

**GLTF:**
- Materials are defined in the .gltf/.glb file
- Textures are embedded or referenced
- Material parameters come from the file
- `GLTF_PBR_Renderer` handles all binding automatically

### 4. Different Scene Concepts

**Render3D:**
- Flat list of objects with transforms
- No hierarchy
- No animations
- Simple and explicit

**GLTF:**
- Node hierarchy (parent-child transforms)
- Skeletal animations with skinning
- Morph targets (blend shapes)
- Multiple meshes per model
- Scene graphs

---

## Why Can't They Work Together?

### Technical Barriers

1. **Separate PSO (Pipeline State Object) caches**
   - Each renderer builds its own shader pipelines
   - They can't share PSOs because the shaders are different

2. **Different constant buffer layouts**
   - `PBR_Renderer` expects `PBRFrameAttribs`, `PBRPrimitiveAttribs`, `PBRMaterialAttribs`
   - `GLTF_PBR_Renderer` has its own frame/primitive/material structures

3. **Different SRB (Shader Resource Binding) patterns**
   - Resources are bound differently
   - Texture slots have different meanings

4. **No shared scene graph**
   - Render3D has `std::vector<Object3D>`
   - GLTFViewer has `std::vector<GLTFModel>` with internal node trees
   - No common representation

### Practical Implications

- You cannot have a GLTF robot standing on a procedural floor
- You cannot add procedural particles to a GLTF scene
- You cannot mix imported assets with generated geometry
- Compositing (render separately, combine textures) loses shared lighting/shadows

---

## Consolidation Options

### Option A: Extend Render3D to Load GLTF

**Approach:** Add GLTF loading capability to Render3D by converting GLTF meshes to `Mesh` objects.

**Implementation:**
1. Parse GLTF file using a library (tinygltf, cgltf, or DiligentFX's loader)
2. Convert each GLTF mesh to Vivid's `Mesh` format
3. Convert GLTF materials to `PBRMaterial`
4. Flatten the node hierarchy into transforms

**Pros:**
- Single renderer, single code path
- Full control over all objects
- Simpler architecture

**Cons:**
- Loses GLTF animations/skinning (would need to reimplement)
- Loses morph targets
- Conversion overhead
- May lose some GLTF material features

**Effort:** Medium (2-3 days for basic support, weeks for animations)

### Option B: Extend GLTFViewer to Accept Procedural Meshes

**Approach:** Add `addMesh()` to GLTFViewer that wraps procedural geometry.

**Implementation:**
1. Convert `Mesh` to GLTF-compatible vertex buffer format
2. Create a "fake" GLTF node for the procedural mesh
3. Use `GLTF_PBR_Renderer`'s material system

**Pros:**
- Keeps all GLTF features
- Single rendering pass

**Cons:**
- Fighting against GLTF_PBR_Renderer's assumptions
- Complex vertex format conversion
- Material system mismatch

**Effort:** High (GLTF_PBR_Renderer isn't designed for this)

### Option C: New Unified Scene3D Operator

**Approach:** Create a new operator that manages a unified scene, delegating to appropriate renderers.

**Implementation:**
```cpp
class Scene3D : public Operator {
    // Scene contains both:
    std::vector<Object3D> proceduralObjects;  // Uses PBR_Renderer
    std::vector<GLTFModel> gltfModels;        // Uses GLTF_PBR_Renderer

    void process(Context& ctx) {
        // Render GLTF models first
        renderGLTFModels(ctx);
        // Then render procedural objects into same framebuffer
        renderProceduralObjects(ctx);
    }
};
```

**Pros:**
- Preserves both systems' full capabilities
- Relatively straightforward to implement
- Shared lighting could be passed to both

**Cons:**
- Two rendering passes (performance cost)
- Depth buffer sharing complexity
- Still somewhat duplicated code

**Effort:** Medium (1-2 days for basic version)

### Option D: Custom Unified Renderer (Long-term)

**Approach:** Write a single renderer from scratch that handles both use cases.

**Implementation:**
1. Design a unified vertex format that supports both cases
2. Write shaders that handle optional attributes
3. Create a material system that works for both loaded and manual materials
4. Implement scene graph that can hold both model nodes and raw meshes

**Pros:**
- Clean architecture
- Optimal performance
- Full flexibility

**Cons:**
- Significant engineering effort
- Reinventing what DiligentFX already provides
- Risk of bugs in PBR implementation

**Effort:** Very High (weeks to months)

---

## Recommendation

For Vivid's creative-coding focus, **Option C (Unified Scene3D)** offers the best balance:

1. **Preserves existing work** - Both renderers continue to function
2. **Enables mixing** - GLTF models and procedural geometry in one scene
3. **Incremental** - Can be built on top of existing code
4. **Pragmatic** - Two render passes is acceptable for creative tools

### Proposed API

```cpp
auto scene = std::make_unique<Scene3D>();

// Load GLTF models
int robotId = scene->loadModel(ctx, "robot.gltf");
scene->setModelTransform(robotId, robotTransform);

// Add procedural geometry
Mesh floor;
floor.create(ctx.device(), MeshUtils::createPlane(10, 10));
int floorId = scene->addMesh(&floor, floorTransform);
scene->getMesh(floorId)->material = &concreteMaterial;

// Shared lighting
scene->addLight(Light3D::directional(glm::vec3(-1, -1, -1), 3.0f));
scene->setEnvironment(iblEnv.get());

// Single process call renders everything
scene->process(ctx);
```

This would unify the workflows while keeping the underlying DiligentFX renderers doing what they do best.

---

## Current Workaround

Until consolidation happens, you can composite separate renders:

```cpp
// Render GLTF to texture
gltfViewer->process(ctx);

// Render procedural to texture
render3d->process(ctx);

// Composite (but no shared shadows/reflections)
composite->setInput(0, gltfViewer);
composite->setInput(1, render3d);
composite->blendMode(BlendMode::Over);
composite->process(ctx);
```

This works but loses the benefits of a unified 3D scene (shared lighting, proper depth sorting, shadows, reflections between objects).
