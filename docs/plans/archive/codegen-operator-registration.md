# Codegen-Based Operator Registration

**Created:** 2026-04-30  
**Status:** Draft  
**Supersedes:** `4-30-operator-api-audit.md` → "Refactor Registration" recommendation
**Depends on:** `tree-sitter-source-index-implementation.md` — this plan provides the concrete motivation to unblock tree-sitter integration (the tree-sitter plan's "defer until concrete failure" recommendation).

---

## Problem Statement

The `VIVID_REGISTER` macro (~200 lines of C preprocessor) is the single largest maintenance burden in the Operator API. It:

1. **Hides complexity in macro expansion** — descriptor building, capability detection, static state management, and `extern "C"` stub generation all happen inside a single macro that expands differently for every operator.
2. **Produces undebuggable failures** — the `static ClassName tmp` trick, `if (!inited)` guard, and hidden `s_params` vectors live inside a `static` function inside a macro. When registration fails, you get a crash with no stack trace pointing to the source of truth.
3. **Requires runtime string stabilization** — every `const char*` in `DeferredEntry` exists because the descriptor's string pointers are transient. The runtime must maintain parallel `std::vector<std::string>` storage for every operator it loads.
4. **Cannot enforce the C++/WGSL contract** — `NoiseUniforms` alignment mismatches are the most common developer pain point, and the macro system provides no mechanism to solve this.

The current system works because the macro is a "complete engine component" rather than a thin entry point. The goal of this plan is to make the macro thin again — or eliminate it entirely — by moving all complexity to compile-time codegen.

---

## Goals

| Goal | Detail |
|------|--------|
| **Debuggability** | Every registration step is a real function call visible in a debugger. Failures produce clear error messages, not crashes. |
| **Compile-time descriptor** | The `VividOperatorDescriptor` is built at compile time from a literal struct — no hidden static state, no `if (!inited)` guards. |
| **Zero runtime string stabilization** | The descriptor's `const char*` pointers point to static constexpr storage, not to parallel `std::vector<std::string>` arrays. |
| **Preserve DX** | Operator authors write the same declarative C++ — no new DSL, no new syntax burden. |
| **WGSL alignment** | The codegen pipeline becomes the natural home for WGSL→C++ struct generation. |
| **Backward compatible** | Existing operators continue to work during migration; no forced rewrite. |

---

## Design

### Overview

```
Operator source (.cpp)
    │
    ▼
[Build: operator_codegen tool]
    │
    ├── generated_registration.cpp  (descriptor + stubs)
    └── generated_uniforms.h        (WGSL→C++ struct, optional)
    │
    ▼
[Compile + link]
    │
    ▼
Dylib with resolved symbols:
    vivid_descriptor()    → returns static const descriptor
    vivid_create()        → trivial one-liner
    vivid_process_*()     → trivial one-liners
    (optional) vivid_draw_thumbnail()
    (optional) vivid_draw_inspector()
    (optional) vivid_draw_editor()
    (optional) vivid_file_drop_descriptor()
    (optional) vivid_main_thread_update()
    (optional) vivid_prepare_instance_assets()
```

### Phase 1: Codegen Tool

A standalone C++ tool (`tools/operator_codegen/`) invoked as a custom CMake build step. It reads the operator's source file, parses it, and emits `generated_registration.cpp`.

#### Input: Operator Source

The operator author writes standard C++ with a new registration macro that **only** marks the operator type — it does not expand to entry points:

```cpp
// noise.cpp — operator author's file

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "NoiseTexture";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> scale      {"scale",       4.0f,  0.1f, 100.0f};
    vivid::Param<float> speed      {"speed",       1.0f,  0.0f, 10.0f};
    vivid::Param<int>   octaves    {"octaves",     4,     1,    8};
    // ... more params ...

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        out.push_back(&speed);
        out.push_back(&octaves);
        // ...
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override { /* ... */ }
};

// NEW: Registration marker — no expansion, just metadata
VIVID_DEFINE_OP(Noise) {
    name = "noise";
    display_name = "Noise";
    keywords = {"noise", "fbm", "perlin", "simplex"};
    summary  = "3D noise generator with FBm octaves and animation";
}
```

The codegen tool parses this file to extract:
- The operator class name (`Noise`)
- The base classes (`OperatorBase`, `GpuProcessable`) → capability flags
- `kName`, `kTimeDependent` static members
- The `VIVID_DEFINE_OP` body → descriptor fields
- `collect_params` and `collect_ports` method bodies → param/port descriptors

#### Output: `generated_registration.cpp`

```cpp
// noise_generated.cpp — GENERATED FILE, DO NOT EDIT
// Generated by operator_codegen from noise.cpp

#include "operator_api/types.h"

// ── Param descriptors (populated from collect_params body) ──

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
        // ... all other fields ...
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
        // ...
    },
    // ... more params ...
};

// ── Port descriptors ──

static const char* noise_port_names[] = {
    "texture",
};

static VividPortDescriptor noise_ports[] = {
    {
        .name          = "texture",
        .type          = VIVID_PORT_TEXTURE,
        .direction     = VIVID_PORT_OUTPUT,
        // ... defaults ...
    },
};

// ── Descriptor ──

static const VividOperatorDescriptor noise_descriptor = {
    .name              = "NoiseTexture",
    .display_name      = "Noise",
    .param_count       = 9,
    .params            = noise_params,
    .port_count        = 1,
    .ports             = noise_ports,
    .has_process_audio = 0,
    .has_process_gpu   = 1,
    .has_process_frame = 0,
    .lane_behavior     = VIVID_LANE_POINTWISE,
    .strategy_independent = 0,
    .time_dependent    = 1,
    .keywords          = noise_keywords,
    .keyword_count     = 4,
    .summary           = "3D noise generator with FBm octaves and animation",
};

// ── Keyword storage ──

static const char* noise_keywords[] = {
    "noise", "fbm", "perlin", "simplex",
};

// ── extern "C" entry points (trivial stubs) ──

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
    // No-op: Noise does not override main_thread_update
}

extern "C" void vivid_prepare_instance_assets(
    void* instance, const float* param_values,
    const char** file_param_values, uint32_t file_param_count) {
    // No-op: Noise does not override prepare_instance_assets
}
```

### Phase 2: Runtime Changes

#### 2.1 Simplified `OperatorLoader`

After migration, `OperatorLoader` no longer needs the complex `DeferredEntry` infrastructure for string stabilization. The descriptor's `const char*` pointers point to static constexpr storage in the dylib.

```cpp
// Before (current):
// Descriptor pointers are transient → runtime copies everything into
// DeferredEntry's parallel std::vector<std::string> arrays.

// After (codegen):
// Descriptor is a static const in the dylib → pointers are stable.
// OperatorLoader just holds a pointer to it.
```

The key change is in `OperatorLoader::load()`:

```cpp
// Before: dlsym → call descriptor() → static init guard → copy everything
// After:  dlsym → call descriptor() → return static const pointer (no copy needed)
```

#### 2.2 Simplified `DeferredEntry`

`DeferredEntry` shrinks dramatically:

```cpp
struct DeferredEntry {
    std::string dylib_path;
    VividOperatorDescriptor desc{};       // shallow copy of pointer
    // All the parallel std::vector<std::string> storage is eliminated.
};
```

For deferred/probe-only scans (catalog display), the runtime still needs to copy the descriptor by value into the registry's internal storage. This is a one-time cost at startup, not per-load.

#### 2.3 Registration Validation

A new `validate_descriptor()` function runs after `vivid_descriptor()` returns:

```cpp
struct ValidationError {
    std::string message;
    enum Severity { ERROR, WARNING };
};

std::vector<ValidationError> validate_descriptor(
    const VividOperatorDescriptor* desc,
    const VividPortDescriptor* ports, uint32_t port_count);
```

Checks include:
- Param count matches `params` array length
- All `const char*` fields are non-null or explicitly null
- No duplicate param names
- `has_process_audio`/`has_process_gpu`/`has_process_frame` are consistent with port directions
- For GPU operators: uniform buffer size is a multiple of 16 (WGSL alignment)

#### 2.4 `VIVID_REGISTER` Macro Deprecation

The existing `VIVID_REGISTER` macro is **not removed** — it continues to work for backward compatibility. A new `VIVID_REGISTER_V2` macro is introduced that:

1. Marks the operator for codegen (writes a marker file or annotation)
2. Provides trivial `extern "C"` stubs
3. Does **not** expand descriptor-building logic

```cpp
// VIVID_REGISTER_V2: thin wrapper, delegates to codegen
#define VIVID_REGISTER_V2(ClassName) \
    extern "C" const VividOperatorDescriptor* vivid_descriptor() { \
        return ClassName##_descriptor(); \
    } \
    extern "C" void* vivid_create() { return new ClassName(); } \
    extern "C" void vivid_destroy(void* p) { delete static_cast<ClassName*>(p); } \
    // ... trivial stubs for each dispatch function ...
```

### Phase 3: WGSL→C++ Struct Codegen (Bonus)

The same codegen pipeline generates C++ uniform structs from WGSL:

```bash
# During operator build:
operator_codegen --input noise.cpp --output-dir build/noise/
# Produces:
#   build/noise/generated_registration.cpp
#   build/noise/generated_uniforms.h   ← from WGSL Uniforms struct
```

`generated_uniforms.h`:

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

This eliminates the `NoiseUniforms` alignment mystery entirely.

---

## Build System Integration

### CMake Changes

```cmake
# In each operator's CMakeLists.txt (or a shared include):
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

add_library(${TARGET_NAME} SHARED
    operator.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated_registration.cpp
)
target_link_libraries(${TARGET_NAME} PRIVATE vivid_operator_api)
```

For seed operators, the codegen tool is built as part of the main CMake project:

```cmake
# Top-level CMakeLists.txt
# Links against the same tree-sitter + tree-sitter-cpp compiled sources
# that the runtime's SourceSyntaxIndex uses.
add_executable(operator_codegen
    tools/operator_codegen/main.cpp
    tools/operator_codegen/ast_parser.cpp
    tools/operator_codegen/descriptor_builder.cpp
    tools/operator_codegen/uniform_codegen.cpp
)
target_link_libraries(operator_codegen PRIVATE tree_sitter tree_sitter_cpp)
```

### Build Flow

```
cmake --build build

For each operator:
  1. operator_codegen parses operator.cpp + operator.wgsl
  2. Emits generated_registration.cpp + (optionally) generated_uniforms.h
  3. Clang compiles operator.cpp + generated_registration.cpp → dylib
  4. dlopen at runtime loads the dylib
  5. Runtime calls vivid_descriptor() → gets static const pointer
```

---

## Migration Strategy

### Phase A: Tooling + Seed Operator (No Breaking Changes)

1. Build the `operator_codegen` tool
2. Add CMake integration for codegen build step
3. Migrate **one** seed operator (e.g., `noise`) to the new system
4. Verify it loads, registers, and works identically to the macro version
5. Add the WGSL→C++ struct generation

### Phase B: Gradual Migration (Zero Breaking Changes)

1. Add `VIVID_REGISTER_V2` macro alongside `VIVID_REGISTER`
2. Migrate seed operators one at a time (5-10 per sprint)
3. Each migrated operator is a working example for package authors
4. `VIVID_REGISTER` continues to work for all existing operators and packages

### Phase C: Validation + Warning

1. Add `validate_descriptor()` to the runtime's load path
2. When loading an operator via `VIVID_REGISTER`, emit a deprecation warning:
   ```
   [vivid][deprecated] Operator "Noise" uses VIVID_REGISTER.
   Consider migrating to VIVID_REGISTER_V2 for better error messages.
   See docs/plans/codegen-operator-registration.md
   ```
3. Add a `vivid validate-operators` CLI command that checks all loaded operators

### Phase D: Deprecation (Future)

1. After 6 months, `VIVID_REGISTER` produces a compile-time warning
2. After 12 months, `VIVID_REGISTER` is removed
3. Update documentation, examples, and package ecosystem guidance

---

## Operator Author Experience

### Before (current)

```cpp
struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 100.0f};
    // ...
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        // ...
    }
};

VIVID_REGISTER(Noise)  // ← 200 lines of macro expansion, opaque
```

### After (codegen)

```cpp
struct Noise : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 100.0f};
    // ...
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        // ...
    }
};

VIVID_REGISTER_V2(Noise)  // ← 10 lines of macro expansion, transparent
```

**The operator author's file changes by one line.** The heavy lifting moves to the build step.

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

### Unit Tests

1. **Codegen parser tests** — verify AST parsing of `collect_params`, `collect_ports`, `VIVID_DEFINE_OP` body
2. **Descriptor builder tests** — verify generated descriptor matches source for all param types
3. **Validation tests** — verify error detection for invalid descriptors
4. **WGSL alignment tests** — verify generated structs match WGSL layout

### Integration Tests

1. **Load migrated operator** — load a codegen-built operator via `OperatorLoader::load()`, verify descriptor matches
2. **Compatibility test** — load a `VIVID_REGISTER` operator alongside a `VIVID_REGISTER_V2` operator, verify both work
3. **Hot-reload test** — rebuild a codegen operator, verify hot-reload works identically
4. **Descriptor hash test** — verify `descriptor_hash()` produces stable output for codegen operators

### Migration Tests

For each operator migrated from `VIVID_REGISTER` to `VIVID_REGISTER_V2`:
1. Build with both macros
2. Compare descriptors byte-for-byte
3. Verify identical runtime behavior (all seed operators)

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| **Codegen parser is fragile** — C++ is not a grammar you can parse with regex | **Already solved by the tree-sitter plan.** The same `tree-sitter-cpp` grammar, compiled parser sources, and fallback behavior are already specified. The codegen tool reuses them. |
| **Build complexity increases** — custom build step for every operator | The codegen step is fast (< 50ms) and cached. CMake's `add_custom_command` handles dependency tracking. Existing operators don't need to migrate. |
| **Package ecosystem disruption** — third-party packages use `VIVID_REGISTER` | Backward compatibility is the default. `VIVID_REGISTER` continues to work. Migration is opt-in and gradual. |
| **WGSL codegen is out of scope** — adds significant complexity | WGSL codegen is a bonus feature in Phase A. The core registration migration works without it. |
| **Runtime `DeferredEntry` complexity** — the runtime already has extensive string stabilization code | This is a reduction in complexity, not an increase. The runtime's string stabilization code is gradually eliminated as operators migrate. |
| **Dependency bloat** — tree-sitter adds compile-time/binary size for a narrow use | **Already assessed in the tree-sitter plan.** The dependency is private to runtime codegen/indexing code, not user-facing. The tree-sitter plan's "defer until concrete failure" note applies to the source-index path; the codegen path provides the concrete motivation to unblock it. |

---

## Timeline

**Note:** The tree-sitter dependency and grammar are already planned in `tree-sitter-source-index-implementation.md`. This plan unblocks that work by providing the concrete use case. The timeline assumes tree-sitter is built as part of Phase A and is shared between both paths.

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| **A: Tooling** | 2 weeks | Tree-sitter integration (shared with source-index plan), `operator_codegen` tool, CMake integration, 1 migrated seed operator (noise) |
| **B: Gradual migration** | 6-8 weeks | 15-20 seed operators migrated, all working identically |
| **C: Validation** | 2 weeks | `validate_descriptor()`, deprecation warnings, `vivid validate-operators` CLI |
| **D: WGSL codegen** | 2 weeks | WGSL→C++ struct generation with `static_assert` alignment verification |
| **E: Deprecation** | 6 months | `VIVID_REGISTER` warning, documentation updates, ecosystem migration support |

**Total to core benefit (A+B+C): ~10 weeks**  
**Total to full deprecation (A-E): ~8 months**

---

## Success Criteria

1. **No hidden static state** — descriptor building is a visible, debuggable function call
2. **No runtime string stabilization** — the runtime no longer maintains parallel `std::vector<std::string>` arrays for operator descriptors
3. **Clear error messages** — registration failures produce actionable diagnostic output
4. **Zero DX regression** — operator authors write the same code, get better error messages
5. **Backward compatible** — existing operators and packages continue to work
6. **WGSL alignment verified** — uniform struct alignment is compile-time verified, not manual

---

## Appendix: What `operator_codegen` Parses

The codegen tool parses the following from operator source:

### From the operator class:
- Class name
- Base classes (via `std::is_base_of` equivalent: checks for `AudioProcessable`, `GpuProcessable`, `FrameProcessable` in base list)
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

### Reuse from the Tree-Sitter Plan

The tree-sitter plan (`tree-sitter-source-index-implementation.md`) already specifies:

| Feature | Tree-sitter plan | Codegen plan |
|---------|-----------------|--------------|
| C++ parser | `tree-sitter-cpp` grammar, compiled into runtime | Same grammar, linked from codegen tool |
| `VIVID_REGISTER` detection | Replace regex in `OperatorSourceDocs` | Extract `VIVID_DEFINE_OP` metadata block |
| Type definition discovery | Class/struct names, start/end line | Extract operator class name |
| Base class parsing | Multiline inheritance | Extract `OperatorBase`, `GpuProcessable`, etc. |
| Doc-block adjacency | Comment-node/range lookup | Extract `vivid::description()` calls |
| Fallback on parse failure | Graceful no-doc fallback | Emit conservative descriptor with warnings |

The codegen tool adds parsing for:
- `collect_params` body: extracts `out.push_back(&<param>)` calls
- `collect_ports` body: extracts `out.push_back({...})` aggregate initializers
- `VIVID_DEFINE_OP` body: extracts `name`, `display_name`, `keywords`, `summary`

Everything else is already specified in the tree-sitter plan. The tree-sitter plan's `SourceSyntaxIndex` helper can be refactored into a public-facing `SourceSyntaxParser` that both the source-index and codegen paths use.
