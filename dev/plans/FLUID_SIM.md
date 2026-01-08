# Fluid Simulation Plan (vivid-fluid module)

## Goal

Add GPU-accelerated fluid simulation to Vivid, inspired by [ofxFlowTools](https://github.com/moostrik/ofxFlowTools). Create a new `vivid-fluid` module with full Navier-Stokes solver and VJ-friendly controls.

---

## Architecture Overview

### Simulation Pipeline (per frame)

```
1. Add Forces (mouse, texture input, optical flow)
       ↓
2. Advection (semi-Lagrangian, velocity self-advection)
       ↓
3. Diffusion (Jacobi iteration for viscosity)
       ↓
4. Vorticity Confinement (curl → force injection)
       ↓
5. Pressure Projection:
   a. Compute divergence
   b. Jacobi pressure solve (40 iterations)
   c. Subtract pressure gradient from velocity
       ↓
6. Advect Dye/Density (for visualization)
       ↓
7. Render to output texture
```

### Key Data Structures

| Buffer | Format | Purpose |
|--------|--------|---------|
| Velocity A/B | RG16Float | Ping-pong velocity field |
| Pressure | R16Float | Pressure field |
| Divergence | R16Float | Velocity divergence |
| Dye A/B | RGBA16Float | Color/density ping-pong |
| Curl | R16Float | Vorticity magnitude |

---

## Module Structure

```
modules/vivid-fluid/
├── CMakeLists.txt
├── module.json
├── README.md
├── include/vivid/fluid/
│   ├── fluid.h              # Module header (includes all)
│   └── fluid_sim.h          # Main FluidSim operator
├── src/
│   ├── fluid_sim.cpp        # FluidSim implementation
│   └── shaders/             # Embedded WGSL shaders
│       ├── advect.wgsl
│       ├── diffuse.wgsl
│       ├── divergence.wgsl
│       ├── pressure.wgsl
│       ├── gradient_subtract.wgsl
│       ├── vorticity.wgsl
│       ├── add_force.wgsl
│       └── render_dye.wgsl
└── examples/
    ├── fluid-basic/         # Mouse-driven fluid
    └── fluid-audio/         # Audio-reactive fluid
```

> **Note**: Optical flow is NOT part of vivid-fluid. Use vivid-opencv's OpticalFlow
> operator and pipe its output into FluidSim's force input. This keeps vivid-fluid
> independent with no opencv dependency.

---

## Operators

### 1. FluidSim (Primary Operator)

```cpp
class FluidSim : public TextureOperator {
public:
    // Parameters
    Param<float> viscosity{"viscosity", 0.0001f, 0.0f, 0.01f};
    Param<float> dissipation{"dissipation", 0.99f, 0.9f, 1.0f};
    Param<float> vorticity{"vorticity", 0.3f, 0.0f, 1.0f};
    Param<float> dyeDissipation{"dyeDissipation", 0.98f, 0.9f, 1.0f};
    Param<int> pressureIterations{"pressureIterations", 40, 10, 80};
    Param<int> diffuseIterations{"diffuseIterations", 20, 0, 40};
    Param<float> forceScale{"forceScale", 1.0f, 0.0f, 5.0f};

    // Force injection
    void addForce(float x, float y, float dx, float dy, float radius = 0.01f);
    void addDye(float x, float y, float r, float g, float b, float radius = 0.01f);
    void setForceInput(TextureOperator* forces);  // Velocity from optical flow
    void setDyeInput(TextureOperator* dye);       // Color from webcam/image

    // Control
    void clear();
    void pause();
    void resume();
};
```

**Inputs:**
- Input 0 (optional): Force/velocity texture (e.g., from OpticalFlow)
- Input 1 (optional): Dye/color texture (e.g., from Webcam)

**Output:** RGBA texture with rendered fluid dye

---

## WGSL Shaders

### advect.wgsl (Semi-Lagrangian Advection)
```wgsl
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var velocityIn: texture_2d<f32>;
@group(0) @binding(2) var fieldIn: texture_2d<f32>;
@group(0) @binding(3) var samp: sampler;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let vel = textureSample(velocityIn, samp, in.uv).xy;
    let pos_back = in.uv - u.dt * u.rdx * vel;
    var result = textureSample(fieldIn, samp, pos_back);
    result *= u.dissipation;
    return result;
}
```

### pressure.wgsl (Jacobi Iteration)
```wgsl
// Single Jacobi iteration for pressure solve
// alpha = -dx², beta = 0.25
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let L = textureSample(pressureIn, samp, in.uv + vec2f(-u.texelSize.x, 0.0)).r;
    let R = textureSample(pressureIn, samp, in.uv + vec2f(u.texelSize.x, 0.0)).r;
    let B = textureSample(pressureIn, samp, in.uv + vec2f(0.0, -u.texelSize.y)).r;
    let T = textureSample(pressureIn, samp, in.uv + vec2f(0.0, u.texelSize.y)).r;
    let bC = textureSample(divergenceIn, samp, in.uv).r;

    let pressure = (L + R + B + T + u.alpha * bC) * u.beta;
    return vec4f(pressure, 0.0, 0.0, 1.0);
}
```

---

## Implementation Phases

### Phase 1: Core Infrastructure
1. Create module structure (CMakeLists.txt, module.json)
2. Implement ping-pong texture management
3. Implement basic advection shader
4. Get minimal fluid (advection only) rendering

### Phase 2: Full Navier-Stokes
1. Add diffusion (Jacobi iteration)
2. Add divergence computation
3. Add pressure solve (Jacobi, 40 iterations)
4. Add gradient subtraction
5. Add vorticity confinement

### Phase 3: Interaction & Input
1. Add mouse/touch force injection
2. Add texture-based force input (accepts RG velocity textures, e.g. from vivid-opencv)
3. Add dye injection and rendering

### Phase 4: Polish & Examples
1. Add parameter controls for all coefficients
2. Implement state save/load for hot-reload
3. Create examples (basic, webcam, audio-reactive)
4. Write CLAUDE.md documentation
5. Add to MCP operator registry

---

## Files to Create

### Module Setup
- `modules/vivid-fluid/CMakeLists.txt`
- `modules/vivid-fluid/module.json`
- `modules/vivid-fluid/README.md`

### Headers
- `modules/vivid-fluid/include/vivid/fluid/fluid.h`
- `modules/vivid-fluid/include/vivid/fluid/fluid_sim.h`

### Implementations
- `modules/vivid-fluid/src/fluid_sim.cpp`

### Shaders (embedded in .cpp or separate .wgsl)
- advect.wgsl
- diffuse.wgsl
- divergence.wgsl
- pressure.wgsl
- gradient_subtract.wgsl
- vorticity.wgsl
- add_force.wgsl
- render_dye.wgsl

### Examples
- `modules/vivid-fluid/examples/fluid-basic/chain.cpp`
- `modules/vivid-fluid/examples/fluid-basic/CLAUDE.md`

---

## Reference Templates (from Vivid codebase)

| Pattern | Template File |
|---------|---------------|
| Compute + ping-pong | `modules/vivid-core/src/effects/gpu_particles.cpp` |
| Multi-pass rendering | `modules/vivid-core/src/effects/blur.cpp` |
| Persistent state | `modules/vivid-core/include/vivid/effects/feedback.h` |
| WGSL shaders | `modules/vivid-core/include/vivid/effects/gpu_common.h` |
| GPU handle RAII | `modules/vivid-core/include/vivid/effects/gpu_handle.h` |

---

## API Usage Examples

### Basic (Mouse-driven)

```cpp
#include <vivid/vivid.h>
#include <vivid/fluid/fluid.h>

using namespace vivid;
using namespace vivid::fluid;

FluidSim* fluid;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    fluid = &chain.add<FluidSim>("fluid");
    fluid->viscosity = 0.0001f;
    fluid->vorticity = 0.4f;

    chain.output(fluid);
}

void update(Context& ctx) {
    // Mouse interaction
    if (ctx.mousePressed()) {
        auto [mx, my] = ctx.mousePos();
        auto [dx, dy] = ctx.mouseDelta();
        fluid->addForce(mx, my, dx * 10, dy * 10, 0.02f);
        fluid->addDye(mx, my, 1.0f, 0.5f, 0.2f, 0.02f);
    }

    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

### With Optical Flow (requires vivid-opencv)

```cpp
#include <vivid/vivid.h>
#include <vivid/fluid/fluid.h>
#include <vivid/opencv/opencv.h>  // Provides OpticalFlow

using namespace vivid;
using namespace vivid::fluid;
using namespace vivid::opencv;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input
    auto& cam = chain.add<Webcam>("cam");

    // Optical flow from vivid-opencv (NOT part of vivid-fluid)
    auto& flow = chain.add<OpticalFlow>("flow");
    flow.input(&cam);
    flow.scale = 2.0f;

    // Fluid simulation
    auto& fluid = chain.add<FluidSim>("fluid");
    fluid.setForceInput(&flow);   // Motion drives fluid
    fluid.setDyeInput(&cam);      // Webcam colors the fluid
    fluid.viscosity = 0.0001f;
    fluid.vorticity = 0.4f;

    chain.output(&fluid);
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

---

## Performance Considerations

1. **Resolution independence**: Simulation can run at lower resolution than output
2. **Iteration counts**: Expose pressureIterations, diffuseIterations as params
3. **RG16Float for velocity**: 2 channels sufficient, saves bandwidth
4. **Texture format**: Use R16Float for scalar fields (pressure, divergence)
5. **Bind group caching**: Pre-create bind groups for all ping-pong configurations

---

## ofxFlowTools Reference

Key algorithms adapted from:
- **Advection**: Semi-Lagrangian backward trace
- **Pressure solve**: Jacobi iteration (40 iterations standard)
- **Vorticity**: Curl magnitude → force injection

Source: https://github.com/moostrik/ofxFlowTools

> **Note**: Optical flow will be implemented separately in vivid-opencv module.
