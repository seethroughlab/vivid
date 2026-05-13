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
4. **ISF support lives as an operator type, not as an extension to the existing WGSL filter framework.** Filters are authored against our native contract; ISF operators are imported foreign content. Conflating them muddies the model.
5. **Audio routing reuses existing analysis outputs** (FFT, RMS, peak) — ISF `audioFFT` becomes a 1D texture sourced from a Vivid audio operator. No new audio plumbing.

## Stages

Each stage has an objective, work items, deliverable, and validation criteria. Don't advance until the validation passes.

---

### Stage 1: Spec & Subset Decisions

**Objective:** Lock down what we will and won't support in v1, in writing.

**Work items:**
- Read the ISF v2 spec end to end.
- Survey the top ~50 shaders on isf.video and categorize them by which ISF features they use (input types, pass count, persistent buffers, custom functions, GLSL features).
- Write `docs/ISF-SUPPORT.md` listing supported INPUT types, supported PASS configurations, supported GLSL features, and an explicit unsupported list with rationale per item.

**Deliverable:** `docs/ISF-SUPPORT.md` checked in.

**Validation:** A human reads it and can answer "will my shader work?" without running anything.

---

### Stage 2: GLSL→WGSL Translation Wrapper

**Objective:** Have a callable C function `vivid_isf_translate(const char* glsl, char** wgsl_out, char** error_out)` that uses naga internally.

**Work items:**
- Create `deps/vivid-shader-xlate/` as a new Cargo crate.
- Implement `lib.rs` wrapping `naga::front::glsl::Frontend` → `naga::valid::Validator` → `naga::back::wgsl::write_string`. Expose a `#[no_mangle] extern "C"` entry point that takes a GLSL string and returns either a heap-allocated WGSL string or a heap-allocated error string. Provide a `vivid_isf_free_string(char*)` for the caller.
- Add Corrosion to the top-level `CMakeLists.txt` and pull the crate in as a static library.
- Write a minimal C++ test harness in `tests/isf/translate_smoke.cpp` that hard-codes three trivial GLSL fragments and confirms translation succeeds.

**Deliverable:** Static library linked into the main vivid binary; smoke test passes in CI.

**Validation:** `cmake --build build` succeeds on macOS. Smoke test compiles and runs.

---

### Stage 3: ISF Preprocessor

**Objective:** Transform a raw ISF `.fs` file into GLSL that naga will accept, plus a parsed manifest struct.

**Work items:**
- Parse the manifest. ISF embeds JSON as a `/*{ ... }*/` comment at the top of the file. Strip it, parse with nlohmann/json, populate an `IsfManifest` struct (already a dependency).
- Run a C preprocessor pass on the remaining source. Pull in `mcpp` or equivalent — many ISF shaders use `#ifdef`/`#define`/`#include` that naga won't expand.
- Inject a preamble containing GLSL declarations for ISF built-ins (`uniform vec2 RENDERSIZE; uniform float TIME; uniform float TIMEDELTA; uniform vec4 DATE; uniform int FRAMEINDEX; uniform int PASSINDEX;` plus `in vec2 isf_FragNormCoord;`).
- Rewrite ISF texture macros. `IMG_PIXEL(img, p)`, `IMG_NORM_PIXEL(img, p)`, `IMG_THIS_PIXEL(img)`, `IMG_THIS_NORM_PIXEL(img)`, `IMG_SIZE(img)` need to become legal GLSL that lowers to texture sampling. Either implement as GLSL functions in the preamble (preferred) or as a textual substitution pass.
- Handle Y-flip. ISF inherits GL's bottom-left origin in some places. Decide once whether to flip on input or output and document the decision.

**Deliverable:** A `IsfPreprocessor` C++ class with `preprocess(const std::string& isf_source) -> std::pair<IsfManifest, std::string>`. Companion unit tests covering the manifest parser and each macro rewrite.

**Validation:** Twenty representative ISF shaders (chosen during Stage 1) preprocess without error and the output is syntactically valid GLSL (verified by running them through naga even if translation later fails). Manifest fields round-trip.

---

### Stage 4: WGSL Post-Processor & Bind Group Patching

**Objective:** Naga output is valid WGSL but won't have the sampler declarations and bind group layouts that Vivid's pipeline expects. Patch it.

**Work items:**
- Inspect naga's emitted bind groups for textures. Inject corresponding `sampler` declarations bound to the matching group/binding indices.
- Inject Vivid's standard uniform block layout for `RENDERSIZE`, `TIME`, etc. — these need to match the layout our Dawn pipeline binds.
- Add a `// generated from ISF — do not edit` header for traceability.

**Deliverable:** A `wgsl_post_process(const std::string& wgsl, const IsfManifest& manifest) -> std::string` function.

**Validation:** Translated + patched WGSL compiles cleanly when handed to Dawn's `wgpuDeviceCreateShaderModule`. Verified for the same twenty-shader corpus from Stage 3.

---

### Stage 5: The ISF Operator

**Objective:** A new operator type that loads `.fs` files, runs the translation pipeline, and renders.

**Work items:**
- Add `IsfOperator` to `operators/`. Constructor takes a path to a `.fs` file.
- On load: read file → preprocess → translate → post-process → compile WGSL → cache compiled module.
- Map manifest INPUTS to Vivid Control input ports. `float`/`bool`/`long`/`point2D`/`color`/`event` → Control. `image` → GPU texture input port.
- Map `audio` and `audioFFT` input types to texture inputs that expect a 1D texture from an audio analysis operator. Document the expected format in `docs/ISF-SUPPORT.md`.
- Per-frame execution: bind uniforms (RENDERSIZE from current render target, TIME from clock, etc.), bind input textures, bind sampler, dispatch.
- Hook into the existing filter hot-reload watcher so editing the source file re-runs the pipeline.

**Deliverable:** The `ISF` operator appears in the operator menu. Loading any of the twenty test shaders produces visible, correct output.

**Validation:** Visual diff against reference renders for the twenty-shader corpus, with fixed inputs and a fixed frame index. Acceptance threshold: 90%+ pixel match (some color-space drift is expected).

---

### Stage 6: Multi-Pass Support

**Objective:** ISF shaders with `PASSES` declared in their manifest render correctly.

**Work items:**
- Parse `PASSES` from the manifest, including per-pass `TARGET`, `PERSISTENT`, and `FLOAT` flags.
- Allocate intermediate render targets. `PERSISTENT` means the texture survives between frames (maps onto Vivid's existing feedback machinery). `FLOAT` means use a float texture format.
- Adjust the per-frame execution to run each pass in order, binding the previous pass's output as input where the shader references it by `TARGET` name.
- Update `PASSINDEX` uniform per pass.

**Deliverable:** Multi-pass ISF shaders (e.g., bloom, feedback effects) render correctly.

**Validation:** A curated set of ~5 known multi-pass shaders from isf.video render correctly. Visual diff against reference.

---

### Stage 7: CLI Tooling

**Objective:** Two commands that make ISF authoring and vetting workflow-friendly.

**Work items:**
- `vivid isf-check <path>` — runs preprocess + translate + post-process, reports success or human-readable error. Exit code reflects status. Don't actually render.
- `vivid isf-import <path> [--name <name>]` — runs the full pipeline and writes the resulting WGSL + a small Vivid-flavored JSON manifest into the user's local ISF library directory (TBD location, suggest `~/Library/Application Support/Vivid/isf/` on macOS).
- Add MCP tool wrappers for both so they're scriptable from agents.

**Deliverable:** Both commands work; both are documented in `docs/GETTING-STARTED.md` under a new ISF section.

**Validation:** Run `vivid isf-check` against the full twenty-shader corpus and confirm output matches expectations (passes pass, fails fail with informative messages).

---

### Stage 8: Compatibility Corpus & CI

**Objective:** Catch regressions in the translation pipeline before they ship.

**Work items:**
- Vendor ~25 ISF shaders into `tests/isf/corpus/` (check licenses; isf.video shaders are mostly MIT/CC0 but verify per-shader).
- Build a test harness that for each shader: preprocesses, translates, post-processes, compiles, renders one frame at fixed inputs, compares against a reference PNG snapshot.
- Add to GitHub Actions smoke workflow.
- Document how to add a new shader to the corpus and how to regenerate reference renders.

**Deliverable:** CI runs the corpus on every PR. Adding a new test shader is a documented two-command process.

**Validation:** Intentionally break the preprocessor in a PR and confirm CI catches it.

---

### Stage 9 (Optional): Ship a Pack

**Objective:** Make the killer feature accessible to users who don't want to install Rust.

**Work items:**
- Curate a pack of ~100-200 high-quality ISF shaders that translate cleanly.
- Pre-translate them all offline. The shipped artifact is WGSL + Vivid JSON manifests, no `.fs` files.
- Publish as `vivid-isf` package using the existing package template.
- README explains: "Install this pack and you get N shaders. To import your own `.fs` files at runtime, you also need the Rust toolchain installed (we call out to it via `vivid isf-import`)."

**Deliverable:** `vivid-isf` package published, installable via `vivid link`.

**Validation:** Fresh install on a clean machine without Rust can still use the bundled shaders.

---

## Risks & Open Questions

- **Naga GLSL frontend coverage.** Before committing to naga, throw the twenty Stage-1 shaders directly at `naga::front::glsl::Frontend` with no preprocessing and see how many parse. If fewer than half do, the pre-processor will need to be much more aggressive, or we should evaluate the glslang→SPIR-V→Tint path as a fallback. **Action: spike this in Stage 1, before Stage 2.**
- **Texture binding model mismatch.** WebGPU's explicit sampler binding versus ISF/GL's implicit one is the biggest source of fiddliness. Stage 4 is where this gets resolved; if it turns out to be more complex than expected, may need a redesign of how we emit bind groups.
- **Color space.** ISF doesn't specify a color space; most shaders assume sRGB-ish. Vivid's pipeline color space needs to be confirmed and documented. May need a `vivid-isf` standard preamble that does sRGB↔linear conversion if there's a mismatch.
- **`audio` and `audioFFT` semantics.** ISF's spec for these is loose. Need to pick a canonical sample count, sample rate, and texture format for audio buffers and document it. This becomes part of the Vivid–ISF contract.
- **Performance.** Some isf.video shaders are written without performance in mind (lots of branching, expensive loops). Not our problem in v1 but worth flagging in the import process — `vivid isf-check` could warn on obvious perf antipatterns.

## References

- ISF spec: <https://github.com/mrRay/ISF_Spec>
- Shader library: <https://editor.isf.video/shaders>
- naga: <https://github.com/gfx-rs/wgpu/tree/trunk/naga>
- Corrosion (CMake↔Cargo): <https://github.com/corrosion-rs/corrosion>
- Existing Vivid filter framework: see `filters/` in the vivid repo
- Existing operator contract: see `docs/ARCHITECTURE.md` in the vivid repo
- Package template (for Stage 9): <https://github.com/seethroughlab/vivid-package-template>

## Estimated Effort

Solo, focused: ~4 weeks for Stages 1–7, plus open-ended polish on the corpus. Stage 8 is a few more days. Stage 9 is dependent on translation-pass rate against the broader library.

Most of the risk is in Stage 3 (preprocessor); most of the leverage is in Stage 5 (the operator); most of the long-term value is in Stage 8 (the test corpus, which is what lets you keep adding shaders without breaking old ones).
