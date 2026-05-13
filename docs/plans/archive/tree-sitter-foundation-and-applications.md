# Tree-sitter Foundation + Dual-Path Applications

**Created:** 2026-04-30  
**Status:** Draft  
**Consolidates:** `tree-sitter-source-index-implementation.md` + `codegen-operator-registration.md`  
**Supersedes:** `4-30-operator-api-audit.md` → "Refactor Registration" recommendation

---

## Executive Summary

This plan introduces Tree-sitter as a shared C++ parsing foundation for Vivid, then uses it along two parallel paths:

1. **Source-index path** — replaces brittle regex in `OperatorSourceDocs` and `SourceIndex` with robust AST-based parsing for operator documentation and symbol classification.
2. **Codegen path** — builds a compile-time `operator_codegen` tool that generates operator registration code (descriptor + `extern "C"` stubs) from operator source, eliminating the ~200-line `VIVID_REGISTER` macro's hidden complexity.

Both paths share the same tree-sitter dependency, the same parser library (`SourceSyntaxParser`), and the same `tree-sitter-cpp` grammar. Developing them in parallel halves the per-path effort compared to executing them sequentially.

---

## Why Consolidate

| Factor | Separate Plans | Consolidated |
|--------|---------------|--------------|
| Tree-sitter dependency | Added twice (or one blocks on the other) | Added once, shared |
| Parser infrastructure | Duplicated AST parsing logic | Single `SourceSyntaxParser` library |
| Grammar maintenance | Two `tree-sitter-cpp` pins | One pinned version |
| Test fixtures | Two separate test suites | Shared C++ fixtures |
| "Concrete failure" blocker | Tree-sitter plan was deferred | Codegen provides the concrete motivation |
| Total effort | ~14 weeks sequential | ~10 weeks parallel |

The tree-sitter plan was deferred because "no concrete failure motivates the work." The codegen operator registration plan provides that concrete motivation — and the tree-sitter plan makes the codegen plan's parser risk negligible.

---

## Goals

### Shared Goals (Both Paths)

| Goal | Detail |
|------|--------|
| **Robust C++ parsing** | Tree-sitter handles multiline declarations, templated base classes, namespace wrapping — all things regex gets wrong. |
| **Graceful fallback** | Parse failures fall back to existing text/regex behavior. No regressions. |
| **Zero user-facing change** | Public API, JSON response shapes, and MCP behavior are preserved. |
| **Cached parsing** | Parsed records are cached per-root/file, invalidated through existing flows. |

### Source-Index Path Goals

| Goal | Detail |
|------|--------|
| **Operator doc extraction** | Replace regex `VIVID_REGISTER` detection, type-definition discovery, base-class parsing, and doc-block adjacency with AST-based lookup. |
| **Symbol classification** | Optionally enrich `SourceIndex::find_symbol()` definition classification for C++ files. |
| **Preserve broad search** | `SourceIndex::search()`, `read_file()`, `read_span()` remain line-based for non-C++ files. |

### Codegen Path Goals

| Goal | Detail |
|------|--------|
| **Debuggability** | Every registration step is a real function call visible in a debugger. Failures produce clear error messages. |
| **Compile-time descriptor** | `VividOperatorDescriptor` built at compile time from a literal struct — no hidden static state, no `if (!inited)` guards. |
| **Zero runtime string stabilization** | Descriptor's `const char*` pointers point to static constexpr storage, not to parallel `std::vector<std::string>` arrays. |
| **Preserve DX** | Operator authors write the same declarative C++ — one macro name change (`VIVID_REGISTER` → `VIVID_REGISTER_V2`). |
| **WGSL alignment** | Codegen pipeline generates C++ uniform structs from WGSL with `static_assert` verification. |
| **Backward compatible** | Existing operators continue to work during migration; no forced rewrite. |

---

## Architecture

### Shared Foundation

```
┌─────────────────────────────────────────────────┐
│  cmake/dependencies.cmake                        │
│  Pin: tree-sitter C runtime                      │
│  Pin: tree-sitter-cpp grammar                    │
└──────────────────┬──────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────┐
│  src/runtime/core/source_syntax_parser.cpp/.h    │
│  (shared library: SourceSyntaxParser)            │
│                                                  │
│  - Wraps tree-sitter C API + tree_sitter_cpp()   │
│  - Parses source file → SourceSyntaxRecord       │
│  - Cache per-root/file, invalidate via           │
│    invalidate_core() / invalidate_package()      │
│  - Fallback: returns empty record on parse fail  │
│                                                  │
│  SourceSyntaxRecord contains:                    │
│  - type_definitions[]: {name, kind, path,        │
│    start_line, end_line, base_class_names[]}     │
│  - register_calls[]: {macro_name, path, line}    │
│  - include_targets[]: {quoted_path, is_system}   │
│  - doc_comment_ranges[]: {start_line, end_line}  │
│  - symbol_definitions[]: {name, kind, path,      │
│    start_line, end_line}                         │
└──────────────────┬──────────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
┌──────────────┐    ┌──────────────────┐
│ Source-index  │    │  Codegen tool    │
│ path          │    │  (operator_      │
│               │    │   codegen)       │
│              │    │                  │
│ • Operator   │    │ • Parses         │
│   SourceDocs │    │   operator.cpp   │
│ • SourceIndex│    │ • Emits          │
│   enrichment │    │   generated_     │
│              │    │   registration.cpp│
└──────────────┘    └──────────────────┘
```

### Source-Index Path Design

`SourceSyntaxParser` replaces the existing regex-based logic in:

- **`OperatorSourceDocs`** — `src/runtime/operators/operator_source_docs.cpp`
  - Replace regex `VIVID_REGISTER` detection with AST-based `register_calls[]` lookup
  - Replace type-definition discovery with `type_definitions[]` records
  - Replace multiline base-class parsing with `base_class_names[]` fields
  - Replace doc-block adjacency detection with `doc_comment_ranges[]` + line adjacency
  - Preserve existing fallback for wrapper classes like `ClockAu` resolving docs from shared base types

- **`SourceIndex`** — `src/runtime/core/source_index.cpp`
  - Enrich `find_symbol()` definition classification for C++ files using `symbol_definitions[]`
  - Keep `search()`, `read_file()`, `read_span()`, `find_references()` line-based (non-C++ coverage)

### Codegen Path Design

The codegen tool (`tools/operator_codegen/`) uses `SourceSyntaxParser` to parse operator source and emit `generated_registration.cpp`:

```
Operator source (.cpp)
    │
    ▼
SourceSyntaxParser (shared library)
    │
    ├── type_definitions[] → operator class name + base classes
    ├── register_calls[] → VIVID_DEFINE_OP metadata block
    ├── doc_comment_ranges[] → vivid::description() calls
    └── symbol_definitions[] → collect_params / collect_ports bodies
    │
    ▼
DescriptorBuilder (codegen-specific)
    │
    ▼
generated_registration.cpp
    ├── static VividParamDescriptor[] (from collect_params)
    ├── static VividPortDescriptor[] (from collect_ports)
    ├── static VividOperatorDescriptor (literal struct)
    └── extern "C" entry points (trivial stubs)
```

The operator author writes:

```cpp
struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 100.0f};
    // ...
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
};

VIVID_DEFINE_OP(Noise) {
    name = "noise";
    display_name = "Noise";
    keywords = {"noise", "fbm", "perlin", "simplex"};
    summary  = "3D noise generator with FBm octaves and animation";
}
```

The codegen tool emits `generated_registration.cpp` with a static const `VividOperatorDescriptor` and trivial `extern "C` stubs. The operator author's file changes by one line (`VIVID_REGISTER` → `VIVID_DEFINE_OP` + `VIVID_REGISTER_V2`).

---

## Implementation Phases

### Phase 0: Tree-sitter Foundation (Shared)

**Duration:** 1 week  
**Deliverable:** Tree-sitter dependency pinned, compiled, and available to both paths.

1. Pin `tree-sitter` C runtime and `tree-sitter-cpp` grammar in `cmake/dependencies.cmake`
2. Use generated parser sources directly; compile into internal target `tree_sitter_cpp_lib`
3. No CLI, no Node, no npm — pure C/C++ compile-time integration
4. Update dependency manifest in `docs/ARCHITECTURE.md`
5. **No runtime behavior changes** — this is purely infrastructure

**Acceptance:**
- `cmake --build build` compiles `tree_sitter_cpp_lib` without errors
- Both `operator_codegen` and runtime source-index code can link against it

---

### Phase 1: SourceSyntaxParser Library (Shared)

**Duration:** 1.5 weeks  
**Deliverable:** `SourceSyntaxParser` library with tests.

1. Add `src/runtime/core/source_syntax_parser.h/.cpp`
2. Implement parsing: type definitions, register calls, includes, doc comments, symbol definitions
3. Cache parsed records per-root/file; invalidate through `invalidate_core()` / `invalidate_package()` / `SourceIndex::invalidate()`
4. Parse only C++ extensions: `.cpp`, `.cc`, `.cxx`, `.mm`, `.h`, `.hh`, `.hpp`
5. File-size and directory-skip limits consistent with existing indexers
6. Graceful fallback: empty records on parse failure (no crash)
7. Unit tests with fixtures: multiline declarations, templated bases, namespaces, malformed C++

**Acceptance:**
- `SourceSyntaxParser::parse()` returns correct records for all test fixtures
- Malformed C++ returns empty records without crashing
- Cache invalidation works through existing flows

---

### Phase 2A: Source-Index Path

**Duration:** 2 weeks  
**Deliverable:** `OperatorSourceDocs` and `SourceIndex` use AST-based parsing with preserved public behavior.

1. **Prototype in `OperatorSourceDocs`** first:
   - Replace regex `VIVID_REGISTER` detection with AST `register_calls[]` lookup
   - Replace type-definition discovery with `type_definitions[]` records
   - Replace multiline base-class parsing with `base_class_names[]`
   - Replace doc-block adjacency with `doc_comment_ranges[]` + line adjacency
   - Preserve wrapper class fallback (e.g., `ClockAu` → shared base docs)

2. **Enrich `SourceIndex::find_symbol()`** (optional, if stable):
   - Use `symbol_definitions[]` for C++ definition classification
   - Keep line-based search for non-C++ files unchanged

3. **Tests:**
   - Extend `test_operator_source_docs` with multiline, templated, namespace-wrapped, malformed fixtures
   - Extend `test_source_index` with C++ definition vs. reference fixtures
   - Verify unchanged public JSON response shapes

**Acceptance:**
- `OperatorSourceDocs` JSON output is compatible with existing callers
- Handles multiline declarations, templated bases, namespace wrapping more robustly
- `SourceIndex::search()`, `read_file()`, `read_span()` remain line-based
- `find_symbol()` definition classification improves only where parsed data is available
- All existing tests pass

---

### Phase 2B: Codegen Path

**Duration:** 2 weeks (parallel with 2A)  
**Deliverable:** `operator_codegen` tool, CMake integration, 1 migrated seed operator (noise).

1. **Build `operator_codegen` tool:**
   - `tools/operator_codegen/main.cpp` — CLI entry point
   - `tools/operator_codegen/descriptor_builder.cpp` — parses `SourceSyntaxRecord` → `VividOperatorDescriptor`
   - `tools/operator_codegen/uniform_codegen.cpp` — WGSL `struct Uniforms` → C++ struct with `static_assert`s
   - Links against `tree_sitter_cpp_lib` from Phase 0

2. **CMake integration:**
   ```cmake
   add_custom_command(
       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated_registration.cpp
       COMMAND operator_codegen
           --input ${CMAKE_CURRENT_SOURCE_DIR}/operator.cpp
           --output ${CMAKE_CURRENT_BINARY_DIR}/generated_registration.cpp
           --wgsl ${CMAKE_CURRENT_SOURCE_DIR}/operator.wgsl
       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/operator.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/operator.wgsl
               ${CMAKE_SOURCE_DIR}/tools/operator_codegen/operator_codegen
       COMMENT "Generating operator registration for ${TARGET_NAME}"
   )
   ```

3. **Migrate `noise` operator** to `VIVID_DEFINE_OP` + `VIVID_REGISTER_V2`:
   - Verify it loads, registers, and works identically to the macro version
   - Byte-for-byte descriptor comparison against the `VIVID_REGISTER` version

4. **WGSL→C++ struct codegen:**
   - Parse `struct Uniforms` from WGSL
   - Emit C++ struct with offset/size/align comments and `static_assert`s
   - Verified against WGSL layout rules (16-byte minimum alignment, vec alignment)

**Acceptance:**
- `operator_codegen` parses `noise.cpp` and emits valid `generated_registration.cpp`
- Migrated `noise` operator loads identically to the `VIVID_REGISTER` version
- WGSL struct generation produces correct alignment-verified C++ struct

---

### Phase 3: Gradual Migration (Codegen Path)

**Duration:** 6-8 weeks  
**Deliverable:** 15-20 seed operators migrated, `VIVID_REGISTER` deprecation path active.

1. Add `VIVID_REGISTER_V2` macro alongside `VIVID_REGISTER`
2. Migrate seed operators one at a time (5-10 per sprint)
3. Each migrated operator is a working example for package authors
4. `VIVID_REGISTER` continues to work for all existing operators and packages
5. Add `validate_descriptor()` to the runtime's load path
6. Add deprecation warning when loading via `VIVID_REGISTER`:
   ```
   [vivid][deprecated] Operator "Noise" uses VIVID_REGISTER.
   Consider migrating to VIVID_REGISTER_V2.
   See docs/plans/tree-sitter-foundation-and-applications.md
   ```
7. Add `vivid validate-operators` CLI command

**Byte-for-byte comparison for each migrated operator:**
- Build with both macros
- Compare descriptors
- Verify identical runtime behavior

---

### Phase 4: Validation + Deprecation

**Duration:** 2 weeks (validation) + 6 months (deprecation)  
**Deliverable:** `validate_descriptor()` production-ready, `VIVID_REGISTER` deprecated.

1. **Validation pass** (immediate):
   - Param count matches `params` array length
   - All `const char*` fields are non-null or explicitly null
   - No duplicate param names
   - `has_process_audio`/`has_process_gpu`/`has_process_frame` consistent with port directions
   - For GPU operators: uniform buffer size is a multiple of 16

2. **Deprecation path** (6-12 months):
   - After 6 months: `VIVID_REGISTER` produces compile-time warning
   - After 12 months: `VIVID_REGISTER` removed
   - Documentation, examples, and package ecosystem guidance updated

---

## Runtime Simplification (Post-Migration)

As operators migrate to `VIVID_REGISTER_V2`, the runtime's `DeferredEntry` infrastructure shrinks:

```cpp
// Before: every operator loaded → runtime copies all const char* into
// parallel std::vector<std::string> arrays for stability.

// After (codegen operators): descriptor is a static const in the dylib.
// const char* pointers are stable. No copy needed.

struct DeferredEntry {
    std::string dylib_path;
    VividOperatorDescriptor desc{};       // shallow copy of pointer
    // All the parallel std::vector<std::string> storage eliminated
    // for codegen-built operators.
};
```

This is a **reduction in complexity**, not an increase. The string stabilization code is gradually eliminated as operators migrate.

---

## Operator Author Experience

### Before (current)

```cpp
struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 100.0f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }
};

VIVID_REGISTER(Noise)  // ← 200 lines of macro expansion, opaque
```

### After (codegen)

```cpp
struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 100.0f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }
};

VIVID_DEFINE_OP(Noise) {
    name = "noise";
    display_name = "Noise";
    keywords = {"noise", "fbm", "perlin", "simplex"};
    summary  = "3D noise generator with FBm octaves and animation";
}
VIVID_REGISTER_V2(Noise)  // ← 10 lines, transparent
```

**One line changes.** The heavy lifting moves to the build step.

### DX Comparison

| Aspect | `VIVID_REGISTER` | `VIVID_REGISTER_V2` + codegen |
|--------|-----------------|-------------------------------|
| Operator source complexity | 200 lines of macro | 1 line |
| Debuggability | None (macro expansion) | Every step is a real function |
| Error messages | Compiler noise | Clear validation errors |
| Descriptor stability | Runtime string stabilization | Static constexpr pointers |
| WGSL alignment | Manual, error-prone | Automatic, `static_assert`-verified |
| Build step | None | Custom codegen step (fast, cached) |
| Developer friction | Low (nothing to learn) | Low (one macro name change) |

---

## Testing Strategy

### Shared Test Fixtures

Both paths share C++ test fixtures for:
- Multiline class/struct declarations
- Templated base classes
- Namespace-wrapped operators
- Doc-comment adjacency and no-doc fallback
- `VIVID_REGISTER` / `VIVID_DEFINE_OP` with whitespace or line breaks
- Wrapper classes resolving shared base docs
- Malformed or incomplete C++ returning graceful fallback

### Source-Index Tests

- Extend `test_operator_source_docs` — verify `OperatorSourceDocs` JSON output
- Extend `test_source_index` — verify `find_symbol()` classification
- Verify unchanged public response shapes

### Codegen Tests

- **Parser tests** — verify AST parsing of `collect_params`, `collect_ports`, `VIVID_DEFINE_OP` body
- **Descriptor builder tests** — verify generated descriptor matches source for all param types
- **Validation tests** — verify error detection for invalid descriptors
- **WGSL alignment tests** — verify generated structs match WGSL layout
- **Load tests** — load codegen-built operator via `OperatorLoader::load()`, verify descriptor matches
- **Compatibility tests** — load `VIVID_REGISTER` alongside `VIVID_REGISTER_V2`, verify both work
- **Hot-reload tests** — rebuild codegen operator, verify hot-reload works identically
- **Descriptor hash tests** — verify `descriptor_hash()` produces stable output
- **Migration tests** — byte-for-byte descriptor comparison for each migrated operator

### Verification Commands

```bash
cmake --build build --target test_operator_source_docs test_source_index
ctest --test-dir build --output-on-failure -R "operator_source_docs|source_index"

# Codegen tests (new target)
cmake --build build --target test_operator_codegen
ctest --test-dir build --output-on-failure -R "operator_codegen"
```

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| **Tree-sitter dependency cost** — compile-time and binary size for narrow use | Assessed in tree-sitter plan. Dependency is private to runtime codegen/indexing code, not user-facing. This consolidated plan provides the concrete motivation to proceed. |
| **Parser is fragile** — C++ is not a grammar you can parse with regex | Tree-sitter handles this. The `tree-sitter-cpp` grammar is the same one used by VS Code, Neovim, and dozens of other tools. Graceful fallback on parse failure. |
| **Build complexity increases** — custom build step for every operator | The codegen step is fast (< 50ms) and cached. CMake's `add_custom_command` handles dependency tracking. Existing operators don't need to migrate. |
| **Package ecosystem disruption** — third-party packages use `VIVID_REGISTER` | Backward compatibility is the default. `VIVID_REGISTER` continues to work. Migration is opt-in and gradual. |
| **WGSL codegen out of scope** — adds complexity | WGSL codegen is a bonus feature in Phase 2B. The core registration migration works without it. |
| **Runtime `DeferredEntry` complexity** — extensive string stabilization code | This is a reduction in complexity. The string stabilization code is gradually eliminated as operators migrate. |
| **Dual-maintenance burden** — regex fallbacks + AST parsing | Fallback is the *absence* of AST data, not a parallel code path. When parsing succeeds, AST wins. When it fails, empty records → existing behavior. No dual-maintenance. |

---

## Timeline

| Phase | Duration | Deliverable | Parallel? |
|-------|----------|-------------|-----------|
| **0: Tree-sitter** | 1 week | Dependency pinned, compiled, available to both paths | — |
| **1: SourceSyntaxParser** | 1.5 weeks | Shared `SourceSyntaxParser` library with tests | — |
| **2A: Source-index** | 2 weeks | `OperatorSourceDocs` + `SourceIndex` AST-based | ✅ parallel |
| **2B: Codegen** | 2 weeks | `operator_codegen` tool, 1 migrated operator | ✅ parallel |
| **3: Gradual migration** | 6-8 weeks | 15-20 seed operators migrated | — |
| **4: Validation** | 2 weeks | `validate_descriptor()`, deprecation warnings | — |
| **5: Deprecation** | 6 months | `VIVID_REGISTER` removed | — |

**Total to core benefit (0+1+2A+2B+4): ~9 weeks**  
**Total to full deprecation (0-5): ~9 months**

### Parallelism Gains

| Approach | Weeks |
|----------|-------|
| Tree-sitter plan alone (deferred) | ∞ (no start) |
| Tree-sitter plan (unblocked) | 4.5 weeks |
| Codegen plan alone (with tree-sitter) | 12 weeks |
| **Consolidated (parallel 2A+2B)** | **9 weeks to core benefit** |

The 3-week saving comes from running Phase 2A and 2B in parallel instead of sequentially.

---

## Success Criteria

### Foundation (Phases 0+1)

1. Tree-sitter is pinned and compiled without runtime CLI or Node requirements
2. `SourceSyntaxParser` handles all test fixtures correctly
3. Parse failures return empty records without crashing

### Source-Index Path (Phase 2A)

4. `OperatorSourceDocs` output is compatible with existing callers
5. Handles multiline declarations, templated bases, namespace wrapping more robustly
6. `SourceIndex::search()`, `read_file()`, `read_span()` remain line-based
7. `find_symbol()` definition classification improves only where parsed data is available

### Codegen Path (Phase 2B+)

8. No hidden static state — descriptor building is a visible, debuggable function call
9. No runtime string stabilization — the runtime no longer maintains parallel `std::vector<std::string>` arrays for codegen-built operators
10. Clear error messages — registration failures produce actionable diagnostic output
11. Zero DX regression — operator authors write the same code, get better error messages
12. Backward compatible — existing operators and packages continue to work
13. WGSL alignment verified — uniform struct alignment is compile-time verified

---

## Appendix A: What `operator_codegen` Parses

The codegen tool uses `SourceSyntaxParser` to extract:

### From the operator class:
- Class name (from `type_definitions[]`)
- Base classes (from `base_class_names[]` in type definition)
- `kName` static constexpr member
- `kTimeDependent` static constexpr member
- `collect_params` body: extracts `out.push_back(&<param>)` calls
- `collect_ports` body: extracts `out.push_back({...})` aggregate initializers
- `vivid::description(...)`, `vivid::semantic_tag(...)`, etc. calls on params
- `main_thread_update` override (presence/absence)
- `prepare_instance_assets` override (presence/absence)

### From `VIVID_DEFINE_OP` body:
- `name = "..."`
- `display_name = "..."`
- `keywords = {"...", ...}`
- `summary = "..."`

### From WGSL (optional):
- `struct Uniforms { ... }` definition
- Generates C++ struct with offset/size/align comments and `static_assert`s

### What it does NOT parse:
- Shader code (`kNoiseFragment`) — left as-is
- GPU initialization (`lazy_init`) — left as-is
- Any other C++ code — ignored

---

## Appendix B: Tree-Sitter Parsing Reuse

| Feature | Source-index path | Codegen path |
|---------|------------------|--------------|
| Type definitions | `type_definitions[]` → operator docs | `type_definitions[]` → class name, base classes |
| Register calls | `register_calls[]` → `VIVID_REGISTER` detection | `register_calls[]` → `VIVID_DEFINE_OP` metadata |
| Base class names | `base_class_names[]` → doc inheritance chain | `base_class_names[]` → capability flags |
| Doc comment ranges | `doc_comment_ranges[]` → doc-block adjacency | `doc_comment_ranges[]` → `vivid::description()` calls |
| Include targets | `include_targets[]` → recursive doc lookup | Not used |
| Symbol definitions | `symbol_definitions[]` → `find_symbol()` enrichment | `symbol_definitions[]` → `collect_params`/`collect_ports` bodies |

---

## Appendix C: Generated Output Example

### `generated_registration.cpp` (from `noise.cpp`)

```cpp
// noise_generated.cpp — GENERATED FILE, DO NOT EDIT
// Generated by operator_codegen from noise.cpp

#include "operator_api/types.h"

static const char* noise_param_names[] = {
    "scale", "speed", "octaves", "lacunarity", "persistence",
    "noise_type", "channels", "scale_from_x", "scale_from_y",
};

static VividParamDescriptor noise_params[] = {
    {
        .name          = "scale",
        .type          = VIVID_PARAM_FLOAT,
        .default_value = 4.0f,
        .min_value     = 0.1f,
        .max_value     = 100.0f,
        .group         = nullptr,
        .display_hint  = VIVID_DISPLAY_DEFAULT,
        .semantic_tag  = nullptr,
        .semantic_shape= nullptr,
        .semantic_unit = nullptr,
        .semantic_intent= nullptr,
        .description   = "Zoom level of the noise pattern",
    },
    {
        .name          = "speed",
        .type          = VIVID_PARAM_FLOAT,
        .default_value = 1.0f,
        .min_value     = 0.0f,
        .max_value     = 10.0f,
        .semantic_tag  = "frequency_hz",
        .semantic_shape= "scalar",
        .semantic_unit = "Hz",
        .semantic_intent= "animation_rate",
    },
    // ... more params ...
};

static const char* noise_port_names[] = { "texture" };

static VividPortDescriptor noise_ports[] = {
    {
        .name          = "texture",
        .type          = VIVID_PORT_TEXTURE,
        .direction     = VIVID_PORT_OUTPUT,
    },
};

static const char* noise_keywords[] = {
    "noise", "fbm", "perlin", "simplex",
};

static const VividOperatorDescriptor noise_descriptor = {
    .name                  = "NoiseTexture",
    .display_name          = "Noise",
    .param_count           = 9,
    .params                = noise_params,
    .port_count            = 1,
    .ports                 = noise_ports,
    .has_process_audio     = 0,
    .has_process_gpu       = 1,
    .has_process_frame     = 0,
    .lane_behavior         = VIVID_LANE_POINTWISE,
    .strategy_independent  = 0,
    .time_dependent        = 1,
    .keywords              = noise_keywords,
    .keyword_count         = 4,
    .summary               = "3D noise generator with FBm octaves and animation",
};

extern "C" uint32_t vivid_abi_version() {
    return VIVID_OPERATOR_ABI_VERSION;
}

extern "C" const VividOperatorDescriptor* vivid_descriptor() {
    return &noise_descriptor;
}

extern "C" void* vivid_create() {
    return new Noise();
}

extern "C" void vivid_destroy(void* instance) {
    delete static_cast<Noise*>(instance);
}

extern "C" void vivid_process_frame(void* instance, VividFrameContext* ctx) {
    // No-op: Noise is not FrameProcessable
}

extern "C" void vivid_process_audio(void* instance, VividAudioContext* ctx) {
    // No-op: Noise is not AudioProcessable
}

extern "C" void vivid_process_gpu(void* instance, VividGpuContext* ctx) {
    static_cast<Noise*>(instance)->process_gpu(ctx);
}

extern "C" void vivid_main_thread_update(void* instance, double time,
                                          const char** file_param_values,
                                          uint32_t file_param_count) {
    // No-op
}

extern "C" void vivid_prepare_instance_assets(
    void* instance, const float* param_values,
    const char** file_param_values, uint32_t file_param_count) {
    // No-op
}
```

### `generated_uniforms.h` (from `noise.wgsl`)

```cpp
// Auto-generated from noise.wgsl — DO NOT EDIT
// Verified against WGSL layout rules

struct NoiseUniforms {
    float  resolution[2];     // offset 0, size 8, align 8
    float  time;              // offset 8, size 4, align 4
    float  scale;             // offset 12, size 4, align 4
    float  speed;             // offset 16, size 4, align 4
    float  z;                 // offset 20, size 4, align 4
    float  lacunarity;        // offset 24, size 4, align 4
    float  persistence;       // offset 28, size 4, align 4
    float  offsetX;           // offset 32, size 4, align 4
    float  offsetY;           // offset 36, size 4, align 4
    int32_t octaves;          // offset 40, size 4, align 4
    int32_t noiseType;        // offset 44, size 4, align 4
    int32_t colorNoise;       // offset 48, size 4, align 4
    float  scaleFromX;        // offset 52, size 4, align 4
    float  scaleFromY;        // offset 56, size 4, align 4
    int32_t _pad;             // offset 60, size 4, align 8 (vec2f align)
    // Total: 64 bytes (multiple of WGSL 16-byte minimum)

    static_assert(sizeof(NoiseUniforms) == 64);
    static_assert(offsetof(NoiseUniforms, scaleFromY) == 56);
};
```
