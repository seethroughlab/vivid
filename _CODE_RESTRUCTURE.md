# Vivid Code Restructuring Plan

Comprehensive plan to make the project architecture less confusing and more conventional.

---

## Current Architecture Problems

### Problem 1: Unclear "Runtime" vs "Core" Distinction

**Current state**: The `core/` folder produces BOTH:
- `libvivid-core.dylib` (shared library with operators, chain, context)
- `vivid` executable (main loop, hot-reload, visualization, CLI)

**Why it's confusing**: No clear separation between "the library you link against" and "the application that runs your chains."

### Problem 2: Chain Visualizer Lives in Executable

**Current state**: `chain_visualizer.cpp` is compiled into the executable, not the library.

**Why**: It depends on `vivid-render3d` for 3D geometry previews, creating a circular dependency if it were in core.

**Why it's confusing**: Mental model says "core has all the code" but visualization is in the executable.

### Problem 3: Mixed Responsibilities in main.cpp

**Current state**: `core/src/main.cpp` (1,400+ lines) contains:
- CLI argument parsing
- Window/GLFW management
- WebGPU initialization
- Hot-reload orchestration
- Event loop
- Editor bridge (WebSocket server)
- Memory debugging
- Platform-specific code

**Why it's confusing**: Too many responsibilities in one file. Hard to understand or modify.

### Problem 4: No Headless Mode

**Current state**: Always creates a window, always runs visualization.

**Why it's a problem**: Can't run video export on headless servers, can't embed in other apps.

---

## Proposed Architecture

```
vivid/
├── core/                      # Shared library (libvivid-core)
│   ├── include/vivid/         # Public API headers
│   ├── src/                   # Library implementation
│   │   ├── chain.cpp
│   │   ├── context.cpp
│   │   ├── operator.cpp
│   │   ├── audio_*.cpp
│   │   └── effects/           # 25+ 2D effects
│   ├── imgui/                 # Visualization (MOVE chain_visualizer here)
│   │   ├── imgui_integration.cpp
│   │   └── chain_visualizer.cpp  # With graceful 3D degradation
│   └── shaders/
│
├── runtime/                   # Executable source (NEW FOLDER)
│   ├── main.cpp               # Entry point only (~100 lines)
│   ├── app.cpp                # Application class (window, loop)
│   ├── cli.cpp                # Argument parsing
│   ├── hot_reload.cpp         # Dynamic chain loading
│   └── editor_bridge.cpp      # IDE integration
│
├── addons/                    # Optional shared libraries
│   ├── vivid-audio/
│   ├── vivid-video/
│   ├── vivid-render3d/
│   ├── vivid-midi/
│   ├── vivid-network/
│   └── vivid-serial/
│
└── examples/
```

---

## Implementation Plan

### Phase 1: Create runtime/ Folder (Low Risk)

Move executable-specific code out of core/src/.

**Files to move**:
| From | To |
|------|-----|
| `core/src/main.cpp` | `runtime/main.cpp` |
| `core/src/cli.cpp` | `runtime/cli.cpp` |
| `core/src/hot_reload.cpp` | `runtime/hot_reload.cpp` |
| `core/src/editor_bridge.cpp` | `runtime/editor_bridge.cpp` |

**CMake changes**:
```cmake
# core/CMakeLists.txt - remove main.cpp from library sources
add_library(vivid-core SHARED
    src/chain.cpp
    src/context.cpp
    # ... but NOT main.cpp, cli.cpp, etc.
)

# runtime/CMakeLists.txt - new file
add_executable(vivid
    main.cpp
    app.cpp
    cli.cpp
    hot_reload.cpp
    editor_bridge.cpp
)
target_link_libraries(vivid PRIVATE vivid-core vivid-render3d vivid-audio vivid-video)
```

**Estimated effort**: 2-3 hours

---

### Phase 2: Move Chain Visualizer to Core (Medium Risk)

Make visualization work without render3d dependency.

**Current dependency chain**:
```
chain_visualizer.cpp
  → includes vivid/render3d/render3d.h
  → can't be in core (would create circular dep)
```

**Solution**: Use runtime interface for 3D previews

**Step 2a**: Create abstract preview interface in core
```cpp
// core/include/vivid/geometry_preview.h
class IGeometryPreview {
public:
    virtual ~IGeometryPreview() = default;
    virtual void render(Operator* op, const ImVec2& pos, const ImVec2& size) = 0;
};

// Set by runtime after linking render3d
void setGeometryPreviewProvider(std::function<IGeometryPreview*(Operator*)> factory);
```

**Step 2b**: Update chain_visualizer to use interface
```cpp
// Instead of: #include <vivid/render3d/render3d.h>
// Use: if (auto preview = getGeometryPreview(op)) preview->render(...);
```

**Step 2c**: Runtime registers the render3d implementation
```cpp
// runtime/main.cpp
vivid::setGeometryPreviewProvider([](Operator* op) {
    return new Render3DPreview(op);
});
```

**Estimated effort**: 4-6 hours

---

### Phase 3: Add Headless Mode (Low Risk)

Add `--headless` flag that skips window/visualization.

**Changes to runtime/main.cpp**:
```cpp
bool headless = args.has("--headless");

if (!headless) {
    window = createWindow();
    imgui::init(window, device);
}

while (running) {
    if (!headless) {
        glfwPollEvents();
        visualizer.render(ctx);
        present();
    }

    chain.process(ctx);

    if (headless && exportComplete) break;
}
```

**Changes to Context**:
```cpp
class Context {
    bool m_headless = false;
public:
    bool isHeadless() const { return m_headless; }
};
```

**Estimated effort**: 1-2 hours

---

### Phase 4: Split main.cpp (Low Risk)

Break the 1,400-line main.cpp into focused files.

**New file structure**:
| File | Responsibility | Lines |
|------|----------------|-------|
| `main.cpp` | Entry point, parse args, call run() | ~50 |
| `app.cpp` | Application class with init/run/shutdown | ~400 |
| `window.cpp` | GLFW window management | ~200 |
| `renderer.cpp` | WebGPU setup, frame rendering | ~300 |
| `cli.cpp` | Already exists, argument parsing | ~300 |
| `hot_reload.cpp` | Already exists | ~400 |

**Estimated effort**: 3-4 hours

---

## Implementation Order

| Phase | Description | Risk | Effort | Dependencies |
|-------|-------------|------|--------|--------------|
| 1 | Create runtime/ folder | Low | 2-3h | None |
| 3 | Add headless mode | Low | 1-2h | None |
| 4 | Split main.cpp | Low | 3-4h | Phase 1 |
| 2 | Move visualizer to core | Medium | 4-6h | Phase 1 |

**Recommended order**: 1 → 3 → 4 → 2

Total estimated effort: ~12-15 hours

---

## Testing Plan

After each phase:
1. Build succeeds
2. `./vivid examples/getting-started/02-hello-noise` runs normally
3. Hot-reload works (edit chain.cpp, see changes)
4. Visualization works (nodes, thumbnails, audio waveforms)
5. Video export works
6. All existing tests pass

After Phase 3:
- `./vivid examples/... --headless --snapshot out.png` works
- `./vivid examples/... --headless --export out.mp4` works

---

## Files to Modify

### Phase 1 Files
- `core/CMakeLists.txt` - Remove executable sources from library
- `runtime/CMakeLists.txt` - New file for executable
- `CMakeLists.txt` (root) - Add runtime subdirectory
- Move: main.cpp, cli.cpp, hot_reload.cpp, editor_bridge.cpp

### Phase 2 Files
- `core/include/vivid/geometry_preview.h` - New interface
- `core/imgui/chain_visualizer.cpp` - Use interface instead of direct render3d
- `runtime/main.cpp` - Register render3d preview provider

### Phase 3 Files
- `runtime/main.cpp` - Add --headless flag
- `core/include/vivid/context.h` - Add isHeadless()
- `core/src/context.cpp` - Implement isHeadless()

### Phase 4 Files
- `runtime/main.cpp` - Slim down to entry point
- `runtime/app.cpp` - New file
- `runtime/window.cpp` - New file
- `runtime/renderer.cpp` - New file

---

## Open Questions

1. **Hot-reload location**: Should hot_reload.cpp stay in core (useful for asset watching) or move to runtime?
   - Recommendation: Move to runtime initially, but keep file watching utilities in core

2. **Display.cpp**: Currently in core/src/. Is it library or executable code?
   - Appears to be frame rendering - investigate if it belongs in runtime

3. **Window manager**: There's window_manager.cpp in core. Duplicate of window logic?
   - Investigate relationship with main.cpp window code

4. **Platform files**: platform_macos.mm, platform_stub.cpp - where should they live?
   - Probably stay in core since they're used by library code
