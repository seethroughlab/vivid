# 3D Operators for Vivid — Architecture Research Report

## Where We Are Today

Vivid has three operator domains (Control, Audio, GPU). GPU operators run fullscreen 2D fragment shaders — no vertex buffers, no meshes, no 3D transforms, no depth testing. WebGPU infrastructure (device, queue, textures, compute) is in place. Data flows through typed ports: control floats, control spreads, audio buffers, and GPU textures.

This report surveys how four leading real-time visual tools handle 3D, then maps three possible architectures onto Vivid with detailed tradeoff analysis.

---

## Part 1: How the Reference Platforms Handle 3D

### TouchDesigner

**Model: Strict operator family separation with an explicit rendering bridge.**

TD splits 3D across multiple operator families. **SOPs** (Surface Operators) create and manipulate geometry on the CPU — meshes, NURBS, curves, point clouds. SOPs produce no visual output on their own. A **Geometry COMP** wraps a SOP network, gives it a world-space transform, and assigns a **MAT** (Material Operator — Phong, PBR, or custom GLSL). **Camera** and **Light** COMPs define viewpoint and illumination. The **Render TOP** pulls from all of these and rasterizes the scene into a 2D texture that feeds back into the regular compositing pipeline (TOPs).

In 2025, TD added **POPs** (Point Operators) — GPU-accelerated geometry and particles that bypass the CPU bottleneck of SOPs.

The pipeline is: `SOP (geometry) → COMP (scene object + transform + material) → Render TOP (2D image)`. 3D and 2D are separate worlds, bridged explicitly.

### vvvv (gamma + VL.Stride)

**Model: Game engine scene graph exposed through visual programming.**

vvvv wraps the open-source Stride game engine. It offers a **high-level** workflow (entity-component scene graph — build a tree of entities using Group nodes, attach mesh/material/light/camera components, children inherit parent transforms) and a **low-level** workflow (implement `IRenderer`, issue draw calls directly, participate at specific render stages like Opaque, Transparent, ShadowCaster).

Scene entities flow through the node graph and get collected at render sinks (`SceneWindow` for display, `SceneTexture` for render-to-texture). Both workflows can combine freely.

This is the most "traditional 3D" approach — essentially visual scripting over a game engine.

### Max/MSP (Jitter)

**Model: Named rendering context with implicit object registration.**

Jitter's `jit.gl.render` object owns a named drawing context. Any GL object (`jit.gl.gridshape`, `jit.gl.mesh`, `jit.gl.model`, etc.) created with a matching context name **automatically appears in the scene** — no explicit wiring needed. Sending `jit.gl.render` a `bang` draws one frame.

Geometry is represented as Jitter matrices — multi-plane float32 arrays where planes map to position, texcoords, normals, and colors. You can process vertices with standard matrix operators. `jit.gl.node` provides hierarchical transform grouping.

The approach is loosely structured but flexible — objects appear by convention rather than explicit connection.

### Notch

**Model: Unified node graph with no domain boundaries.**

Notch organizes nodes into 17+ categories (Geometry, Lighting, Materials, Deformers, Cloning, Particles, Fields/Volumetrics, Procedural/SDFs, Physics, Post-FX, Ray-Tracing, etc.) all living in one graph. A material node can drive geometry deformation and vice versa. There's no "2D world" that 3D renders into — the entire graph is a 3D scene description.

NURA (Notch Unified Rendering Architecture) provides four physically-based renderers (Path Tracer, Smart Tracer, Hybrid, Standard) sharing unified materials and lighting. Users can switch between them or mix renderers in one project.

### Reference Platform Comparison

| Aspect | TouchDesigner | vvvv | Max/Jitter | Notch |
|---|---|---|---|---|
| **3D model** | Separate families bridged by Render TOP | Game engine ECS | Named-context auto-registration | Unified node graph |
| **Geometry** | SOPs (CPU), POPs (GPU) | Stride meshes | Float32 matrices | Nodes with mesh data |
| **Scene composition** | Explicit (Render TOP pulls COMPs) | Scene graph tree | Implicit (shared context name) | Parent-child hierarchy |
| **Materials** | Separate MAT family | Stride material system | jit.gl.material / jit.gl.shader | Material nodes in graph |
| **Rendering** | Render TOP → texture | SceneWindow / SceneTexture | jit.gl.render (bang-driven) | NURA (4 renderers) |
| **2D↔3D boundary** | Explicit bridge | Explicit (SceneTexture) | Explicit (render to matrix) | Blurred / nonexistent |

### Cross-Cutting Patterns

1. **Render-to-texture bridge**: All four platforms ultimately produce a 2D image from 3D. TD's explicit bridge is cleanest for Vivid's architecture — a Render operator that outputs a GPU texture.
2. **Geometry as data vs. scene object**: TD SOPs and Max matrices treat geometry as data you process through chains. vvvv and Notch treat geometry as scene objects you compose into trees. Vivid's operator-chain model maps naturally to the "data" approach.
3. **GPU-native geometry**: TD's 2025 POPs move validates going GPU-native from the start rather than building CPU geometry infrastructure.

---

## Part 2: Three Architecture Options for Vivid

### Option A: Separated (TouchDesigner-style)

New port types carry different 3D data through the graph. Mesh operators produce meshes. Material operators produce materials. An explicit AssignMaterial operator combines them. Transform operators position things. A Render3D operator collects everything and outputs a 2D GPU texture.

```
[BoxMesh] → [AssignMaterial] → [Transform3D] → [Render3D] → [Bloom] → output
                  ↑                  ↑              ↑
            [PBRMaterial]      rotation param    [Camera]
```

**Pros:**
- Purely additive — doesn't touch the existing 2D pipeline at all
- Each operator has a clear, single responsibility
- Clean mental model: geometry operators → Render3D → texture operators
- Easy to reason about performance (one render pass at the Render3D operator)
- Fits Vivid's existing architecture perfectly (typed ports, chain evaluation)

**Cons:**
- Verbose — the audio-reactive cube scenario requires ~7 operators
- Users must understand multiple new port types (mesh, material, transform, scene)
- Composition overhead: every object needs mesh + material + transform + camera + render
- Two separate mental worlds (3D space vs. 2D texture space)
- Feedback from 2D→3D is indirect (texture → control values → 3D params)
- Multi-object scenes: need to wire N chains into Render3D, or use a merge operator

### Option B: Unified (Notch-style)

3D and 2D operators coexist in the GPU domain. The graph itself IS the scene description. Any node can drive any other — a material reads from a texture operator, a deformer reads audio data, geometry drives material properties. A render operator at the end traverses the graph to collect everything.

```
[Shape3D] → [Deform] ─┐
                        ├→ [Group] → [SceneRender] → [Bloom] → output
[Light] ───────────────┘      ↑
                           Material
                           Camera
```

**Pros:**
- Maximum creative expressiveness — no artificial boundaries
- Single mental model — users just connect things
- Adding objects to a scene is just adding nodes
- Encourages unexpected combinations (Notch users love this)

**Cons:**
- **Major architectural overhaul**: The renderer must understand graph topology, traverse it to find geometry/lights/cameras, and build a render list. This is fundamentally different from Vivid's current "each node processes independently" model.
- **Evaluation complexity**: Currently operators evaluate in dependency order, each transforming its inputs. A unified renderer must collect scene elements from across the graph, which breaks this pattern.
- **Type ambiguity**: What does a wire between a 2D texture operator and a 3D geometry operator mean? Many cross-domain interactions need defining.
- **Existing pipeline disruption**: 2D GPU operators may need rethinking to coexist with 3D scene objects.
- **Cannot ship incrementally**: The scene-graph-traversal renderer must exist before any 3D operator is useful.
- **Performance**: When does rendering happen? What triggers re-render? Multiple renderers in one graph?

### Option C: Hybrid — "Scene Fragment" Port

Keep Vivid's domain separation and operator-chain model. Introduce ONE new port type — a **scene fragment** — that bundles geometry + material + transform into a single data type flowing through the graph, just like GPU textures flow today.

```
[Shape3D (cube)] → [Transform3D] → [SceneMerge] → [Render3D] → [Bloom] → output
 color, roughness    rotation ← beat    ↑              ↑
 emission ← beat                   [Shape3D (sphere)]  Camera params
                                    color ← spectrum    built into Render3D
```

A Shape3D operator outputs a scene fragment (mesh + default material + identity transform). Downstream operators modify it: Transform changes the matrix, SetMaterial swaps the material, Deformer modifies vertices. SceneMerge combines multiple fragments. Render3D traverses the fragment tree and outputs a GPU texture.

**Pros:**
- ONE new port type (mirrors how GPU textures are the single type for 2D)
- Self-contained operators — Shape3D bundles geometry + material params, matching how existing 2D operators work (Shape has color, size, etc. built in)
- Minimum viable is just **2 operators** (Shape3D + Render3D)
- Fits the operator-chain model — scene fragments flow and get transformed, just like textures
- Incremental — doesn't touch the existing 2D pipeline
- Simple mental model: "make 3D things → combine them → render to texture → do 2D stuff"
- Camera can start as built-in params on Render3D, later become a separate operator

**Cons:**
- The scene fragment is a complex data structure (geometry + material + transform + children)
- Modifying just one aspect (e.g., swapping only the material) requires "unwrapping" the fragment
- Less pure separation of concerns than Option A — geometry and material are coupled in the fragment
- Slightly less expressive than full Unified — still has an explicit Render3D bridge point
- Scene fragment ownership/copying semantics need careful design (does Transform copy or mutate?)

---

## Part 3: Deep Dives

### Audio-Reactive 3D

Audio-reactivity works identically across all three architectures, because audio data already reaches GPU operators through existing port types:

- **Control floats** (beat intensity, envelope, LFO) → wire to any 3D parameter (rotation, scale, emission)
- **Control spreads** (spectrum bands, sequences) → drive per-instance properties
- **GPU textures** (spectrum/waveform from TextureAnalysis) → displacement maps, color maps on materials

| Audio-reactive pattern | Mechanism | Architecture impact |
|---|---|---|
| Beat → rotation/scale | Control float → transform params | Identical in all three |
| Spectrum → color | Texture → material color map | Identical in all three |
| Bass → mesh displacement | Float/texture → deformer amplitude | Identical in all three |
| Beat → particle burst | Control float → emission rate | Identical in all three |
| Spectrum → instancing | Spread → per-instance properties | Identical in all three |

**Conclusion**: The architecture choice does NOT gate audio-reactivity. What matters more is operator *parameter design* — making it obvious and easy to wire audio data into 3D properties. Shape3D should have params like `emission_intensity`, `displacement_amount`, `pulse_scale` that are clearly meant to be driven by audio analysis.

### Data Types: What Flows Through a 3D Wire?

**Option A** needs 3-4 new port types:
```
VIVID_PORT_MESH         — vertex + index GPU buffers
VIVID_PORT_MATERIAL     — shader pipeline + params + texture refs
VIVID_PORT_TRANSFORM    — 4x4 matrix
VIVID_PORT_SCENE        — collection of renderable objects
```

**Option C** needs 1 new port type:
```
VIVID_PORT_GPU_SCENE    — a scene fragment containing:
                          • geometry (vertex/index buffers, topology)
                          • material (render pipeline, bind group, blend mode)
                          • transform (mat4 model matrix)
                          • children (for SceneMerge grouping)
```

**Option B** needs 0 new port types but requires polymorphic/scene-aware port resolution.

At the WebGPU level, a scene fragment contains everything needed for a draw call:

```
SceneFragment {
    WGPUBuffer vertex_buffer        // positions, normals, UVs
    WGPUBuffer index_buffer         // triangle indices
    VertexLayout layout             // attribute descriptions
    PrimitiveTopology topology      // triangles, lines, points
    uint32_t vertex_count, index_count

    WGPURenderPipeline pipeline     // compiled shader
    WGPUBindGroup material_binds    // uniforms + textures
    BlendMode blend
    CullMode cull

    mat4 model_matrix

    SceneFragment* children[]       // for scene composition
    BoundingBox bounds              // for culling
    bool depth_write
}
```

The fragment needs to accommodate multiple geometry types:

| Type | Representation | Rendering |
|---|---|---|
| Triangle mesh | Vertex + index buffers | Standard rasterization |
| Point cloud | Vertex buffer (points) | Point topology |
| Particles | Compute buffer + state | Compute → indirect draw |
| SDF / Raymarched | Fullscreen quad + frag shader | Like current 2D ops, but with depth |
| Instanced mesh | Mesh + instance buffer | Instanced draw call |

### Concrete UX: Audio-Reactive Rotating Cube

**Option A (Separated)** — 7 operators:
```
[Oscillator] → [DrumKick] → [TextureAnalysis] → beat_intensity
                                                     ↓                ↓
[BoxMesh] → [AssignMaterial] → [Transform3D] → [Render3D] → [Bloom] → out
                  ↑                  ↑              ↑
            [PBRMaterial]      rotation.y =      [Camera]
            color ← beat      beat_intensity
```

**Option C (Hybrid)** — 4 operators:
```
[Oscillator] → [DrumKick] → [TextureAnalysis] → beat_intensity
                                                     ↓              ↓
                         [Shape3D (cube)] → [Transform3D] → [Render3D] → [Bloom] → out
                          color ← beat     rotation ← beat
                          (built-in material)          (built-in camera)
```

**Option B (Unified)** — 3-4 operators:
```
[Oscillator] → [DrumKick] → [TextureAnalysis] → beat_intensity
                                                     ↓           ↓
                                  [Shape3D (cube)] → [Render3D] → [Bloom] → out
                                   color ← beat
                                   rotation ← beat
```

Options B and C look nearly identical for this case. The difference is architectural, not user-facing.

---

## Part 4: Implementation Roadmap (Option C as example)

### Phase 0 — WebGPU 3D Infrastructure
Required regardless of architecture choice:
- Depth buffer support in GPU render passes
- Vertex buffer creation and attribute binding utilities
- Vertex + fragment shader compilation for 3D (position, normal, UV attributes)
- mat4 / vec3 / projection matrix math utilities
- Key files: `src/runtime/gpu_operator.h`, `src/runtime/gpu_common.h`

### Phase 1 — Scene Fragment Type + Render3D
- Define `SceneFragment` data structure
- Add `VIVID_PORT_GPU_SCENE` to `src/operator_api/types.h`
- Scene port routing in `src/runtime/scheduler.cpp`
- **Render3D operator**: scene fragment input, built-in camera params, depth-tested render pass, outputs GPU texture

### Phase 2 — First Geometry Operator
- **Shape3D**: Cube, sphere, torus, plane, cylinder. Built-in material params (color, roughness, metallic, emission). Built-in transform params (position, rotation, scale). Outputs scene fragment.
- **Milestone**: `Shape3D → Render3D → existing 2D pipeline` produces a visible 3D scene. Two operators.

### Phase 3 — Scene Composition
- **Transform3D**: Modify scene fragment transform from control signals
- **SceneMerge**: Combine N scene fragments into one tree
- **Light**: Point light, directional light (params or scene element)

### Phase 4 — Advanced Geometry
- **MeshImport**: OBJ/glTF → scene fragment
- **Deformer**: Vertex displacement (noise, audio-driven, wave)
- **Instancer**: Scene fragment + spread data → instanced rendering

### Phase 5 — Particles & Volumetrics
- GPU compute particles → scene fragment
- SDF raymarching → scene fragment (with depth)
- Camera-facing billboards

### Phase 6 — Advanced Rendering
- Full PBR material system with texture map inputs
- Shadow mapping, environment mapping, HDRI
- 3D-aware post-processing (SSAO, depth of field)

---

## Part 5: Summary Comparison

| | A: Separated | B: Unified | C: Hybrid |
|---|---|---|---|
| **Fits Vivid today** | Perfectly | Poorly | Well |
| **New port types** | 3-4 | 0 (but needs polymorphism) | 1 |
| **Min operators for first result** | ~5 | ~3 + infrastructure | 2 |
| **Implementation effort** | Medium | Very large | Medium |
| **Can ship incrementally** | Yes | No | Yes |
| **User-facing verbosity** | High (7 ops for a cube) | Low (3-4 ops) | Low (4 ops) |
| **Creative expressiveness** | Good | Excellent | Very good |
| **Mental model** | Two separate worlds | One unified world | One world, one bridge |
| **Risk to existing pipeline** | None | High | None |
| **Audio-reactivity** | Same as others | Same as others | Same as others |

---

## Sources

- [TouchDesigner SOP Documentation](https://docs.derivative.ca/SOP)
- [TouchDesigner Render TOP](https://docs.derivative.ca/Render_TOP)
- [TouchDesigner Geometry COMP](https://docs.derivative.ca/Geometry_COMP)
- [TouchDesigner POPs (2025)](https://interactiveimmersive.io/blog/touchdesigner-resources/whats-new-in-the-2025-touchdesigner-release/)
- [vvvv 3D Graphics Documentation](https://thegraybook.vvvv.org/reference/libraries/graphics-3d.html)
- [vvvv Rendering Documentation](https://thegraybook.vvvv.org/reference/libraries/3d/rendering.html)
- [VL.Stride / Stride Engine](https://www.stride3d.net/blog/vvvv-releases-VL.Stride/)
- [Max/MSP Jitter OpenGL](https://docs.cycling74.com/max8/vignettes/working_with_opengl_topic)
- [Jitter jit.gl.render](https://docs.cycling74.com/max7/refpages/jit.gl.render)
- [Notch Features: 3D, Lighting & Materials](https://www.notch.one/features/3d-lighting-materials)
- [Notch Node Reference](https://manual.notch.one/0.9.23/en/docs/nodes/)
- [Notch NURA Rendering Architecture](https://manual.notch.one/1.0/en/docs/learning/lighting-and-rendering/nura-rendering-architecture/)
- [Notch Particle System](https://manual.notch.one/1.0/en/docs/reference/nodes/particles/)
