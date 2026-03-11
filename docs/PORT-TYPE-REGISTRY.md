# Port Type Registry — Design Exploration

## Defining Ports — Package Creator Experience

This section shows what port definition looks like from a package author's perspective — the current API and how it evolves through the phases below.

### Today

Every operator implements `collect_ports()`, pushing `VividPortDescriptor` structs into a vector. The struct has four fields (`types.h`):

```cpp
typedef struct VividPortDescriptor {
    const char*        name;
    VividPortType      type;
    VividPortDirection direction;
    uint32_t           handle_type_id;  // non-zero when type == VIVID_PORT_HANDLE
} VividPortDescriptor;
```

For the 6 built-in non-handle types, `handle_type_id` is implicitly zero:

```cpp
// Float ports (clock.h — Clock operator)
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back({"beat_phase", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    out.push_back({"beat_ms",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    out.push_back({"bar_phase",  VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
}

// Audio ports (gain.cpp)
out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT});
out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});

// Texture ports (texture_loader.cpp)
out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});

// String ports (string_select.cpp)
out.push_back({"file", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});

// Spread ports (spread_adsr.cpp)
out.push_back({"gates",     VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
out.push_back({"envelopes", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});

// String-spread ports (folder_list.cpp)
out.push_back({"files", VIVID_PORT_STRING_SPREAD, VIVID_PORT_OUTPUT});
```

Handle ports use the `VIVID_HANDLE_PORT` macro (`type_id.h`) which fills in the `handle_type_id` via `vivid_type_id<T>()`:

```cpp
#define VIVID_HANDLE_PORT(port_name, dir, CppType) \
    VividPortDescriptor { (port_name), VIVID_PORT_HANDLE, (dir), vivid_type_id<CppType>() }

// Handle port (movie_loaded.cpp — outputs a MediaStreamV1 handle alongside a texture)
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    out.push_back({"time",    VIVID_PORT_FLOAT,   VIVID_PORT_OUTPUT});
    out.push_back({"speed",   VIVID_PORT_FLOAT,   VIVID_PORT_OUTPUT});
    out.push_back(VIVID_HANDLE_PORT("media_clock", VIVID_PORT_OUTPUT, vivid::MediaStreamV1));
}
```

### After Phases 1+2

`VividPortDescriptor` gains two trailing fields: `handle_payload_size` and `handle_type_name`. The `VIVID_HANDLE_PORT` macro emits both automatically — non-handle ports are unchanged:

```cpp
#define VIVID_HANDLE_PORT(port_name, dir, CppType) \
    VividPortDescriptor { (port_name), VIVID_PORT_HANDLE, (dir), \
                          vivid_type_id<CppType>(), sizeof(CppType), #CppType }

// Package author code — identical call site, macro does the work:
out.push_back(VIVID_HANDLE_PORT("media_clock", VIVID_PORT_OUTPUT, vivid::MediaStreamV1));
// Expands to: { "media_clock", VIVID_PORT_HANDLE, VIVID_PORT_OUTPUT,
//               0xA3F2..., 56, "vivid::MediaStreamV1" }
```

The runtime uses `handle_payload_size` for generic cross-domain copy (no more hardcoded `sizeof` branches) and `handle_type_name` for UI tooltips and MCP/LLM-facing port descriptions.

### After Phase 3

Packages that define custom handle types export an optional `vivid_register_types` function. The package manager calls it after `dlopen`, before any operator is instantiated. Packages without custom types simply omit it — fully backward compatible.

```cpp
// In your package's main .cpp, alongside the existing vivid_get_descriptor() export:

#include "operator_api/type_id.h"
#include "my_scene_fragment.h"

extern "C" void vivid_register_types(VividRegistrationContext* ctx) {
    static const VividHandleTypeInfo info = {
        .type_id      = vivid_type_id<mypkg::SceneFragmentV1>(),
        .type_name    = "mypkg::SceneFragmentV1",
        .package_name = "my_3d_package",
        .payload_size = sizeof(mypkg::SceneFragmentV1),
        .abi_version  = 1,
        .description  = "Scene fragment carrying transform + mesh reference"
    };
    vivid_register_handle_type(&info);
}
```

Operators still define ports with the same `VIVID_HANDLE_PORT` macro — registration is separate from port declaration:

```cpp
out.push_back(VIVID_HANDLE_PORT("scene_out", VIVID_PORT_OUTPUT, mypkg::SceneFragmentV1));
```

### Defining a custom handle type

Handle payloads must satisfy these requirements:

- **POD / trivially copyable** — the runtime `memcpy`s them across domain boundaries
- **≤ 256 bytes** — must fit in `HandleInputSnapshot::kMaxBytes` for audio-domain delivery
- **Version suffix** — use `V1`, `V2`, etc.; bump on breaking layout changes
- **Namespace** — `vivid::` for core types, `<package>::` for community types

Minimal example:

```cpp
// my_scene_fragment.h
#pragma once
#include <cstdint>

namespace mypkg {

struct SceneFragmentV1 {
    float    transform[16];   // 4×4 column-major
    uint32_t mesh_id;
    uint32_t material_id;
    float    opacity;
};

static_assert(sizeof(SceneFragmentV1) <= 256, "must fit in HandleInputSnapshot");
static_assert(__is_trivially_copyable(SceneFragmentV1), "must be POD for cross-domain memcpy");

} // namespace mypkg
```

---

## Context

Vivid has 7 built-in port types that represent fundamentally different runtime routing mechanisms (float copy, audio buffers, GPU textures, etc.). The `VIVID_PORT_HANDLE` type is the extensibility point — it carries a `void*` with a `handle_type_id` (FNV-1a hash) for type safety at connection time.

Currently, handle type safety is nominal (name-hash only), and the runtime has no knowledge of handle payloads beyond the hash. With community-contributed packages exchanging custom handle data without dev-time coordination, we need:

- **Discoverability** — tooling/LLM can enumerate available handle types and their semantics
- **Collision detection** — two packages defining `MeshBuffer` with different layouts are caught
- **Compatibility rules** — structured type A can connect to type B if a conversion exists
- **UI presentation** — custom type names, colors, tooltips in the graph editor
- **Cross-domain safety** — the audio engine can copy any handle type generically (today it hardcodes `sizeof(MediaStreamV1)`)

### Key insight

Custom port types don't need new routing mechanisms — they need **typed data passing with metadata**, which is exactly what HANDLE already does. The registry is not about adding values to the `VividPortType` enum. It's about enriching what HANDLE can express.

---

## Current State (reference)

### Where port types are handled in the runtime

| Subsystem | File | What it does with port types |
|-----------|------|------------------------------|
| Scheduler init | `scheduler.cpp:176-218` | Classifies ports into per-type index vectors |
| Wire resolution | `scheduler.cpp:400-590` | Type matching, handle_type_id validation |
| Tick processing | `scheduler.cpp:711-940` | Type-specific routing (texture, handle, string, float) |
| Audio bridging | `audio_engine.cpp:559-573` | Cross-domain memcpy into 256-byte HandleInputSnapshot |
| UI compatibility | `node_graph_util.h:135-149` | `port_type_compatible()` — hardcoded rules |
| UI visuals | `node_graph_constants.h:137-144` | Domain colors (not per-port-type) |
| MCP/tooling | `graph_snapshot.h:42-56` | PortInfo exposes type + direction |
| Package loading | `operator_registry.h:17-31` | Caches VividPortDescriptor including handle_type_id |

### Current handle types defined

| Type | Header | Status | Size |
|------|--------|--------|------|
| `vivid::MediaStreamV1` | `media_stream.h` | Active (movie ops) | ~56 bytes |
| `vivid::MediaClockV1` | `media_clock.h` | Active (embedded in MediaStreamV1) | ~40 bytes |
| `VividGpuBuffer` | `gpu_types.h` | Scaffolding | ~24 bytes |
| `VividMesh` | `gpu_types.h` | Scaffolding | ~64 bytes |
| `VividMidiBuffer` | `midi_types.h` | Scaffolding | ~520 bytes |

### Constraints

- Cross-domain handle payloads must fit in `HandleInputSnapshot::kMaxBytes = 256`
- Audio thread: no heap allocation, no blocking
- `dlopen` boundary: no C++ RTTI, no virtual dispatch across libraries
- FNV-1a hash includes full namespace (`vivid::MediaStreamV1` ≠ `mypkg::MediaStreamV1`)

---

## Phased Design

### Phase 0: Documentation & Convention (no code changes)

**Goal:** Establish handle type authoring conventions before building machinery.

- Document the handle type contract in `docs/HANDLE-TYPES.md`:
  - All handle payloads must be POD/trivially-copyable (for cross-domain memcpy)
  - Max 256 bytes for audio-domain consumption
  - Version suffix convention (`V1`, `V2`) for breaking changes
  - Namespace convention: `vivid::` for core, `<package>::` for community
- Add a "Handle Type Catalog" section listing all defined types with their sizes and purposes
- Establish that `type_id.h` is the single header packages include for handle type support

**Open question:** Should we require packages to place handle type headers in a well-known location (e.g. `include/handle_types/`) for cross-package consumption?

### Phase 1: Generic Cross-Domain Handle Copy

**Goal:** Remove the hardcoded `sizeof(MediaStreamV1)` from the audio engine.

**Problem:** `audio_engine.cpp:559-573` branches on `handle_type_id == vivid_type_id<MediaStreamV1>()` to determine copy size. Every new handle type needs a new branch.

**Approach A — Store sizeof in VividPortDescriptor:**
- Add `uint32_t handle_payload_size` to `VividPortDescriptor` (ABI change — bump to v2?)
- The `VIVID_HANDLE_PORT` macro already knows the type, so it can emit `sizeof(CppType)`
- Audio engine reads `handle_payload_size` from the descriptor instead of branching
- Pro: No runtime state, no registration. Cons: ABI field addition.

**Approach B — Embedded header (discussed earlier, deferred):**
- `VividHandleHeader` as first field with `struct_size`
- Audio engine reads size from the payload itself
- Pro: Validates actual data. Con: Requires all handle types to adopt the header.

**Approach C — Type registry lookup:**
- Packages register `{ type_id, sizeof, name }` at load time
- Audio engine queries registry by `handle_type_id` to get size
- Pro: Centralizes metadata. Con: Registration machinery.

**Recommendation:** Approach A is the smallest change that solves the immediate problem. Approach C is the long-term answer (Phase 3). They're compatible — the descriptor field works now, the registry subsumes it later.

### Phase 2: Handle Type Metadata in the Scheduler

**Goal:** The scheduler and UI know handle type names (not just numeric IDs).

**Changes:**
- Extend `VividPortDescriptor` with `const char* handle_type_name` (nullable, set by macro)
- Update `VIVID_HANDLE_PORT` to also emit the stringified type name:
  ```cpp
  #define VIVID_HANDLE_PORT(port_name, dir, CppType) \
      VividPortDescriptor { (port_name), VIVID_PORT_HANDLE, (dir), \
                            vivid_type_id<CppType>(), sizeof(CppType), #CppType }
  ```
- `node_graph_util.h` — `port_type_compatible()` can show type name in tooltip on mismatch
- `graph_snapshot.h` — `PortInfo` gains `handle_type_name` for MCP/LLM exposure
- MCP tools can report "this port expects a `vivid::MediaStreamV1`" instead of "this port expects handle 0xA3F2B1C4"

**ABI impact:** Adds fields to `VividPortDescriptor`. Must be done together with Phase 1 if we want a single ABI bump.

### Phase 3: Handle Type Registry

**Goal:** Packages register handle types at load time. The runtime has a queryable catalog of all available handle types.

**Registry API (C, in `type_id.h` or new `handle_registry.h`):**
```cpp
typedef struct VividHandleTypeInfo {
    uint32_t     type_id;         // FNV-1a hash
    const char*  type_name;       // "vivid::MediaStreamV1"
    const char*  package_name;    // "vivid_core" / "vivid_3d"
    uint32_t     payload_size;    // sizeof(T)
    uint32_t     abi_version;     // package-defined, bumped on breaking changes
    const char*  description;     // human/LLM-readable
} VividHandleTypeInfo;

// Called by packages at load time (from vivid_register or init)
void vivid_register_handle_type(const VividHandleTypeInfo* info);

// Called by runtime/tooling to query
const VividHandleTypeInfo* vivid_lookup_handle_type(uint32_t type_id);
uint32_t vivid_list_handle_types(const VividHandleTypeInfo** out, uint32_t max);
```

**Registration timing:**
- Packages already have a load entry point via `dlopen` → `vivid_get_descriptor()`
- Add an optional `vivid_register_types(VividRegistrationContext*)` export that the package manager calls after `dlopen` but before any operator is instantiated
- If the export doesn't exist, the package has no custom types (backward compatible)

**Collision detection:**
- If two packages register the same `type_id` with different `payload_size` or `abi_version`, the runtime logs an error and refuses to load the second package
- Same `type_id` + same `payload_size` + same `abi_version` = assumed compatible (shared type header)

**What the registry enables:**
- Audio engine queries `payload_size` by `type_id` — no hardcoded branches
- UI shows type names and descriptions on hover
- MCP/LLM can enumerate handle types: "available handle types: MediaStreamV1 (media playback state), VividMesh (GPU mesh data), ..."
- Graph editor can filter connection suggestions by handle type compatibility
- Future: conversion functions between compatible handle types

### Phase 4: Handle Type Compatibility & Conversion (future)

**Goal:** Allow compatible-but-not-identical handle types to connect with automatic conversion.

This is the furthest-out phase. Examples:
- `MediaStreamV1` → `MediaStreamV2` with a registered upgrade function
- `PackageA::Mesh` → `PackageB::Mesh` if both register as implementing a common "mesh" interface

**Mechanism:** Registry entries gain an optional `convert_from` table:
```cpp
typedef struct VividHandleConversion {
    uint32_t from_type_id;
    void (*convert)(const void* src, void* dst);  // must be trivial, no allocation
} VividHandleConversion;
```

**Deferred** — only worth building when the ecosystem has enough types to need it.

---

## ABI Impact Summary

| Phase | ABI change? | What changes |
|-------|-------------|--------------|
| 0 | No | Documentation only |
| 1 | Yes (minor) | Add `handle_payload_size` to `VividPortDescriptor` |
| 2 | Yes (minor) | Add `handle_type_name` to `VividPortDescriptor` |
| 3 | Yes | New `vivid_register_types` export, registry API |
| 4 | Yes | Conversion table API |

Phases 1+2 should be combined into a single ABI bump. Phase 3 is additive (new optional export). Phase 4 extends Phase 3's API.

---

## Open Questions

1. **Should Phases 1+2 bump to ABI v2, or can we add trailing fields to VividPortDescriptor without a version bump?** Trailing fields in a C struct are safe if old code doesn't read them, but the runtime reads all fields. Probably needs a bump.

2. **Should the registry be process-global or per-graph?** Process-global is simpler and matches how packages are loaded. Per-graph would allow different graphs to have different type environments, but adds complexity for no clear benefit.

3. **Should `vivid_register_types` be required or optional?** Optional (backward compatible) — packages without custom handle types don't export it. The runtime discovers types from operator descriptors as a fallback.

4. **HandleInputSnapshot is 256 bytes — is that enough for community types?** VividMidiBuffer is already ~520 bytes (64 messages × 8 bytes). If it needs audio-domain delivery, the limit must increase or become configurable. This should be addressed before Phase 3.

5. **Should handle types support inheritance/interfaces?** E.g. "this type implements the Mesh interface." Probably not in v1 of the registry — keep it flat. Conversion functions (Phase 4) cover the use case without type hierarchy complexity.

---

## Key Files

- `src/operator_api/types.h` — VividPortDescriptor struct (ABI surface)
- `src/operator_api/type_id.h` — vivid_type_id, VIVID_HANDLE_PORT macro
- `src/runtime/scheduler.cpp` — wire validation, handle delivery
- `src/runtime/scheduler.h` — NodeState, Wire structs
- `src/runtime/audio_engine.h` — HandleInputSnapshot (256-byte buffer)
- `src/runtime/audio_engine.cpp` — cross-domain handle copy (hardcoded sizeof)
- `src/runtime/package_manager.h` — package loading
- `src/runtime/operator_registry.h` — operator probing/caching
- `src/ui/node_graph_util.h` — port_type_compatible()
- `src/ui/graph_snapshot.h` — PortInfo (MCP/tooling exposure)
