# Operator Base Class Expansion

Analysis of additional operator base classes inspired by TouchDesigner, with focus on reducing boilerplate.

## Current Architecture

```
Operator (base)
├── TextureOperator     - GPU texture effects (2D image processing)
├── AudioOperator       - Audio synthesis and generation (48kHz)
│   └── AudioEffect     - Audio effects with dry/wet mixing
├── AudioAnalyzer       - Audio analysis (levels, FFT, beat detection)
└── (direct subclasses) - Value operators, 3D geometry, cameras, lights
```

## Philosophy: C++ vs Node-Based

TouchDesigner requires containers (CHOPs, TOPs) for everything because the node graph *is* the data model. Want a counter? Make a CHOP. Want a color? Make a CHOP.

In Vivid/C++, plain variables work fine:
```cpp
float m_phase = 0.0f;
glm::vec3 m_color;
m_phase += ctx.deltaTime();
```

Only wrap something in an operator if you need it to:
1. **Appear in the chain visualizer**
2. **Be controllable via OSC/external UI**
3. **Be hot-reloadable** (persisted across code changes)

For internal state, use plain member variables.

---

## Debug Value Display (Implemented)

Visualize arbitrary values with rolling history graphs:

```cpp
void update(Context& ctx) {
    m_phase += ctx.deltaTime() * rate;

    // Show values in debug overlay with sparkline graphs
    ctx.debug("phase", m_phase);
    ctx.debug("lfo", sinf(ctx.time() * 2.0f));
    ctx.debug("envelope", levels.rms());
    ctx.debug("beat", beatDetect.triggered());
}
```

**Features:**
- Values displayed in floating ImGui panel (bottom-left)
- Rolling history (~2 seconds) shown as sparkline graph
- Auto-cleanup: values disappear after 1 second of no updates
- Supports: float, int, bool, vec2/vec3 (as magnitude)

**Files:**
- `core/include/vivid/context.h` - DebugValue struct and debug() methods
- `core/src/context.cpp` - Implementation
- `core/imgui/chain_visualizer.cpp` - Panel rendering

---

## GeometryOperator (Future)

**Purpose**: Base class for 3D geometry generators to reduce boilerplate.

Currently `Sphere`, `Cube`, `Plane`, etc. all implement similar patterns. A base class would consolidate mesh storage, dirty tracking, and bounding box computation.

```cpp
class GeometryOperator : public Operator, public ParamRegistry {
public:
    const MeshData& mesh() const;
    bool meshChanged();
    const BoundingBox& boundingBox() const;
    OutputKind outputKind() const override { return OutputKind::Geometry; }

protected:
    virtual void generateMesh() = 0;
    void setMesh(MeshData mesh);
    void computeBoundingBox();
};
```

**Status**: Not yet implemented. Consider when geometry primitive count grows.

---

## ComputeOperator (Future)

**Purpose**: GPU compute shader base class for parallel data processing.

Use cases: GPU particles, physics simulation, point cloud processing, image analysis.

**Status**: Defer until a specific use case requires it.
