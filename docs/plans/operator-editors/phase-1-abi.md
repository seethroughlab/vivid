# Phase 1: Editor ABI

## Goal

Extend the operator ABI so any package can declare a dedicated editor for an operator, in the same way `VIVID_INSPECTOR` declares custom inspector drawing today. Load-time plumbing only — no windowing or host UI yet.

## Context

The existing ABI v1 surface (see `src/operator_api/operator.h` lines 451–728) already has a working precedent for optional operator-defined UI via `VIVID_INSPECTOR`:

```cpp
extern "C" uint32_t vivid_inspector_mode();
extern "C" void     vivid_draw_inspector(void* instance, VividInspectorContext* ctx);
```

`VividInspectorContext` (`src/operator_api/types.h` lines 437–462) bundles a `VividDrawAPI` (function pointers into `Renderer2D`), a command API (set_param/set_string_param), theme, current values, simplified mouse, and a key-event array.

The editor ABI is a superset of this: same draw API, richer input, bigger canvas, window metadata.

## Scope

- Add the `VIVID_EDITOR(ClassName)` macro and new exports.
- Define `VividEditorContext` and `VividEditorMetadata`.
- Bump the ABI version to **2**. Operators that do not use `VIVID_EDITOR` remain valid v1 — the host simply finds no editor symbols and does not offer an "Open Editor" button.
- Teach `OperatorLoader` to `dlsym` the new symbols and expose them on the loader struct.

Explicitly **not** in this phase: creating windows, routing input, drawing anything, changing any operator.

## Design

### New macro (`src/operator_api/operator.h`)

Modeled on `VIVID_INSPECTOR` (lines 712–728):

```cpp
#define VIVID_EDITOR(ClassName)                                                    \
    extern "C" uint32_t vivid_has_editor() { return 1u; }                          \
    extern "C" VividEditorMetadata vivid_editor_metadata() {                       \
        return ClassName::editor_metadata();                                       \
    }                                                                              \
    extern "C" void vivid_draw_editor(void* instance, VividEditorContext* ctx) {   \
        static_cast<_VividInstance*>(instance)->op.draw_editor(ctx);               \
    }
```

Operator class contract:

```cpp
static VividEditorMetadata editor_metadata();     // default size, min size, title
void draw_editor(VividEditorContext* ctx);        // per-frame draw
```

### New types (`src/operator_api/types.h`)

```c
typedef struct VividEditorMetadata {
    uint32_t default_width;   // px
    uint32_t default_height;
    uint32_t min_width;
    uint32_t min_height;
    const char* title_suffix; // e.g. "Drum Pattern Editor"; appended to node label
} VividEditorMetadata;

typedef struct VividEditorContext {
    // Surface
    float    surface_width;
    float    surface_height;
    float    dpi_scale;

    // Drawing and commands (reused from inspector ABI)
    VividDrawAPI             draw;
    VividInspectorCommandAPI commands;   // keep the existing struct; alias as VividCommandAPI later if desired
    VividInspectorTheme      theme;

    // Current operator state (read-only)
    const float*       param_values;        uint32_t param_count;
    const float*       output_values;       uint32_t output_count;
    const char* const* string_param_values; uint32_t string_param_count;

    // Full input event queue (superset of VividInspectorMouse + key arrays)
    const VividInputEvent* events;
    uint32_t               event_count;
    VividInspectorMouse    mouse;           // still provided for convenience (same semantics as inspector)

    // Clock
    double time;

    // Host-writable responses
    int wants_keyboard;   // operator sets 1 if it wants to consume arrow keys, etc.
    int request_close;    // operator sets 1 to ask the host to close the editor (e.g. internal button)
} VividEditorContext;
```

Bump `VIVID_OPERATOR_ABI_VERSION` from 1 to 2 in the same header. Document that v1 operators continue to load — the version is queried by the loader and the editor exports are optional.

### Loader plumbing (`src/runtime/operators/operator_loader.{h,cpp}`)

In the loader struct, add function-pointer members:

```cpp
using VividHasEditorFn      = uint32_t (*)();
using VividEditorMetaFn     = VividEditorMetadata (*)();
using VividDrawEditorFn     = void (*)(void* instance, VividEditorContext* ctx);

VividHasEditorFn  has_editor_fn_  = nullptr;
VividEditorMetaFn editor_meta_fn_ = nullptr;
VividDrawEditorFn draw_editor_fn_ = nullptr;
```

After the dylib is `dlopen`-ed, attempt `dlsym` on each of `vivid_has_editor`, `vivid_editor_metadata`, `vivid_draw_editor`. Missing symbols are not errors — they mean the operator has no editor.

Expose:

```cpp
bool                has_editor() const;          // true iff all three symbols resolved and vivid_has_editor() returned nonzero
VividEditorMetadata editor_metadata() const;     // defined only when has_editor()
void                draw_editor(void* instance, VividEditorContext* ctx);
```

## Files

| Change | Path |
|---|---|
| Add `VIVID_EDITOR` macro, bump `VIVID_OPERATOR_ABI_VERSION` | `src/operator_api/operator.h` |
| Add `VividEditorMetadata`, `VividEditorContext` | `src/operator_api/types.h` |
| Add dlsym + public accessors for editor symbols | `src/runtime/operators/operator_loader.h`, `src/runtime/operators/operator_loader.cpp` |
| Unit test: scaffolded operator with `VIVID_EDITOR` exports expected symbols | new test under `tests/` (mirror existing inspector-export test) |

## Acceptance Criteria

1. An operator that adds `VIVID_EDITOR(Foo)` compiles and exports `vivid_has_editor`, `vivid_editor_metadata`, `vivid_draw_editor`.
2. `OperatorLoader::has_editor()` returns true for such an operator and false for any existing operator.
3. `VIVID_OPERATOR_ABI_VERSION == 2`. Existing v1 operators (DrumSequencer, Tracker, MSEG, etc. before migration) still load and behave identically — no regressions in the inspector or in `ctest`.
4. Running `nm` / `dlsym`-probing against a test dylib confirms the three editor symbols exist only when `VIVID_EDITOR` is used.
5. A new test in `tests/` that scaffolds a minimal operator with `VIVID_EDITOR`, loads it, and asserts the loader reports `has_editor() == true` with the expected metadata.

## Dependencies

None. This phase ships independently and is useful on its own (a `has_editor()` flag the host can start reading without acting on it yet).

## Out of Scope for This Phase

- Any window creation, drawing, or input routing (Phase 2).
- Any host UI changes (Phase 3).
- Any operator migration (Phase 4).
