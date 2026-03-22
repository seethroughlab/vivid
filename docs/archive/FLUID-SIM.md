# Phase 5: Fluid Simulation Operator — Implementation Plan

## Context

This is the final and most complex GPU operator in the GPU Bindable Operators plan (`docs/GPU-BINDABLE-OPS.md`). It implements 2D Navier-Stokes via the Stable Fluids method (Jos Stam, 1999), requiring 7 distinct shader passes per frame — more than any existing operator. Phases 1–4 (Instanced Shapes, Flocking, Trails, Reaction-Diffusion, Cellular Automata) are already implemented and establish the patterns we'll follow.

## Architecture Overview

**7 passes per frame:**
1. Advect velocity (semi-Lagrangian backtracing)
2. Apply forces (Gaussian splat at emitter position)
3. Compute divergence (central differences)
4. Pressure solve (N Jacobi iterations, ping-pong pressure textures)
5. Subtract pressure gradient (project velocity to divergence-free)
6. Advect dye (semi-Lagrangian, same as pass 1 but for dye field)
7. Visualize (map dye to output color)

**State textures (all RGBA16Float):**
- `velocity_tex_[2]` — ping-pong, RG = (vx, vy)
- `pressure_tex_[2]` — ping-pong, R = pressure scalar
- `dye_tex_[2]` — ping-pong, RGB = dye color
- `divergence_tex_` — single (written once per frame, read by pressure solver)

Total: 7 RGBA16Float textures at sim resolution.

**Sim resolution**: Configurable (default 256x256), independent of output. Visualization pass upsamples with bilinear filtering to output dimensions.

## File

`operators/gpu/fluid/fluid.cpp` — single self-contained file following existing operator conventions.

## Implementation Phases

### Phase 5a: Skeleton & Parameters

Create the operator class with all params, ports, role bindings, and empty `process_gpu`.

**Params** (from spec):
| Param | Type | Default | Range |
|-------|------|---------|-------|
| `viscosity` | float | 0.0001 | 0.0–0.01 |
| `diffusion` | float | 0.0001 | 0.0–0.01 |
| `pressure_iters` | int | 20 | 4–40 |
| `buoyancy` | float | 0.5 | 0.0–2.0 |
| `dissipation` | float | 0.98 | 0.8–1.0 |
| `emitter_x` | float | 0.5 | 0.0–1.0 |
| `emitter_y` | float | 0.5 | 0.0–1.0 |
| `emitter_radius` | float | 0.05 | 0.01–0.2 |
| `force_strength` | float | 300.0 | 0.0–1000.0 |
| `color_r` | float | 1.0 | 0.0–1.0 |
| `color_g` | float | 0.3 | 0.0–1.0 |
| `color_b` | float | 0.1 | 0.0–1.0 |
| `sim_resolution` | int | 256 | 64–512 (dropdown: 64/128/256/512) |
| `reset` | int | 0 | trigger (Off/Reset) |

**Ports:**
- Input (optional): `input` — texture (dye source)
- Output: `texture`

**Role bindings (SHARED):**
- `viscosity_mod`, `buoyancy_mod`, `force_mod`

**Key patterns to follow:**
- `collect_params` with `layout_row` — ref: `operators/gpu/reaction_diffusion/reaction_diffusion.cpp`
- `collect_role_bindings` with `VividRoleBindingDescriptor` — same ref
- `SharedBinding` struct with `maybe_init_shared` / `process_shared` — same ref

### Phase 5b: GPU Initialization

Lazy init on first `process_gpu` call. Create all GPU resources.

**Resources to create:**
- 7 shaders (advect_vel, forces, divergence, pressure, gradient_sub, advect_dye, visualize)
- 7 pipelines (sim passes target RGBA16Float, vis pass targets output format)
- 7 state textures + views (velocity[2], pressure[2], dye[2], divergence)
- 2 uniform buffers (sim + vis, or 1 shared if layout is compatible)
- 1 linear sampler (for bilinear texture reads)
- Bind layout: standard (uniform + sampler + N textures)
- Bind groups per pass (see below)

**Bind group strategy:**
Each pass reads different textures, so we need per-pass bind groups:
- Advect velocity: reads `velocity_tex_[ping]`
- Forces: reads `velocity_tex_[write]` (just written by advect)
- Divergence: reads `velocity_tex_[write]`
- Pressure iteration: reads `pressure_tex_[ping]` + `divergence_tex_`
- Gradient subtract: reads `velocity_tex_[write]` + `pressure_tex_[result]`
- Advect dye: reads `velocity_tex_[final]` + `dye_tex_[ping]`
- Visualize: reads `dye_tex_[result]`

Since passes share a uniform buffer but read different texture combinations, we'll need:
- A bind layout supporting uniform(0) + sampler(1) + tex_a(2) + tex_b(3) (2 texture slots covers all cases; passes needing only 1 texture bind a dummy/self for slot 2)
- Alternatively: two layouts (1-texture and 2-texture). **Prefer single 2-texture layout** for simplicity, with unused slot bound to any valid view.

**Uniform struct:**
```cpp
struct FluidUniforms {
    float sim_res[2];       // vec2f — sim grid dimensions
    float dt;               // f32 — delta time (capped)
    float viscosity;        // f32
    float force_x;          // f32 — emitter force direction x
    float force_y;          // f32 — emitter force direction y
    float force_strength;   // f32
    float emitter_x;        // f32
    float emitter_y;        // f32
    float emitter_radius;   // f32
    float dissipation;      // f32
    float buoyancy;         // f32
    float dye_r;            // f32
    float dye_g;            // f32
    float dye_b;            // f32
    float padding;          // f32 — align to 16-byte boundary (64 bytes total)
};
```

**Resolution change handling:**
- Track `cached_sim_res_` — if `sim_resolution` param changes, destroy and recreate all 7 state textures + rebuild bind groups.
- Also track output resolution for vis bind group rebuild.

### Phase 5c: WGSL Shaders

7 inline fragment shaders, all using `fullscreenTriangle()` vertex stage from `FULLSCREEN_VERTEX_WGSL`.

All shaders share the same uniform struct declaration and bind group layout:
```wgsl
struct Uniforms {
    sim_res: vec2f,
    dt: f32,
    viscosity: f32,
    force_x: f32,
    force_y: f32,
    force_strength: f32,
    emitter_x: f32,
    emitter_y: f32,
    emitter_radius: f32,
    dissipation: f32,
    buoyancy: f32,
    dye_r: f32,
    dye_g: f32,
    dye_b: f32,
    padding: f32,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tex_a: texture_2d<f32>;
@group(0) @binding(3) var tex_b: texture_2d<f32>;
```

#### Shader 1: Advect Velocity (`kAdvectVelFragment`)
Semi-Lagrangian backtracing. For each texel, trace backward through the velocity field and sample.
```
pos = uv
vel = sample(velocity, uv).rg
back_pos = pos - vel * dt / sim_res
output.rg = sample(velocity, back_pos).rg * dissipation_factor
```
- Reads: `velocity_tex_[read]` via tex_a
- Writes to: `velocity_tex_[write]`

#### Shader 2: Apply Forces (`kForcesFragment`)
Gaussian splat at emitter position. Adds velocity + dye at emitter.
```
vel = sample(velocity, uv).rg
dist = distance(uv, emitter_pos)
gauss = exp(-dist*dist / (2 * radius * radius))
vel += force_dir * force_strength * gauss * dt
output.rg = vel
```
- Reads: `velocity_tex_[write]` (just advected) via tex_a
- Writes to: `velocity_tex_[read]` (swap back — or use a temp; simpler to write back to read and swap ping)

**Ping-pong strategy for velocity:**
- Advect: read=ping, write=1-ping → swap ping
- Forces: read=ping (now points to advected), write=1-ping → swap ping
- This way each pass reads the latest and writes to the other.

#### Shader 3: Compute Divergence (`kDivergenceFragment`)
Central differences on velocity field.
```
texel = 1.0 / sim_res
vL = sample(velocity, uv - vec2(texel.x, 0)).r
vR = sample(velocity, uv + vec2(texel.x, 0)).r
vB = sample(velocity, uv - vec2(0, texel.y)).g
vT = sample(velocity, uv + vec2(0, texel.y)).g
div = 0.5 * (vR - vL + vT - vB)
output.r = div
```
- Reads: `velocity_tex_[current]` via tex_a
- Writes to: `divergence_tex_`

#### Shader 4: Pressure Solve — Jacobi Iteration (`kPressureFragment`)
Iterated N times (ping-ponging pressure textures).
```
texel = 1.0 / sim_res
pL = sample(pressure, uv - vec2(texel.x, 0)).r
pR = sample(pressure, uv + vec2(texel.x, 0)).r
pB = sample(pressure, uv - vec2(0, texel.y)).r
pT = sample(pressure, uv + vec2(0, texel.y)).r
div = sample(divergence, uv).r
p = (pL + pR + pB + pT - div) / 4.0
output.r = p
```
- Reads: `pressure_tex_[ping]` via tex_a + `divergence_tex_` via tex_b
- Writes to: `pressure_tex_[1-ping]`
- Repeated `pressure_iters` times, flipping pressure ping each time

#### Shader 5: Subtract Pressure Gradient (`kGradientSubFragment`)
Projects velocity to divergence-free field.
```
texel = 1.0 / sim_res
pL = sample(pressure, uv - vec2(texel.x, 0)).r
pR = sample(pressure, uv + vec2(texel.x, 0)).r
pB = sample(pressure, uv - vec2(0, texel.y)).r
pT = sample(pressure, uv + vec2(0, texel.y)).r
vel = sample(velocity, uv).rg
vel -= 0.5 * vec2(pR - pL, pT - pB)
output.rg = vel
```
- Reads: `velocity_tex_[current]` via tex_a + `pressure_tex_[result]` via tex_b
- Writes to: `velocity_tex_[other]` → swap ping

#### Shader 6: Advect Dye (`kAdvectDyeFragment`)
Same semi-Lagrangian as shader 1, but for dye field using final velocity.
```
vel = sample(velocity, uv).rg
back_pos = uv - vel * dt / sim_res
dye = sample(dye_field, back_pos).rgb * dissipation
output.rgb = dye
```
Also blends in optional input texture and emitter dye color (Gaussian splat).
- Reads: `velocity_tex_[final]` via tex_a + `dye_tex_[ping]` via tex_b
- Writes to: `dye_tex_[1-ping]`

#### Shader 7: Visualize (`kVisFragment`)
Maps dye to output. Simple passthrough with optional tone mapping.
```
dye = sample(dye_field, uv).rgb
output = vec4f(dye, 1.0)
```
- Reads: `dye_tex_[result]` via tex_a
- Writes to: `ctx->output_texture_view` (output format, not RGBA16Float)

### Phase 5d: Process Loop

`process_gpu` per-frame logic:

```
1. Lazy init if needed
2. Check sim_resolution change → recreate state textures
3. Handle reset trigger → clear all state textures
4. Init/update shared role bindings (viscosity_mod, buoyancy_mod, force_mod)
5. Compute effective params (base + mod * scale)
6. Cap dt (e.g., min(delta_time, 1/30))
7. Write uniforms to GPU buffer

// Simulation passes:
8.  Advect velocity:     read vel[ping] → write vel[1-ping], swap vel_ping
9.  Apply forces:        read vel[ping] → write vel[1-ping], swap vel_ping
10. Compute divergence:  read vel[ping] → write divergence
11. Pressure solve loop (N iterations):
      read pressure[p_ping] + divergence → write pressure[1-p_ping], swap p_ping
12. Gradient subtract:   read vel[ping] + pressure[p_ping] → write vel[1-ping], swap vel_ping
13. Advect dye:          read vel[ping] + dye[d_ping] → write dye[1-d_ping], swap d_ping
14. Visualize:           read dye[d_ping] → write output_texture_view
```

**Bind group management:**
- Passes that read different ping states need bind groups rebuilt each frame (or maintain 2 bind groups per pass like reaction-diffusion).
- For the pressure solver (iterated N times), maintain `pressure_bg_[2]` that swap.
- Most efficient: pre-build 2 bind groups per pass at init, index by ping state at runtime.

**Bind groups needed (2 per ping-pong pass):**
- `advect_vel_bg_[2]` — velocity read
- `forces_bg_[2]` — velocity read (after advect swap)
- `divergence_bg_[2]` — velocity read
- `pressure_bg_[2]` — pressure read + divergence
- `gradient_bg_[2]` — velocity + pressure read
- `advect_dye_bg_[2]` — velocity + dye read
- `vis_bg_[2]` — dye read

Since velocity and pressure have independent ping counters, some bind groups become more nuanced. **Simplification**: rebuild bind groups for passes that depend on multiple ping states. Or, pre-build all 4 combinations. Given 7 passes, pre-building 2 per pass (14 total) is manageable.

**Input texture handling:**
- If optional input texture is connected, blend it into dye during the forces/dye advection pass (as an additive dye source).
- Use fallback 1x1 transparent texture pattern from feedback.cpp when input is disconnected.

### Phase 5e: CMakeLists.txt Registration

Add to `CMakeLists.txt`:
```cmake
add_vivid_operator(fluid operators/gpu/fluid/fluid.cpp EXTRA_LIBS webgpu)
```

### Phase 5f: Cleanup & Reset

**Reset trigger:**
- When `reset` param transitions 0→1, clear all state textures.
- Clearing: render a pass that writes zeros, or use `wgpuCommandEncoderClearBuffer` equivalent.
- Simplest: run one frame of advect/pressure with zero state — or just do a clear color pass to each state texture.

**Destructor:**
Release all GPU handles: 7 shaders, 7 pipelines, 2 uniform buffers, 1 sampler, bind layout, pipe layout, 7 textures + views, all bind groups.

## Verification

1. **Build**: `vivid build` compiles without errors
2. **Load**: Create graph with Fluid operator, verify it renders (swirling dye patterns)
3. **Emitter**: Adjust emitter position/radius/strength, verify force injection works
4. **Pressure convergence**: Increase `pressure_iters`, verify smoother flow (fewer artifacts)
5. **Role bindings**: Bind LFO to `viscosity_mod`, verify viscosity oscillates over time
6. **Reset**: Toggle reset, verify state clears and simulation restarts
7. **Input texture**: Connect an image, verify it acts as dye source
8. **Sim resolution**: Change resolution, verify textures recreate and sim continues
9. **Performance**: At 256x256 with 20 pressure iters (27 total passes), should maintain 60fps on modern GPU

## Key Reference Files

- `operators/gpu/reaction_diffusion/reaction_diffusion.cpp` — closest pattern (ping-pong, SHARED bindings, multi-pass)
- `operators/gpu/cellular_automata/cellular_automata.cpp` — sim vs output resolution, step accumulation
- `operators/gpu/feedback/feedback.cpp` — input texture handling, CopyTextureToTexture
- `src/operator_api/gpu_common.h` — all GPU helpers (`create_shader`, `create_pipeline`, `run_pass`, `create_state_texture`, etc.)
- `src/operator_api/bound_control_instance.h` — BoundControlInstance for role bindings
