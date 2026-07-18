# ADR-0016: Shaders Are Content, Not Code — a Shader File Is an Operator

Status: accepted (implemented — S1..S6, 2026-07-14)

Date: 2026-07-13

Amends: [ADR-0011](ADR-0011-reboot-product-architecture.md) (the operator/package platform), which
makes **the compiled dylib the only unit of extension**. That is right for operators that are
genuinely code. It is wrong for the ones that are, in truth, a fragment shader.

Decided: a **shader file is an operator**. A `.wgsl`/`.glsl` file carries a JSON header declaring its
name, inputs and params; a directory scan registers **each file as its own operator type**, whose
params become real, wireable graph params. Ten of the current visual operators stop being C++ and
become shader files in a browsable library. A stated **boundary rule** says which ops may not.

## Context

Every visual effect today is a compiled C++ dylib. `app/operators/packages/core-visuals/kaleidoscope.cpp`
is 48 lines: about twenty are an inline WGSL string; the rest is a name, a summary, five
`Param<float>` declarations, and a `fill()` that packs those params into a uniform struct by hand.
`plasma.cpp` (88 lines) and `gradient.cpp` (89) have the same shape — roughly 60% boilerplate, 20%
shader, 15% param declarations. **The C++ contributes nothing but metadata and plumbing.**

The consequences are not cosmetic:

- **Authoring an effect requires a compiler.** To add a plasma variant you write C++, add a CMake
  target, compile a dylib, and ship a binary — to change twenty lines of shader math. That excludes
  every user who is a shader author but not a C++ programmer, which is most of them.
- **A shader cannot declare its own uniforms.** `CustomShader` (`core-visuals/customshader.cpp`) does
  load GLSL from a file param, but with a *hardcoded* contract: exactly four floats named
  `warp`, `hue`, `density`, `glow`. Every shipped demo shader carries a comment block explaining
  which of those four it has **repurposed** — `examples/demos/projects/pulse/pulse_tunnel.glsl` maps
  `u_density` to "ring count" and `u_glow` to "kick brightness". A tunnel and a glitch effect must
  both pretend they want "hue" and "glow". The fixed contract has leaked into the content.
- **There is no library and no way to browse one.** No shipped shaders, no browser — just an OS
  file-open dialog pointed at a `.glsl` the user must already possess.
- **The uniform packing is a silent-corruption trap.** Each op declares `struct U` in its WGSL string
  and hand-packs a matching `float u[8]` in C++. Get the two out of sync and you get garbage on
  screen, not a compile error.

**vivid-classic already solved this.** A `.wgsl` file there carries a JSON header in a leading
comment (`filters/gradient.wgsl`); `parse_wgsl_header()` extracts it; `WgslOperator::collect_params()`
turns the declared params into live, wireable graph params; `scan_shader_operators()` registers each
file as an operator type. Classic shipped **29 shaders** this way, with hot-reload and fork-to-edit.
It is a proven design in this product's own lineage, and the three files worth lifting are
`wgsl_header_parser.cpp`, `data_driven_filter.h` and `operator_registry_scan.cpp`.

## Decision

1. **A shader file is an operator type.** A directory scan parses each `.wgsl`/`.glsl` header and
   registers *that file* as a type in the same `OpRegistry` the dylib operators use. Its declared
   params become the type's params — wireable, mappable, inspectable, persisted, exactly like a C++
   operator's. Shaders therefore appear in the Tab chooser and in `list_operators` **for free**,
   which is also the answer to "how do we browse them": they are not a separate kind of thing
   needing a separate panel.

2. **The header declares; the host generates.** The author writes a JSON object in a leading
   `/*{ … }*/` comment (name, summary, keywords, inputs, params) and a fragment body. Vivid
   **generates** the uniform struct, the bindings, the sampler and the fullscreen vertex stage from
   that declaration and prepends them before compiling. Nothing is asserted, everything is derived —
   which is what makes the hand-packed-uniform bug class *impossible* rather than merely unlikely.
   `inputs: []` is a generator, `["input"]` a filter, `["a","b"]` a compositor.

3. **The boundary rule.** *A shader file is a fullscreen fragment pass over 0..2 input textures.
   Anything requiring custom vertex data, CPU-side asset decode, cross-frame history, or a non-texture
   input stays a compiled operator.* This is stated so that "everything that can be a shader should be
   a shader" is **checkable rather than aspirational**, and so the exceptions fall out by rule rather
   than by special pleading.

4. **Ten operators become shader files**, per that rule: `plasma`, `gradient`, `noise_texture`,
   `kaleidoscope`, `tint`, `transform`, `shape`, `composite`, `displace`, `blur`. **Eleven stay
   compiled**: `feedback` (owns a history texture and copies output→history each frame — cross-frame
   state), `image`/`video`/`webcam` (CPU decode, native capture), `mesh`/`lines`/`vectortext`/
   `shape_grid`/`text` (real vertex buffers, font atlases), `switch` (a selector, not a pass), and
   `output` (the sink).

5. **A library with three tiers**, precedence **user > project > bundled**: shipped shaders in the app
   bundle, user shaders in `~/Library/Application Support/Vivid/shaders/` (beside the existing
   `operators/` and `clones/`), and project-local shaders in `<project>/shaders/`. A user can shadow a
   shipped shader by name — that is an authoring affordance, not an accident. **Fork-to-edit** copies a
   shipped shader into the user dir and hot-swaps the node onto the fork, mirroring the operator
   clone-to-edit flow that already exists (`app/src/app/operator_clone.cpp`).

6. **Compatibility is structural, not bolted on.** A saved project stores a node's **op type name**
   and its params **by name** (`persist.cpp:440`), and mappings target params by name
   (`"node:0.warp"`, `mapping.h`). Because each migrated shader keeps its **type name, param names,
   defaults and ranges**, every old project loads untouched — **no alias table is required**. That
   every value, mapping and wire survives is the migration gate.

## Consequences

- **Positive:** authoring a visual effect stops requiring a compiler; a shader is a file you can drop
  in a folder, share, and fork. The catalog becomes extensible by content. The hand-packed uniform
  trap is designed out. Ten C++ files (~700 lines that were mostly boilerplate) are deleted. And the
  product gains the thing a visuals tool is expected to have: a shader library.

- **No ABI change, deliberately.** Because each shader is its own *type*, its params are known when
  the type is registered — `OpRegistry::Factory` is already an arbitrary `std::function`
  (`gpu/op_runtime.h:61`), and the UI already reads param count and names from the live instance
  (`ui/node_graph.cpp:53`). The alternative model (one `Shader` node whose params change when you
  point it at a different file) would have forced a per-instance-descriptor ABI, host re-sync
  machinery, a persist load reorder, and orphaned-mapping handling. See Alternatives.

- **Cost / risk:** the format is now a compatibility surface — a shipped `.wgsl` is content users
  depend on, so the v1 schema must be got right, and `"passes"`/`"buffers"` are **reserved now**
  (parse-and-reject with a clear message) so v1 files stay valid when multi-pass lands. Changing a
  node's shader means replacing the node rather than re-pointing it, and a *header* edit re-registers
  the type and rebuilds affected nodes (a *body* edit hot-reloads in place) — classic worked exactly
  this way.

- **A pre-existing bug must be fixed first.** `vop_from_name()` (`gpu/visual_graph.cpp:45`) returns
  `VOp::Plasma` for **any** unrecognized name, so Kaleidoscope, Tint and Displace are *already*
  classified as "Plasma" today — and that classification drives generator detection, `set_generator()`,
  and the seeded `master.level → glow` mapping. Every shader node would inherit the bug. It is retired
  (S2) before any migration lands.

- **`blur` is moved with eyes open.** It is single-pass *today* (one 5-tap box blur), so migrating it
  is a zero-regression change — but single-pass is a *limitation*, not a design; a proper separable
  gaussian is two passes. Moving it now freezes today's quality into content, and is only acceptable
  because `"passes"` is reserved in v1 and multi-pass is filed as the next epic. Otherwise "fix the
  blur" would become "change the file format."

- **Deliberately not fixed here:** multi-pass shaders, persistent buffers, and an ISF/Shadertoy
  importer. The header vocabulary is chosen to be ISF-shaped so an importer is later a mechanical
  translation rather than a redesign. (Classic scoped an ISF importer and never built it.)

## Alternatives considered

- **One generic `Shader` node with a file param and dynamic params.** The obvious design, and the one
  we started with. Rejected: the operator ABI collects a node's params **once, at instance creation**
  (`operator_api/operator.h:598`), so a node whose param list changes when you load a different shader
  forces a new per-instance-descriptor ABI (v13), a host re-sync path, a persistence load reorder, and
  a policy for wires and mappings that point at params the new shader doesn't have. Type-per-shader
  obtains the same user-visible outcome with none of that machinery, and gets old-project
  compatibility for free instead of having to engineer it. The one thing it gives up — swapping a
  node's shader in place — is worth less than the complexity it costs.

- **Keep the hardcoded four-uniform `CustomShader` contract and just ship a shader folder.** Cheapest
  possible move. Rejected: it is the status quo's central lie. A shader that wants a "ring count" would
  still have to call it `density`, and the inspector would still show four knobs named for a plasma.

- **A sidecar `.json` next to each shader.** Rejected: shaders are content that gets shared, dropped
  into folders and dragged onto nodes. A second file is a second thing to lose, and the failure mode
  (a shader arriving without its metadata) is silent and common. One self-describing file is the point.

- **Parse the shader's own uniform declarations instead of a header.** Rejected: it means hand-rolling
  a parser for two shader dialects, and it puts param metadata (ranges, labels, enum choices) nowhere —
  a WGSL `struct U` cannot say a param is a 0..360 angle rendered as a knob. The header is the single
  source of truth precisely so the host can *generate* the struct from it.

- **Migrate everything, including the geometry ops.** Rejected as the place the strict line would do
  real damage: `mesh`, `lines`, `vectortext`, `shape_grid` and `text` need vertex buffers, instancing
  and font atlases. A file format expressing those would be a second engine. The boundary rule in
  Decision §3 exists to make that a principle rather than an exception.

## Implementation plan

Each phase is one commit: it builds, `ctest` is green, and it is live-verified through the control
server. No phase leaves the app broken.

### S1 — `shader_meta`: the parser and the layout generator (pure, headless, no wgpu)

New `app/src/operator_api/shader_meta.{h,cpp}` — the one place that understands the format. It lives
in `operator_api/` because both the host (to scan the library) and an operator dylib may need it.

```cpp
struct ShaderParam { std::string name, label, description; int type;   // VIVID_PARAM_*
                     float def, min, max; std::vector<std::string> choices; int display; };
struct ShaderMeta  { int version; std::string name, summary;
                     std::vector<std::string> keywords, inputs;
                     std::vector<ShaderParam> params;
                     std::string body;      // source with the header comment stripped
                     Dialect dialect;       // Wgsl | Glsl (from the file extension)
                     std::string error; };  // non-empty => malformed; the row still exists

ShaderMeta    parse_shader(const std::string& source, Dialect d);
std::string   generate_prelude(const ShaderMeta&);   // uniform struct + bindings + vertex stage
UniformLayout uniform_layout(const ShaderMeta&);     // offsets + size, std140 16-byte alignment
```

Param types map onto the existing ABI for free: `color` → 4 floats + `VIVID_DISPLAY_COLOR`, `point2`
→ `VIVID_DISPLAY_XY_PAD`, `int` + `choices` → `VIVID_PARAM_INT` + choice labels. The inspector gets
rich widgets with **zero new UI code**.

New headless `app/tests/test_shader_meta.cpp`: valid header; no header; malformed JSON; duplicate
param names; unknown param type; color/point2/enum expansion; layout size and 16-byte alignment;
`"passes"` present → parse-and-reject with a clear message.

### S2 — retire the `VOp` classification (must land BEFORE any migration)

Replace the enum-as-classification with facts already on the instance: a node is a **generator iff its
descriptor declares zero texture inputs** (`inst.input_port_count == 0`); the remaining special cases
key on the op **type name** (`"Video"`, `"Output"`). Touches `gpu/visual_graph.cpp:45,228,234` and
`ui/node_graph.cpp:35` (`uniform_owner`). Behaviour-preserving, separately gated.

### S3 — `ShaderOp` and the shader library

- **`app/src/gpu/shader_op.{h,cpp}`** — host-side `struct ShaderOp : OperatorBase, GpuProcessable`,
  constructed from a `ShaderMeta`. Owns its `std::vector<Param<float>>` (reserved, so
  `collect_params()` hands out stable pointers), builds its pipeline from `generate_prelude(meta) +
  meta.body`, packs the uniform buffer via `uniform_layout()`, and binds 0/1/2 input views. Reuses
  `vivid::gpu::create_shader_checked` / `create_pipeline` / `run_pass` from
  `operator_api/gpu_common.h` — the same helpers every package op already uses.
- **`app/src/gpu/shader_library.{h,cpp}`** — `scan()` walks the three-tier search path, parses each
  file, and registers each shader as an operator type via `OpRegistry::register_type(name, factory,
  meta)`, the factory constructing a `ShaderOp` with that shader's `ShaderMeta`. Display name, summary
  and keywords come from the header, so the chooser and `list_operators` describe it properly with no
  extra work. Called from `main.cpp` immediately after the operator-dylib scan (`main.cpp:120`).
- Bundled shaders live in `app/shaders/`, copied into the bundle's `Resources/shaders` (mirroring the
  `PlugIns/` copy at `app/operators/CMakeLists.txt:41`), with a `VIVID_SHADERS_DIR` dev override
  mirroring `VIVID_OPERATORS_DIR`.

Two rules the code must enforce, both learned the hard way in this codebase:

- **A malformed shader yields a row WITH an error, never a vanished row.** The catalog must not lie —
  the same principle as the chooser's disabled-but-visible rows.
- **A failed reload keeps the last-good pipeline.** Saving a syntax error mid-performance must never
  black out a live output. Where there was never a good pipeline: a generator renders black, a filter
  passes its input through. Never render garbage.

### S4 — surface it

Tab chooser rows with a `SHADER` badge and the header's keywords as search fodder; the node card
titled from the shader's display name. Hot-reload by mtime poll (~4 Hz, off the render hot path):
a **body** edit rebuilds the pipeline in place, a **header** edit re-registers the type and rebuilds
affected nodes, preserving param values by name. `list_shaders` + `reload_shaders` on the control
server (mirroring `list_operators`, `cli/control_handlers_introspection.cpp:63`) and in
`mcp/vivid_mcp.py`. **Fork-to-edit**, mirroring `clone_operator()`.

### S5 — the migration, in waves (one commit each)

Byte-identical shader bodies; identical type names, param names, defaults and ranges.

- **5a** generators: `plasma`, `gradient`, `noise_texture`
- **5b** filters: `kaleidoscope`, `tint`, `transform`, `shape`
- **5c** two-input: `composite`, `displace` — and fix Composite's `mode` from a bare
  `Param<float> mode{0..1}` (`composite.cpp:49`) into a real enum with choice labels. A migration is
  when that kind of thing gets fixed.
- **5d** `blur` — only with `"passes"` reserved (see Consequences).

Each wave deletes the `.cpp` and its `add_vivid_operator(...)` line. `customshader` **stays** as the
headerless-GLSL loader, so the four existing demo projects keep working with zero edits.

### S6 — polish

Node-card error surface (red accent + first error line; full text in the inspector), the authoring
guide `docs/shaders.md`, and `app/src/gpu/CLAUDE.md` updated to describe the new substrate.

## Verification

Per phase: build, `ctest` (38/38 today), then live through the control server on `127.0.0.1:9876`.

The gates that matter, in order:

1. **Compatibility (S5, non-negotiable).** A pre-migration project loads with **every param value,
   every mapping (`"node:0.warp"`) and every wire intact**, and renders identically to the
   pre-migration build. `app/tests/test_persist_chain_migration.cpp` is the existing precedent; extend
   it with an old-type-name fixture.
2. **Headless (S1).** `test_shader_meta` covers the parser and the generated uniform layout with no GPU.
3. **The money shot (S3/S4).** Drop a `.wgsl` into the user shaders dir → it appears in Tab → spawn it
   → it renders, with its **own** declared params, wireable from audio.
4. **Live-safe failure (S3).** Break a running shader's source mid-edit: the last-good pipeline holds,
   the node shows the error, the output never goes black.
5. **Round-trip.** Save and reload a project using a bundled shader, a user shader, and a
   project-local one.
