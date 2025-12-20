# Operator Base Class Expansion

Analysis of additional operator base classes inspired by TouchDesigner, with focus on reducing boilerplate.

## Current Architecture

```
Operator (base)
├── TextureOperator     - GPU texture effects (2D image processing)
├── AudioOperator       - Audio synthesis and generation (48kHz)
│   └── AudioEffect     - Audio effects with dry/wet mixing
├── AudioAnalyzer       - Audio analysis (levels, FFT, beat detection)
├── MeshOperator        - 3D mesh output (CSG-capable)
│   └── GeometryOperator - Procedural primitives (Box, Sphere, etc.)
└── (direct subclasses) - Value operators, cameras, lights
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

## GeometryOperator (Implemented)

**Purpose**: Base class for 3D geometry generators to reduce boilerplate.

Extends MeshOperator with ParamRegistry integration and common defaults:
- Default `init()` (empty) and `cleanup()` (releases mesh)
- Automatic `params()`/`getParam()`/`setParam()` via ParamRegistry
- Common shading options: `flatShading()`, `computeTangents()`
- `finalizeMesh()` helper for the build/upload cycle

```cpp
class Box : public GeometryOperator {
public:
    Box() {
        registerParam(m_width);
        registerParam(m_height);
        registerParam(m_depth);
    }

    void size(float w, float h, float d) {
        if (m_width != w || m_height != h || m_depth != d) {
            m_width = w; m_height = h; m_depth = d; markDirty();
        }
    }

    void process(Context& ctx) override {
        if (!needsCook()) return;
        m_builder = MeshBuilder::box(m_width, m_height, m_depth);
        finalizeMesh(ctx, true);  // Always flat for boxes
    }

    std::string name() const override { return "Box"; }

private:
    Param<float> m_width{"width", 1.0f, 0.01f, 100.0f};
    Param<float> m_height{"height", 1.0f, 0.01f, 100.0f};
    Param<float> m_depth{"depth", 1.0f, 0.01f, 100.0f};
};
```

**Boilerplate reduction**: ~40% fewer lines per primitive (from ~70 to ~30 lines).

**Files:**
- `addons/vivid-render3d/include/vivid/render3d/geometry_operator.h` - Base class
- `addons/vivid-render3d/include/vivid/render3d/primitives.h` - Box, Sphere, Cylinder, Cone, Torus, Plane

---

## ComputeOperator (Future)

**Purpose**: GPU compute shader base class for parallel data processing.

Use cases: GPU particles, physics simulation, point cloud processing, image analysis.

**Status**: Defer until a specific use case requires it.
