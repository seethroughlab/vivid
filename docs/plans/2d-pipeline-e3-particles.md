# Phase E.3 — Particles2D (Compute-Shader Particle Simulator)

**Status: COMPLETE 2026-04-19.** Flocking2D remains as a follow-up E.3b.

Detail plan for the first half of Phase E.3. Master plan at `docs/plans/2d-pipeline-redesign.md`; previous sub-phase at `docs/plans/2d-pipeline-e2-modifiers.md`.

## Context

Phase E.1 + E.2 established the 2D drawable pipeline with CPU-driven instancing (InstanceGrid2D → Instancer2D → Render2D). E.3 introduces the first **GPU-driven** drawable source: `Particles2D`. A compute shader simulates per-particle state in ping-pong storage buffers and writes per-frame `InstanceData2D` records. The output `VividDrawable2D` carries the instance buffer; Render2D's shape-instanced pipeline draws all N particles in a single `DrawIndexed(6, N)` call.

This delivers the TD-grade "tens of thousands of particles on screen" headline capability that was the original motivation for the whole 2D pipeline redesign.

## Design decisions (confirmed during implementation)

1. **No upstream drawable template input.** Particles2D always emits a SHAPE-type drawable (circle-SDF). Keeps the operator self-contained. Sprite-particle variants are deferred.
2. **Blend mode hardcoded to `VIVID_BLEND_ADDITIVE`.** Standard for particle systems (fire, sparks, embers).
3. **Dropped 3D-specific params:** `shape` (cuboid/billboard), `elongation` (velocity-orient). Kept `bounds` (NDC clamp), `learning_mode`.
4. **2D curl noise** uses 4 simplex evaluations instead of 3D's 9 — scalar-potential-field derivative in 2D yields `(dF/dy, -dF/dx)`.
5. **Sim + emit in one operator** (mirrors Particles3D). No split.

## Files shipped

**New:**
- `operators/gpu/particles_2d/particles_2d.cpp` — 573 LOC (3D was 876; simpler as predicted: no cuboid, no scene input, no billboard math, 2D noise instead of 3D).
- `graphs/gpu/particles_2d_demo.json` — 20K-particle ember fountain through Bloom.

**Modified:**
- `cmake/operators.cmake` — one new entry in the 2D-drawable-pipeline section.

## Implementation highlights

- **Particle storage:** 2 ping-pong storage buffers of 32-byte records (`vec2 position, vec2 velocity, f32 age, f32 lifetime, 8 bytes pad`). Bound alternately each frame via `bind_group_a_` / `bind_group_b_`.
- **Compute shader:** one thread per particle via `@compute @workgroup_size(256)`. Reads `particles_in`, writes `particles_out` + per-slot `InstanceData2D` to `instances_out`.
- **Spawn path:** dead-particle slots claim spawn positions via `atomicAdd(&counter, 1u)`; first `new_spawns` claimants reinitialize with a random-angle velocity in the emission cone.
- **Atomic counter:** reset to 0 each frame via `wgpuQueueWriteBuffer`; compute shader uses it to coordinate spawn-slot allocation.
- **Instance write:** for live particles, write column-major `mat3x2` transform (scale × position) + alpha-faded colour. For dead particles, write a degenerate off-screen record so the vertex shader doesn't draw them.
- **Output drawable:** `type = SHAPE`, `blend_mode = ADDITIVE`, `shape_sides = 0` (circle), `shape_softness = param`, `instance_buffer = compute-output buffer`, `instance_count = max_count`. Render2D's shape-instanced pipeline handles the rest.

## Verified end-to-end

1. **Build:** `particles_2d.dylib` produced and registered. Warning-only (no errors) post the unused-field cleanup.
2. **Smoke:** 5000 particles with `emission_rate=1500, curl_strength=1.5, gravity=-0.2` — fountain of ember-coloured dots flowing up, curling from noise, falling under gravity. Animation visible, colours correct.
3. **Scale:** 50,000 particles with `emission_rate=15000`. Runtime stays responsive; visuals show a massive flowing particle river with clear noise-driven swirling. One `DrawIndexed(6, 50000)` per frame.
4. **Coexistence:** Operator loads cleanly in a fresh runtime, no shader-compile errors, no validation errors, no crashes during session.

## Known limitations / follow-up work

- **Flocking2D (E.3b):** deferred. Same architecture — copy Particles2D, swap curl-noise advection for Reynolds boids cohesion/alignment/separation kernels. Probably ~400 LOC.
- **Sprite particles:** no current way to emit textured quads from Particles2D. Would need a texture input on Particles2D OR a new operator that takes a drawable template + particle-sim bundle.
- **Per-particle SDF shape variation:** the shape-instanced pipeline uses per-drawable shape params; can't vary shape per instance. Would need a new pipeline variant with instance-indexed shape data.
- **`_pad0/_pad1` in the CPU Particle struct:** the CPU-side record is 32 bytes to match WGSL struct alignment (each `vec2f` is 8-byte aligned, with trailing pad to 16-byte alignment at the struct end — so the layout is 32 bytes total). Documented in the WGSL struct comments.

## Verification commands

```bash
# Build
cmake --build build --target particles_2d

# Smoke test via MCP
ensure_runtime
new_graph
add_node Particles2D p
add_node Render2D r
connect p/drawable → r/drawable
connect r/texture → video_out/input
set_param p count 5000
set_param p curl_strength 1.5
capture_image interface
```

## Scope bound

- ~2 hours of focused work (faster than the 2-day plan estimate thanks to the clean transplant from Particles3D).
- Particles2D only — Flocking2D shipped separately as E.3b.
- **Not in scope:** particle-emits-sprite variant, per-particle SDF shape, destination-read blend modes, UI affordances.

## E.3b Flocking2D (COMPLETE 2026-04-19)

Sister operator to Particles2D, same architecture, different force model.

**Key differences from Particles2D:**
- No emission/aging — boids are persistent from CPU-side random init.
- Force model: Reynolds 3-rules (separation / alignment / cohesion). O(N²) neighbour loop per boid in compute shader.
- Params: `view_radius`, `sep_radius`, `separation`/`alignment`/`cohesion` weights, `max_speed`/`min_speed`, `wrap` (NDC edge wrap toggle), `seed`, color, size, softness.
- CPU-side random init on rebuild: uniform position in `[-1, 1]²`, random-angle unit velocity × max_speed.

**Files shipped:**
- `operators/gpu/flocking_2d/flocking_2d.cpp` — ~480 LOC.
- `graphs/gpu/flocking_2d_demo.json` — 1200-boid flock through Bloom.
- `cmake/operators.cmake` — one entry.

**Verified:** 800 boids running live; visible separation (roughly even spacing), visible clustering (denser regions forming). Runtime stays responsive at 800; practical ceiling ~2000-4000 before the O(N²) becomes the bottleneck.

**Known limitations:**
- O(N²) neighbour search. Spatial hashing / grid bins would lift the ceiling to ~20k+ boids; deferred.
- No predator/prey, goal-seeking, obstacle avoidance — a minimum-viable Reynolds.
- No per-boid colour variation. Whole flock shares the drawable's tint.
