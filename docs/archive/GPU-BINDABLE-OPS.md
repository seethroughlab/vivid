# GPU Bindable Operators

## Context
We have 7 bindable control operators (LFO, Envelope, Macro, MSEG, RandomSH, StepSeq, Clock) and 1 GPU operator using role bindings (Particles with per-voice envelope). This plan adds 6 new GPU operators that leverage role bindings for real-time visual modulation.

## Implementation Order

### Phase 1: Instanced Shapes
Generalizes Particles to arbitrary SDF geometry with per-instance role bindings.

- **Pattern:** Per-voice bindings + SDF rendering (combines Particles + Shape patterns)
- **Params:** count (1-64), shape (circle/tri/square/pentagon/hex/star), base_size, softness, color, layout (random/grid/circle/line), animate, speed
- **Role bindings (PER_VOICE):** `scale`, `rotation`, `color_mod`
- **Output:** texture
- **GPU:** Single fullscreen pass, uniform with 2x `array<vec4f, 64>` (position+size+rotation, color+alpha), fragment loops over instances evaluating SDFs
- **File:** `operators/gpu/instanced_shapes/instanced_shapes.cpp`

### Phase 2: Flocking / Boids
Emergent swarm with classical separation/alignment/cohesion rules. CPU sim (N<=64, O(N^2) trivial), GPU render.

- **Params:** count (1-64), separation_radius, alignment/cohesion/separation weights, max_speed, size, color, trail_length, boundary_mode (wrap/bounce)
- **Role bindings (PER_VOICE):** `speed_mod`, `separation_mod`, `alignment_mod`
- **Output:** texture
- **GPU:** Same per-voice uniform packing as Instanced Shapes. Oriented triangle or soft circle SDF per boid, optional directional trail via elongated ellipse SDF
- **File:** `operators/gpu/flocking/flocking.cpp`

### Phase 3: Trails / Ribbons
Persistent motion trails with history buffers. Bridge between per-voice and ping-pong patterns.

- **Params:** count (1-32), decay (0.8-1.0), width, color, speed, curvature, glow
- **Role bindings (PER_VOICE):** `width_mod`, `opacity_mod`, `color_shift`
- **Input (optional):** texture (trails track bright spots)
- **Output:** texture
- **GPU:** Two-pass: (1) decay previous frame via ping-pong, (2) draw new trail segments as soft capsule SDFs. `CopyTextureToTexture` to persist. Uniform with `array<vec4f, 32>` per-trail data
- **File:** `operators/gpu/trails/trails.cpp`

### Phase 4a: Reaction-Diffusion
Gray-Scott reaction-diffusion with evolving organic textures.

- **Params:** feed_rate (F), kill_rate (k), diffusion_a, diffusion_b, iterations (1-32 per frame), color_mode, seed_radius, reset trigger
- **Role bindings (SHARED):** `feed_mod`, `kill_mod`, `diffusion_mod`
- **Output:** texture
- **GPU:** Two RGBA16Float ping-pong state textures (R=A, G=B). N iterations per frame: 5-point Laplacian stencil, Gray-Scott update equations. Final visualization pass maps concentrations to color
- **File:** `operators/gpu/reaction_diffusion/reaction_diffusion.cpp`

### Phase 4b: Cellular Automata
Game of Life, HighLife, Seeds, and custom birth/survival rules.

- **Params:** rule_mode (Life/HighLife/Seeds/Custom), birth_min/max, survive_min/max, grid_size (64-1024), speed, alive/dead colors, randomize trigger, fill_density
- **Role bindings (SHARED):** `birth_threshold`, `survive_threshold`
- **Output:** texture
- **GPU:** Two state textures at grid_size resolution, ping-pong. Moore neighborhood (8 neighbors), rule evaluation. Visualization pass with nearest-neighbor sampling to preserve cell edges. Step accumulator for fractional-frame timing
- **File:** `operators/gpu/cellular_automata/cellular_automata.cpp`

### Phase 5: Fluid Simulation
2D Navier-Stokes (Stable Fluids). Most complex — requires 7 shader passes.

- **Params:** viscosity, diffusion, pressure_iters (4-40), buoyancy, dissipation, emitter position/radius, force_strength, color
- **Role bindings (SHARED):** `viscosity_mod`, `buoyancy_mod`, `force_mod`
- **Input (optional):** texture (dye source)
- **Output:** texture
- **GPU:** Configurable sim resolution (default 256x256, upsampled for output). State textures: velocity[2], pressure[2], dye[2], divergence (all RGBA16Float). 7 passes per frame:
  1. Advect velocity (semi-Lagrangian backtracing)
  2. Apply forces (Gaussian splat at emitter)
  3. Compute divergence (central differences)
  4. Pressure solve (N Jacobi iterations, ping-pong pressure)
  5. Subtract pressure gradient
  6. Advect dye
  7. Visualize (map dye to color)
- **File:** `operators/gpu/fluid/fluid.cpp`

## Key Design Decisions

- **CPU simulation for per-voice operators** (Instanced Shapes, Flocking, Trails): N<=64 agents is trivial on CPU, avoids GPU readback complexity for role binding queries
- **Ping-pong via CopyTextureToTexture**: Follow Feedback operator pattern — render to output, copy to persistent state
- **Fluid sim resolution independent of output**: Configurable grid (default 256x256), visualization pass upsamples with bilinear filtering
- **Multiple PER_VOICE roles**: Each role gets its own BoundControlInstance pool, following the `maybe_init_*` pattern from Particles
- **Pool resizing on count change**: Compare pool size to N, reinit if changed (same as Particles)

## Reference Files
- `operators/gpu/particles/particles.cpp` — PER_VOICE binding pool, lazy GPU init, uniform packing
- `operators/gpu/feedback/feedback.cpp` — Ping-pong texture pattern, CopyTextureToTexture
- `operators/gpu/shape/shape.cpp` — SDF rendering (polygon/star)
- `src/operator_api/gpu_common.h` — create_shader, create_pipeline, run_pass, create_state_texture, etc.
- `src/operator_api/bound_control_instance.h` — BoundControlInstance API
- `CMakeLists.txt` — add_vivid_operator registration

## CMakeLists.txt Additions
```cmake
add_vivid_operator(instanced_shapes   operators/gpu/instanced_shapes/instanced_shapes.cpp   EXTRA_LIBS webgpu)
add_vivid_operator(flocking           operators/gpu/flocking/flocking.cpp                     EXTRA_LIBS webgpu)
add_vivid_operator(trails             operators/gpu/trails/trails.cpp                         EXTRA_LIBS webgpu)
add_vivid_operator(reaction_diffusion operators/gpu/reaction_diffusion/reaction_diffusion.cpp EXTRA_LIBS webgpu)
add_vivid_operator(cellular_automata  operators/gpu/cellular_automata/cellular_automata.cpp   EXTRA_LIBS webgpu)
add_vivid_operator(fluid              operators/gpu/fluid/fluid.cpp                           EXTRA_LIBS webgpu)
```

## Verification
- Each operator: build, load in graph, verify GPU rendering output
- Per-voice operators: bind LFO/Envelope to roles, verify per-instance modulation
- Shared operators: bind LFO to params, verify global modulation over time
- Ping-pong operators: verify state persistence across frames, reset triggers work
- Fluid: verify all 7 passes produce correct visual output, pressure solve converges
