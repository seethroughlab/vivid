# Vivid ISF Importer — Implementation Plan

## High-Level Goal

Add first-class support for [Interactive Shader Format (ISF)](https://isf.video/) to Vivid. A user should be able to drop an ISF `.fs` file into Vivid and have it appear as a working filter operator, with INPUTS mapped to Vivid Control ports and IMAGE inputs mapped to GPU texture ports. The strategic prize is access to the existing isf.video community library — hundreds of well-tested real-time shaders — without writing any of them ourselves.

A secondary goal: do something other ISF hosts can't, by routing ISF's `audio` and `audioFFT` input types from Vivid's audio domain analysis outputs. This is the kind of cross-domain wire connection that Vivid's three-domain graph makes natural and that single-domain hosts like VDMX, CoGe, or Resolume can only fake.

## Non-Goals (v1)

- We are **not** trying to host every ISF shader on isf.video. A meaningful subset (target: 70%+ of the top 100) is success.
- We are **not** building a UI for browsing/searching isf.video at runtime. CLI import + drop-in folder support is enough.
- We are **not** supporting `BUFFER_PRECISION`, `IMPORTED` textures, or exotic pass flags in v1. These can come later.
- We are **not** writing our own GLSL→WGSL translator. We integrate naga.

## Key Technical Decisions

1. **Translation via naga, exposed to C++ via a small Rust wrapper crate.** Naga's GLSL frontend + WGSL backend in one pass gives better error messages than the glslang→SPIR-V→Tint path. We accept the Rust build dependency. Integration via [Corrosion](https://github.com/corrosion-rs/corrosion) (CMake↔Cargo bridge).
2. **A preprocessor step runs before naga.** ISF shaders use C-preprocessor directives, embedded JSON manifests, and ISF-specific built-ins (`RENDERSIZE`, `TIME`, `IMG_PIXEL`, etc.) that naga won't accept as-is. We need a real cpp pass plus our own ISF-specific macro injection.
3. **A WGSL post-processing step patches naga output for WebGPU bind-group conventions.** ISF assumes implicit samplers; WebGPU wants explicit ones. Easier to patch the WGSL output than to massage the GLSL input.
4. **ISF support lives as an operator type (`GpuProcessable`), not as an extension to `WgslFilterBase`.** `WgslFilterBase` generates the WGSL preamble and bakes in a specific uniform struct and bind group layout — both fundamentally incompatible with ISF's model (naga generates its own preamble, ISF has a different uniform contract). The shared machinery worth reusing (mtime polling, pipeline RAII swap) is trivial to duplicate. `WgslFilterBase` stays clean for native-authored filters; `IsfFilter` is a fresh `GpuProcessable` start with no shared assumptions.
5. **Audio routing reuses existing analysis outputs** (FFT, RMS, peak) — ISF `audioFFT` becomes a 1D texture sourced from a Vivid audio operator. No new audio plumbing.

## Architecture Overview

```
.fs file on disk
    │
    ▼
IsfPreprocessor::preprocess()
    ├── Strip + parse /*{ ... }*/ manifest → IsfManifest  (nlohmann/json)
    ├── C preprocessor expansion            (mcpp static lib via FetchContent)
    ├── Inject GLSL built-in preamble       (RENDERSIZE, TIME, etc.)
    └── Rewrite IMG_* macros                (GLSL helper functions)
    │
    ▼ (preprocessed GLSL)
isf_translate_glsl()                        (Rust crate: naga GLSL→WGSL)
    │
    ▼ (raw naga WGSL)
wgsl_patch()
    ├── Inject paired sampler declarations
    ├── Replace naga uniform block with Vivid's IsfUniforms layout
    └── Apply Y-flip on isf_FragNormCoord
    │
    ▼ (Vivid-compatible WGSL)
wgpuDeviceCreateShaderModule()              (Dawn/WebGPU)
    │
    ▼
IsfFilter::process_gpu()                   (GpuProcessable, per-frame)
```

## Existing Patterns to Reuse

| Pattern | Where |
|---------|-------|
| `Param<FilePath>` + reload-on-change | `operators/gpu/texture_loader/texture_loader.cpp` |
| `VIVID_FILE_DROP` macro | `operators/gpu/texture_loader/texture_loader.cpp` |
| `gpu::ShaderHandle`, `PipelineHandle`, etc. (RAII) | `src/operator_api/gpu_operator.h` |
| `vivid::gpu::run_pass()`, `create_shader_checked()` | `src/operator_api/gpu_common.h` |
| mtime hot-reload (stat every 30 frames) | `src/operator_api/wgsl_filter.h` |
| nlohmann/json manifest parsing | `src/runtime/gpu/wgsl_header_parser.cpp` |
| `add_vivid_operator()` CMake macro | `cmake/operators.cmake` |
| Test partition registration | `cmake/tests.cmake` |

## Files to Create / Modify

| File | Action |
|------|--------|
| `cmake/rust.cmake` | **Create** — Corrosion + `vivid-shader-xlate` import |
| `CMakeLists.txt` | **Edit** — add `include(cmake/rust.cmake)` after dependencies |
| `cmake/dependencies.cmake` | **Edit** — add mcpp FetchContent |
| `cmake/operators.cmake` | **Edit** — add `isf_filter` operator registration |
| `deps/vivid-shader-xlate/` | **Create** — Rust crate (Cargo.toml, src/lib.rs) |
| `operators/gpu/isf_filter/isf_manifest.h` | **Create** |
| `operators/gpu/isf_filter/shader_xlate.h` | **Create** — C++ FFI wrapper around Rust crate |
| `operators/gpu/isf_filter/isf_preprocessor.h/.cpp` | **Create** |
| `operators/gpu/isf_filter/wgsl_patch.h/.cpp` | **Create** |
| `operators/gpu/isf_filter/isf_filter.h/.cpp` | **Create** |
| `operators/gpu/isf_filter/factory_presets.json` | **Create** |
| `tests/isf/translate_smoke.cpp` | **Create** |
| `tests/isf/preprocessor_test.cpp` | **Create** |
| `tests/isf/corpus/` | **Create** — ~25 MIT/CC0 licensed shaders |
| `tests/isf/corpus_test.cpp` | **Create** |
| `tests/CMakeLists.txt` | **Edit** — register new test targets (new partition 35) |
| `docs/ISF-SUPPORT.md` | **Create** |
| `docs/GETTING-STARTED.md` | **Edit** — add ISF section |
| `src/cli/mcp_server.cpp` | **Edit** — add `isf_check` / `isf_import` MCP tool handlers |

---

## Stages

Each stage has an objective, work items, deliverable, and validation criteria. Don't advance until the validation passes.

---

### Stage 1: Spec & Subset Decisions

**Objective:** Lock down what we will and won't support in v1, in writing.

**Work items:**
- Read the ISF v2 spec end to end.
- Survey the top ~50 shaders on isf.video and categorize them by which ISF features they use (input types, pass count, persistent buffers, custom functions, GLSL features).
- **Critical spike:** throw 20 representative shaders directly at `naga::front::glsl::Frontend` with no preprocessing (via a standalone Rust binary in `deps/vivid-shader-xlate/` before wiring into CMake) to measure raw coverage. If <50% parse, the preprocessor scope must expand significantly, or we evaluate the glslang→SPIR-V→Tint path as a fallback before committing to Stage 2.
- Write `docs/ISF-SUPPORT.md` listing supported INPUT types, supported PASS configurations, supported GLSL features, and an explicit unsupported list with rationale per item.

**Deliverable:** `docs/ISF-SUPPORT.md` checked in, naga coverage number in hand.

**Validation:** A human reads it and can answer "will my shader work?" without running anything.

---

### Stage 2: GLSL→WGSL Translation Wrapper

**Objective:** Have a callable C function `vivid_isf_translate(const char* glsl, char** wgsl_out, char** error_out)` that uses naga internally.

**New Cargo crate:** `deps/vivid-shader-xlate/`
```
deps/vivid-shader-xlate/
  Cargo.toml       (naga = "22", crate-type = ["staticlib"])
  src/lib.rs
```

**C API surface (in `lib.rs`):**
```rust
#[no_mangle] pub extern "C"
fn vivid_isf_translate(glsl: *const c_char, wgsl_out: *mut *mut c_char, error_out: *mut *mut c_char) -> c_int;

#[no_mangle] pub extern "C"
fn vivid_isf_free_string(s: *mut c_char);
```

**CMake integration:**

New file `cmake/rust.cmake`:
```cmake
FetchContent_Declare(Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Corrosion)
corrosion_import_crate(MANIFEST_PATH deps/vivid-shader-xlate/Cargo.toml CRATES vivid_shader_xlate)
```

In top-level `CMakeLists.txt`: add `include(cmake/rust.cmake)` after `include(cmake/dependencies.cmake)`.

**C++ wrapper header:** `operators/gpu/isf_filter/shader_xlate.h`
```cpp
// Thin RAII wrapper around the C FFI
std::string isf_translate_glsl(const std::string& glsl, std::string& error_out);
```

**Work items:**
- Create `deps/vivid-shader-xlate/` as above.
- Add Corrosion to `cmake/rust.cmake`; include from top-level CMakeLists.
- Write `tests/isf/translate_smoke.cpp` — three trivial GLSL fragments, assert translation succeeds.

**Deliverable:** Static library linked into isf_filter.dylib; smoke test passes in CI.

**Validation:** `cmake --build build --target isf_filter` succeeds. Smoke test compiles and runs.

> **CI note:** Cache `~/.cargo` and `target/` in GitHub Actions. Corrosion + naga compile is ~60s uncached, ~5s cached.

---

### Stage 3: ISF Preprocessor

**Objective:** Transform a raw ISF `.fs` file into GLSL that naga will accept, plus a parsed manifest struct.

**Data structures** (`operators/gpu/isf_filter/isf_manifest.h`):
```cpp
struct IsfInput {
    std::string name, label;
    enum class Type { Float, Bool, Long, Point2D, Color, Image, Audio, AudioFFT, Event } type;
    float default_val = 0, min = 0, max = 1;
    std::vector<std::string> values;  // for Long (enum labels)
};

struct IsfPass {
    std::string target;
    bool persistent = false, is_float = false;
    int width = 0, height = 0;  // 0 = inherit output size
};

struct IsfManifest {
    std::string name, description, credit, vsn;
    std::vector<IsfInput> inputs;
    std::vector<IsfPass>  passes;   // empty = single pass
    std::vector<std::string> categories;
};
```

**`IsfPreprocessor::preprocess()` steps (in order):**
1. Extract and strip `/*{ ... }*/` from source → parse with `nlohmann::json` → populate `IsfManifest`.
2. Run C preprocessor expansion via `mcpp` (FetchContent static lib, added to `cmake/dependencies.cmake`). Handles `#ifdef`/`#define`/`#include` that naga won't expand.
3. Inject GLSL built-in preamble (prepended before user source):
   ```glsl
   uniform vec2  RENDERSIZE;
   uniform float TIME, TIMEDELTA;
   uniform vec4  DATE;
   uniform int   FRAMEINDEX, PASSINDEX;
   in vec2 isf_FragNormCoord;
   ```
4. Inject `IMG_*` macro rewrites as GLSL helper functions:
   ```glsl
   #define IMG_SIZE(img)             vec2(textureSize(img, 0))
   #define IMG_PIXEL(img, p)         texelFetch(img, ivec2(p), 0)
   #define IMG_NORM_PIXEL(img, p)    texture(img, p)
   #define IMG_THIS_PIXEL(img)       texelFetch(img, ivec2(gl_FragCoord.xy), 0)
   #define IMG_THIS_NORM_PIXEL(img)  texture(img, isf_FragNormCoord)
   ```
5. **Y-flip decision:** flip on output — the WGSL post-processor (Stage 4) inverts Y on `isf_FragNormCoord` rather than touching GLSL semantics.

**Deliverable:** `IsfPreprocessor` C++ class with `preprocess(const std::string& isf_source) -> std::pair<IsfManifest, std::string>`. Companion unit tests in `tests/isf/preprocessor_test.cpp` covering manifest parser and each macro rewrite.

**Validation:** Twenty representative ISF shaders (chosen during Stage 1) preprocess without error and the output is syntactically valid GLSL (verified by running them through naga even if translation later fails). Manifest fields round-trip.

---

### Stage 4: WGSL Post-Processor & Bind Group Patching

**Objective:** Naga output is valid WGSL but won't have the sampler declarations and bind group layouts that Vivid's pipeline expects. Patch it.

**C++ uniform struct** (must match what gets uploaded each frame):
```cpp
struct IsfUniforms {
    float resolution[2];
    float time;
    float timedelta;
    uint32_t frameindex;
    uint32_t passindex;
    float _pad[2];
};
```

**`wgsl_patch(wgsl, manifest)` steps** (`operators/gpu/isf_filter/wgsl_patch.h/.cpp`):
1. Scan naga-emitted bind groups for texture bindings → inject paired `var<..> sampler_N: sampler;` declarations at matching group/binding indices.
2. Replace naga's generated uniform block for ISF built-ins with Vivid's `IsfUniforms` layout at `@group(0) @binding(0)`.
3. Apply Y-flip: wrap the `isf_FragNormCoord` computation to invert Y.
4. Prepend `// generated from ISF — do not edit` header.

**Deliverable:** `wgsl_patch(const std::string& wgsl, const IsfManifest& manifest) -> std::string`.

**Validation:** Translated + patched WGSL compiles cleanly when handed to Dawn's `wgpuDeviceCreateShaderModule`. Verified for the same twenty-shader corpus from Stage 3.

> **Risk:** If bind-group patching proves too brittle against naga's output, add a second pass that rewrites texture declarations on the GLSL *before* naga (more predictable surface to patch).

---

### Stage 5: The ISF Operator

**Objective:** A new operator type that loads `.fs` files, runs the translation pipeline, and renders.

**Class skeleton** (`operators/gpu/isf_filter/isf_filter.h/.cpp`):
```cpp
struct IsfFilter : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "IsfFilter";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file{"file"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_gpu(const VividGpuContext* ctx) override;

private:
    std::string  loaded_path_;
    IsfManifest  manifest_;
    std::string  error_msg_;

    struct PassResources {
        gpu::PipelineHandle   pipeline;
        gpu::ShaderHandle     shader;
        gpu::TextureHandle    target;       // null for final pass
        gpu::TextureViewHandle target_view;
    };
    std::vector<PassResources> passes_;
    gpu::BufferHandle          uniform_buf_;
    gpu::BindGroupHandle       bind_group_;

    // Hot-reload (mtime poll every 30 frames — same pattern as WgslFilterBase)
    time_t last_mtime_ = 0;
    int    reload_counter_ = 0;

    bool load_isf(const VividGpuContext* ctx, const std::string& path);
    void check_hot_reload(const VividGpuContext* ctx);
};
```

**`collect_ports()` — dynamic port layout from manifest:**
- Each `IsfInput` of type `Image`: add `VIVID_PORT_TEXTURE` input named after `input.name`.
- Each `IsfInput` of type `Audio`/`AudioFFT`: add `VIVID_PORT_TEXTURE` input (1D texture from audio analysis operator — canonical format: 512-sample mono `r16float`).
- Each remaining `IsfInput` (Float/Bool/Long/Point2D/Color/Event): expose as a `Param<float>` (or typed param) and `VIVID_PORT_SCALAR` Control input.
- Always: one `VIVID_PORT_TEXTURE` output.

**`process_gpu()` logic:**
1. If `file.str_value != loaded_path_`: call `load_isf()`.
2. Hot-reload check (every 30 frames: `stat()` the `.fs` for mtime change → re-run full pipeline if changed; keep old pipeline on failure).
3. Fill and upload `IsfUniforms` via `wgpuQueueWriteBuffer`.
4. For each pass: bind inputs, call `vivid::gpu::run_pass()`.
5. On error: set `error_msg_`, skip render.

**CMake registration** (in `cmake/operators.cmake`):
```cmake
add_vivid_operator(isf_filter
    operators/gpu/isf_filter/isf_filter.cpp
    operators/gpu/isf_filter/isf_preprocessor.cpp
    operators/gpu/isf_filter/wgsl_patch.cpp
    CODEGEN
    EXTRA_LIBS webgpu vivid_shader_xlate nlohmann_json::nlohmann_json
    FACTORY_PRESETS operators/gpu/isf_filter/factory_presets.json)
```

**File-drop registration:**
```cpp
static const char* kIsfExts[] = {".fs", ".frag"};
static const VividFileDropHandlerDescriptor kFileDrops[] = {{
    "Load ISF Shader", kIsfExts, 2, "file", 100, "Import ISF .fs shader"
}};
VIVID_FILE_DROP(kFileDrops)
```

**Deliverable:** The `ISF` operator appears in the operator menu. Loading any of the twenty test shaders produces visible, correct output.

**Validation:** Visual diff against reference renders for the twenty-shader corpus, with fixed inputs and a fixed frame index. Acceptance threshold: 90%+ pixel match (some color-space drift is expected).

---

### Stage 6: Multi-Pass Support

**Objective:** ISF shaders with `PASSES` declared in their manifest render correctly.

**Work items:**
- Parse `PASSES` from the manifest (already in `IsfManifest::passes`).
- Allocate intermediate render targets in `load_isf()`: one `WGPUTexture` per pass with a non-empty `target` name. `PERSISTENT` textures survive between frames (only release/reallocate on resolution change). `FLOAT` → use a float texture format.
- Per-frame: iterate `passes_` in order; for pass N, bind pass N-1's output (or the initial input texture) as the `vInput` sampler.
- Update `IsfUniforms::passindex` before each `run_pass()` call.

**Deliverable:** Multi-pass ISF shaders (e.g., bloom, feedback effects) render correctly.

**Validation:** ~5 known multi-pass shaders from isf.video render correctly. Visual diff against reference.

---

### Stage 7: CLI Tooling

**Objective:** Two commands that make ISF authoring and vetting workflow-friendly.

**Work items:**
- `vivid isf-check <path>` — runs preprocess + translate + post-process, reports success or human-readable error. Exit code reflects status. Doesn't render.
- `vivid isf-import <path> [--name <name>]` — runs full pipeline, writes resulting WGSL + Vivid-flavored JSON manifest to `~/Library/Application Support/Vivid/isf/` on macOS.
- Add MCP tool wrappers for both (`isf_check`, `isf_import`) in `src/cli/mcp_server.cpp` following the existing handler pattern.
- Add ISF section to `docs/GETTING-STARTED.md`.

**Deliverable:** Both commands work and are documented.

**Validation:** Run `vivid isf-check` against the full twenty-shader corpus and confirm output matches expectations (passes pass, fails fail with informative messages).

---

### Stage 8: Compatibility Corpus & CI

**Objective:** Catch regressions in the translation pipeline before they ship.

**Work items:**
- Vendor ~25 ISF shaders into `tests/isf/corpus/` (check licenses; isf.video shaders are mostly MIT/CC0 but verify per-shader).
- Build a test harness (`tests/isf/corpus_test.cpp`) that for each shader: preprocesses, translates, post-processes, compiles, renders one frame at fixed inputs, compares against a reference PNG snapshot in `tests/isf/references/`.
- Register as **test partition 35** in `cmake/tests.cmake` (slots between the existing 30 and 40 partitions).
- Add to GitHub Actions smoke workflow.
- Add a `vivid isf-regen-refs` CLI subcommand to regenerate reference renders.
- Document how to add a new shader to the corpus (two-command process).

**Deliverable:** CI runs the corpus on every PR. Adding a new test shader is a documented two-command process.

**Validation:** Intentionally break the preprocessor in a PR and confirm CI catches it.

---

### Stage 9 (Optional): Ship a Pack

**Objective:** Make the killer feature accessible to users who don't want to install Rust.

**Work items:**
- Curate a pack of ~100-200 high-quality ISF shaders that translate cleanly.
- Pre-translate them all offline. The shipped artifact is WGSL + Vivid JSON manifests, no `.fs` files.
- Publish as `vivid-isf` package using the existing package template.
- README explains: "Install this pack and you get N shaders. To import your own `.fs` files at runtime, you also need the Rust toolchain installed."

**Deliverable:** `vivid-isf` package published, installable via `vivid link`.

**Validation:** Fresh install on a clean machine without Rust can still use the bundled shaders.

---

## Risks & Open Questions

| Risk | Mitigation |
|------|-----------|
| Naga GLSL coverage <50% | Spike this in Stage 1 before Stage 2 locks. If coverage is low, evaluate glslang→SPIR-V→Tint as fallback. |
| Sampler binding model mismatch | Stage 4 is the firewall. If bind-group patching is too brittle, add a pre-naga texture declaration rewriter on the GLSL side (more predictable surface). |
| Color space drift | Decide once in Stage 3: ISF assumes sRGB-ish. Insert `pow(color, vec4(2.2))` in preamble if there's a mismatch with Vivid's linear pipeline. Document in `docs/ISF-SUPPORT.md`. |
| `audio`/`audioFFT` semantics | Define canonical format (512-sample mono `r16float` 1D texture) in Stage 5; document in `docs/ISF-SUPPORT.md`. Vivid's audio analysis operators must emit this format. |
| Rust build time in CI | Cache `~/.cargo` and `target/` in CI. ~60s uncached, ~5s cached. |
| Performance antipatterns | Flag in `vivid isf-check` output; not our problem to fix in v1. |

## References

- ISF spec: <https://github.com/mrRay/ISF_Spec>
- Shader library: <https://editor.isf.video/shaders>
- naga: <https://github.com/gfx-rs/wgpu/tree/trunk/naga>
- Corrosion (CMake↔Cargo): <https://github.com/corrosion-rs/corrosion>
- Existing Vivid filter framework: `filters/` + `src/operator_api/wgsl_filter.h`
- Existing operator contract: `docs/ARCHITECTURE.md`
- File-based operator reference: `operators/gpu/texture_loader/texture_loader.cpp`
- GPU RAII handles: `src/operator_api/gpu_operator.h`
- GPU helpers: `src/operator_api/gpu_common.h`
- Package template (for Stage 9): <https://github.com/seethroughlab/vivid-package-template>

## Estimated Effort

Solo, focused: ~4 weeks for Stages 1–7, plus open-ended polish on the corpus. Stage 8 is a few more days. Stage 9 is dependent on translation-pass rate against the broader library.

Most of the risk is in Stage 3 (preprocessor); most of the leverage is in Stage 5 (the operator); most of the long-term value is in Stage 8 (the test corpus, which is what lets you keep adding shaders without breaking old ones).
