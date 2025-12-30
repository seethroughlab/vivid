# POPs Research: Lessons from TouchDesigner's Point Operators

Research into TouchDesigner's new POPs (Point Operators) family and potential applications to Vivid's geometry system.

## What are POPs?

POPs (Point Operators) are TouchDesigner's first new operator family since DATs in 2005, introduced in the 2025 release. They represent a fundamental rethinking of 3D geometry processing:

- **GPU-First**: Entire pipeline runs on GPU (vs CPU-based SOPs from 1995)
- **Points-First**: Everything is points with attributes; primitives are optional topology
- **Attribute-Rich**: Each point carries named data (position, normal, color, user-defined)
- **Instancing-Native**: Points can directly drive geometry instancing

## Core Concepts

### Points-First Data Model

Traditional mesh thinking: **Primitive → Triangles → Vertices**
```
Box → 12 triangles → 36 vertices (with duplication)
```

POPs thinking: **Points → Attributes → Optional Topology**
```
Points (with P, N, Color, custom attrs) → optionally connect as triangles/lines
```

This enables:
- Point clouds without primitives
- Particles as first-class citizens
- Instance sources without full mesh overhead
- Data visualization beyond rendering

### Attribute System

Every point carries named attributes:

| Attribute | Description |
|-----------|-------------|
| P | Position (vec3) |
| N | Normal (vec3) |
| Color | RGBA color |
| User-defined | Arbitrary named float arrays |

Attributes flow through the operator network and can be:
- Modified by operators
- Auto-generated (e.g., noise displacement adds velocity)
- Used for instancing, shaders, export

### Instancing Integration

A Grid POP's points can directly instance geometry:
```
Grid POP (100x100 points with P, Color, Scale attrs)
    ↓
Geometry COMP (instances a sphere at each point)
```

No intermediate conversion - point attributes map directly to instance transforms.

## Current Vivid Architecture

### What We Have

```cpp
// Vertex3D - fixed attribute set
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::vec4 color;
};

// MeshBuilder - primitive-first construction
auto builder = MeshBuilder::sphere(radius, segments);
builder.computeNormals();
Mesh mesh = builder.build();
mesh.upload(ctx);

// SceneComposer - handles instancing separately
composer.addInstance(meshOp, transform, material);
```

### Strengths
- Clean primitive generation via MeshBuilder
- CSG operations via Manifold
- Efficient GPU upload
- Instancing via SceneComposer

### Gaps vs POPs
- Fixed vertex attributes (no user-defined)
- No point-only output (always triangulated)
- Instancing separate from geometry generation
- CPU-based mesh generation

## Potential Improvements

### 1. Custom Vertex Attributes

Extend the attribute system to support named float arrays:

```cpp
// Hypothetical API
builder.addAttribute("velocity", 3);  // vec3 per vertex
builder.addAttribute("age", 1);       // float per vertex

// In shader
layout(location = 5) in vec3 velocity;
layout(location = 6) in float age;
```

**Use cases:**
- Motion blur (velocity attribute)
- Particle age/lifetime
- Per-vertex instance variation
- Custom shader data

### 2. PointCloud Output Mode

Add `OutputKind::PointCloud` for operators that output points without triangulation:

```cpp
class PointCloud : public Operator {
public:
    OutputKind outputKind() const override { return OutputKind::PointCloud; }

    // Returns points with attributes, no primitives
    const PointData& points() const;
};
```

**Use cases:**
- Particle systems
- Instance sources (scatter points on surface)
- LiDAR/scan data visualization
- Data-driven point visualization

### 3. Direct Instancing Bridge

Let geometry operators output instance data directly:

```cpp
class Scatter : public GeometryOperator {
public:
    // Generates points on input surface for instancing
    void process(Context& ctx) override {
        // Output: points with P, N, scale, rotation attributes
        // Can be fed directly to SceneComposer instancing
    }
};

// Usage
auto& scatter = chain.add<Scatter>("scatter");
scatter.meshInput(&terrain);
scatter.count(1000);

composer.addInstances(&scatter, &treeMesh, material);
```

### 4. ComputeOperator (Future)

GPU compute shader base class for massive parallelism:

```cpp
class ComputeOperator : public Operator {
protected:
    // Subclasses provide compute shader
    virtual std::string computeShader() const = 0;

    // Base handles dispatch, buffer management
    void dispatch(Context& ctx, int workgroups);
};
```

**Use cases:**
- GPU particle simulation
- Point cloud processing
- Procedural geometry at scale
- Physics/collision on GPU

## Recommendations

### Near-term (Low effort, High value)

1. **PointCloud struct** - Simple point storage with position + optional attributes
2. **Scatter operator** - Generate points on mesh surface for instancing
3. **Instance attribute passthrough** - Let point attributes feed SceneComposer

### Medium-term (Design required)

4. **Custom vertex attributes** - Extend Vertex3D or create flexible attribute storage
5. **PointCloudOperator base class** - Like GeometryOperator but for point output

### Long-term (Significant work)

6. **ComputeOperator** - GPU compute base class
7. **Full attribute flow** - Named attributes flowing through operator network

## Philosophy Alignment

POPs philosophy aligns well with Vivid's approach:

| POPs | Vivid |
|------|-------|
| GPU-first for performance | WebGPU for cross-platform GPU |
| Node-based for artists | C++ for programmers, but visual chain debugger |
| Attributes for flexibility | Param<T> for introspection |
| Points as universal primitive | Could adopt for particles/instancing |

The key insight: **Points with attributes are a universal primitive** that can represent meshes, particles, instance sources, and abstract data. Adopting this mental model could unify several currently-separate systems.

## References

- [POPs Announcement (May 2024)](https://derivative.ca/community-post/pops-new-operator-family-touchdesigner/69468)
- [TouchDesigner 2025 Release Notes](https://derivative.ca/release/202531550/73152)
- [2025 Official Update](https://derivative.ca/community-post/2025-official-update/73153)
- [Interactive Immersive: What's New in 2025](https://interactiveimmersive.io/blog/touchdesigner-resources/whats-new-in-the-2025-touchdesigner-release/)
