# Phase 1: Editor ABI

## Goal

Add a reusable operator-side editor contract without changing the operator ABI version. After this phase, the runtime can discover whether an operator exports a dedicated editor, surface that fact in `OperatorInfo`, and invoke the editor entry point once later phases supply a host window.

This phase is strictly about optional symbol discovery and metadata wiring. It does not create windows or add host UI.

## Current Repo Facts

- `VIVID_OPERATOR_ABI_VERSION` is defined in `src/operator_api/types.h` and is currently enforced as an exact match by `src/runtime/operators/operator_loader.cpp`.
- `VIVID_INSPECTOR` and `VIVID_THUMBNAIL` already establish the pattern for optional operator UI exports discovered through `dlsym`.
- `OperatorInfo` is the UI-facing metadata structure in `src/ui/graph/graph_snapshot.h`.
- `operator_info_cache.h` already translates loader capabilities into `OperatorInfo` fields such as `has_custom_inspector`.

These facts drive the design for this phase: add optional editor exports and metadata, but do not claim cross-version compatibility semantics the runtime does not actually implement.

## Locked Decisions For This Phase

1. Keep `VIVID_OPERATOR_ABI_VERSION` unchanged.
2. Add editor support as optional symbols, similar to inspector and thumbnail hooks.
3. Export only `vivid_editor_metadata` and `vivid_draw_editor`.
4. Expose editor capability both on `OperatorLoader` and on UI-facing `OperatorInfo`.
5. Use a dedicated editor event type with editor-local pixel coordinates rather than reusing normalized-UV `VividInputEvent`.

## Interface Additions

### `src/operator_api/operator.h`

Add a new macro:

```cpp
#define VIVID_EDITOR(ClassName)                                                \
extern "C" VividEditorMetadata vivid_editor_metadata() {                       \
    return ClassName::editor_metadata();                                       \
}                                                                              \
extern "C" void vivid_draw_editor(void* instance, VividEditorContext* ctx) {   \
    static_cast<_VividInstance*>(instance)->op.draw_editor(ctx);               \
}
```

Required operator contract:

```cpp
static VividEditorMetadata editor_metadata();
void draw_editor(VividEditorContext* ctx);
```

This phase should document that `VIVID_EDITOR` is optional and may appear alongside `VIVID_REGISTER`, `VIVID_INSPECTOR`, and `VIVID_THUMBNAIL`.

### `src/operator_api/types.h`

Add `VividEditorMetadata`:

```c
typedef struct VividEditorMetadata {
    uint32_t default_width;
    uint32_t default_height;
    uint32_t min_width;
    uint32_t min_height;
    const char* title_suffix;
} VividEditorMetadata;
```

Add a dedicated editor event type that is local to the editor window surface:

```c
typedef uint32_t VividEditorEventType;
#define VIVID_EDITOR_EVENT_MOUSE_MOVE    0u
#define VIVID_EDITOR_EVENT_MOUSE_BUTTON  1u
#define VIVID_EDITOR_EVENT_MOUSE_SCROLL  2u
#define VIVID_EDITOR_EVENT_KEY           3u
#define VIVID_EDITOR_EVENT_CHAR          4u

typedef struct VividEditorEvent {
    VividEditorEventType type;
    float x, y;          /* editor-local pixel coordinates */
    int button;
    int action;
    float scroll_dx, scroll_dy;
    int key;
    int scancode;
    uint32_t codepoint;
    int modifiers;
} VividEditorEvent;
```

Add an editor-local mouse snapshot. Keep semantics explicit: this is not normalized UV space and not inspector-relative content coordinates; it is editor-window-local pixel space.

```c
typedef struct VividEditorMouse {
    float x, y;
    float prev_x, prev_y;
    int left_down, left_clicked, left_released, right_clicked;
    int shift_down;
} VividEditorMouse;
```

Add `VividEditorContext`:

```c
typedef struct VividEditorContext {
    float surface_width;
    float surface_height;
    float dpi_scale;

    VividDrawAPI             draw;
    VividInspectorCommandAPI commands;
    VividInspectorTheme      theme;

    const float*       param_values;        uint32_t param_count;
    const float*       output_values;       uint32_t output_count;
    const char* const* string_param_values; uint32_t string_param_count;

    VividEditorMouse         mouse;
    const VividEditorEvent*  events;        uint32_t event_count;

    double time;
    int wants_keyboard;
    int request_close;
} VividEditorContext;
```

Document that editor events are delivered in local pixel coordinates because editor surfaces are arbitrary 2D layouts, not fitted texture outputs.

## Loader And Metadata Wiring

### `src/runtime/operators/operator_loader.{h,cpp}`

Add function pointers:

```cpp
using VividEditorMetadataFn = VividEditorMetadata (*)();
using VividDrawEditorFn = void (*)(void*, VividEditorContext*);
```

Resolve both with `dlsym` during load:

- `vivid_editor_metadata`
- `vivid_draw_editor`

Behavior requirements:

- Missing editor symbols are not errors.
- `has_editor()` returns true only when both symbols resolve.
- `editor_metadata()` is only valid when `has_editor()` is true.
- `draw_editor(...)` is a guarded no-op if editor symbols are absent or `instance == nullptr`.

### `src/ui/graph/graph_snapshot.h`

Extend `OperatorInfo` with:

```cpp
bool has_editor = false;
```

This is the field later phases will read from inspector and node-selection UI. The host UI should not have to talk to `OperatorLoader` directly for simple visibility decisions.

### `src/runtime/operators/operator_info_cache.h`

Populate `OperatorInfo::has_editor` from `loader->has_editor()` at the same time `has_custom_inspector` and `inspector_mode` are populated.

This phase should explicitly call out that editor capability is cached metadata, not an ad hoc loader query from every UI call site.

## Files To Change

| Change | Path |
|---|---|
| Add `VIVID_EDITOR` macro | `src/operator_api/operator.h` |
| Add editor metadata, event, mouse, and context types | `src/operator_api/types.h` |
| Add editor symbol lookup and accessors | `src/runtime/operators/operator_loader.{h,cpp}` |
| Add UI-facing editor capability field | `src/ui/graph/graph_snapshot.h` |
| Populate `OperatorInfo::has_editor` | `src/runtime/operators/operator_info_cache.h` |

## Tests

### Automated Coverage

Add or update tests under `tests/ops/` and adjacent metadata-cache coverage to verify:

1. A fixture operator exporting `VIVID_EDITOR` exposes `vivid_editor_metadata` and `vivid_draw_editor`.
2. `OperatorLoader::has_editor()` is true for that fixture and false for a fixture without editor exports.
3. `OperatorLoader::editor_metadata()` returns the expected dimensions and title suffix.
4. `OperatorInfo::has_editor` is true for the editor fixture and false for existing non-editor operators.
5. Existing operators still load because the ABI number is unchanged.

Avoid vague tests that only shell out to `nm`. Prefer loader-level tests that exercise the same symbol discovery the runtime will use.

### Manual QA

This phase has minimal manual QA. The only manual check worth requiring is that a locally built editor-exporting operator still loads normally in the app with no UI regressions, despite no editor window existing yet.

## Acceptance Criteria

1. `VIVID_OPERATOR_ABI_VERSION` is unchanged.
2. Operators may optionally export `vivid_editor_metadata` and `vivid_draw_editor` with `VIVID_EDITOR`.
3. `OperatorLoader::has_editor()` accurately reflects whether both editor symbols resolved.
4. `OperatorInfo::has_editor` is populated from the loader and available to the UI layer.
5. Existing operators without editor exports load unchanged.
6. Automated coverage exists for loader discovery and `OperatorInfo` propagation.

## Non-Goals

- Creating native windows.
- Rendering editor UI.
- Changing inspector behavior.
- Migrating any operator to use the new editor API.
