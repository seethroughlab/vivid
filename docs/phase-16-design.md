# Phase 16 Design: Operator Creation

The `scaffold_operator` MCP tool and the in-app `+ New Operator...` UI both delegate to a shared `OperatorCreator` module. This document specifies that module, the UI flow, and the build integration.

## OperatorCreator Module

`src/runtime/operator_creator.h` / `operator_creator.cpp` — standalone, no UI dependencies.

```cpp
struct CreateOperatorResult {
    bool success;
    std::string error;        // empty on success
    std::string cpp_path;     // e.g. operators/audio/filter/filter.cpp
    std::string target_name;  // e.g. filter
};

class OperatorCreator {
public:
    // Validate name: returns empty string on success, error message on failure
    static std::string validate_name(const std::string& name, const OperatorRegistry& reg);

    // Create directory, write .cpp from template, patch CMakeLists.txt
    static CreateOperatorResult create(const std::string& name, VividDomain domain,
                                       const std::string& src_dir);

    // Open file in $VISUAL/$EDITOR/open (async, non-blocking)
    static void open_in_editor(const std::string& path);
};
```

### Name Handling

- Input: lowercase with underscores (e.g. `tone_gen`). Validated as C++ identifier.
- Directory & CMake target: same as input (`tone_gen`)
- Struct name: PascalCase derived from input (`ToneGen`)
- Collision check: against `OperatorRegistry::type_names()` and filesystem

### Templates

Each generates a minimal compilable operator:

- **Control** (~25 lines): includes `operator.h`, one float param, float input/output ports, pass-through process. Based on `gain.cpp` pattern.
- **Audio** (~30 lines): adds `audio_operator.h`, audio buffer loop in process. Based on `gain.cpp`.
- **GPU** (~170 lines): full WebGPU pipeline boilerplate — inline WGSL shader (solid color + time uniform), Uniforms struct, `lazy_init()`, render pass, cleanup. Based on `shape.cpp` but stripped to minimal.

### CMakeLists.txt Patching

Find the insertion point by domain:
- Control operators: insert before `# --- GPU operator plugins ---`
- GPU operators: insert before `# --- Audio operator plugins ---`
- Audio operators: insert before `# --- Vivid executable ---`

Insert a blank line + 3 lines (4 for GPU which links `webgpu`):
```cmake
add_library(<name> MODULE operators/<domain>/<name>/<name>.cpp)
set_target_properties(<name> PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(<name> PRIVATE vivid_operator_api)
```

## In-App UI

### Trigger

A `+ New Operator...` entry at the top of the Tab chooser (always present, not filtered by search). Selecting it opens a create popup instead of calling `add_node`.

### Create Popup

Centered modal with:
- Domain selector (three items: `control`, `audio`, `gpu` — arrow keys or click to cycle)
- Name text field with blinking cursor
- Error text (red) if validation fails
- Visual style matching the existing chooser (same colors, text sizes)

### Input Handling

- Escape: close popup
- Left/Right arrows: cycle domain selector
- Typing: append to name buffer, run validation live
- Backspace: delete from name buffer
- Enter: if validation passes, create → build → open editor → close popup

### State (on NodeGraphUI)

```cpp
bool create_popup_open_ = false;
int create_domain_sel_ = 0;       // 0=control, 1=audio, 2=gpu
std::string create_name_buf_;
std::string create_error_;        // validation feedback
```

## Build Integration

Reuse the existing `HotReloader` pipeline. After `OperatorCreator::create()` writes the .cpp and patches CMakeLists.txt:

1. `hot_reloader.queue_rebuild(target_name)` — CMake auto-reconfigures when it detects CMakeLists.txt changed, so a single `cmake --build --target <name>` handles both configure and compile.
2. `OperatorCreator::open_in_editor(cpp_path)` — launches `$VISUAL` > `$EDITOR` > `open` (macOS fallback), async fire-and-forget.
3. Main loop's `poll_ready()` picks up the completed build.

### New Operator Loading

In the hot-reload poll loop, handle unknown targets (operator not yet in registry):

```cpp
if (!type_name_ptr) {
    // New operator — load its dylib into the registry
    std::string dylib_path = exe_dir.string() + "/" + result.target_name + ".dylib";
    if (registry.load_new(dylib_path)) {
        file_watcher.add_watch(/* new .cpp path */, result.target_name);
        fprintf(stderr, "[vivid] New operator '%s' loaded\n", result.target_name.c_str());
    }
    continue;
}
```

This requires:
- `OperatorRegistry::load_new(path)` — loads a single dylib, extracts descriptor, registers in `loaders_` and `target_to_type_`. Returns type name on success.
- `FileWatcher::add_watch(path, target)` — public method (currently private). kqueue `kevent()` is thread-safe for registration.

## Runtime Sequence

1. User presses Tab → chooser opens with `+ New Operator...` at top
2. User selects it → create popup opens
3. User picks domain (left/right arrows), types name, presses Enter
4. `OperatorCreator::create()` writes directory + .cpp + patches CMakeLists.txt
5. `hot_reloader.queue_rebuild(target_name)` — CMake auto-reconfigures then builds
6. `OperatorCreator::open_in_editor(cpp_path)` launches editor async
7. Popup closes
8. Main loop's `poll_ready()` picks up the completed build
9. Unknown target branch loads the new .dylib into the registry
10. FileWatcher registers the new .cpp for hot-reload
11. New operator appears in the chooser on next Tab press

## Files Summary

**Create:**
- `src/runtime/operator_creator.h` / `operator_creator.cpp`

**Modify:**
- `src/runtime/node_graph.h` — popup state, `wants_keyboard()`, declare `draw_create_popup()`
- `src/runtime/node_graph_draw.cpp` — `draw_create_popup()` implementation
- `src/runtime/node_graph_input.cpp` — chooser `+ New...` item handling, popup input
- `src/runtime/node_graph.cpp` — prepend `+ New Operator...` in `rebuild_chooser_items()`
- `src/runtime/main.cpp` — pass src/build dirs, hot_reloader pointer, new-operator loading branch
- `src/runtime/operator_registry.h/.cpp` — add `load_new()`
- `src/runtime/file_watcher.h/.cpp` — expose `add_watch()` publicly
- `CMakeLists.txt` — add `operator_creator.cpp` to vivid executable sources

## Verification

1. Press Tab — `+ New Operator...` appears at the top of the list
2. Select it — create popup appears with domain selector and name field
3. Type `test_op`, select `control`, press Enter
4. `operators/control/test_op/test_op.cpp` exists with correct template
5. `CMakeLists.txt` has the new target in the control section
6. Operator compiles (check stderr for hot-reload success message)
7. Editor opens the file
8. Press Tab again — `TestOp` appears in the chooser list
9. Add the operator to the graph — it works (has ports, processes)
10. Edit the .cpp in the editor, save — hot-reload triggers
11. Repeat for `audio` and `gpu` domains
