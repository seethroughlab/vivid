# ImGui Architecture Update

Analysis and implementation plan for making visualization optional in Vivid.

---

## Current Architecture

### Where ImGui Lives

| Location | Purpose |
|----------|---------|
| `core/imgui/imgui_integration.cpp` | WebGPU backend, frame management |
| `core/imgui/chain_visualizer.cpp` | Node editor, thumbnails, inspector |
| `core/src/main.cpp` | Runtime integration, event loop |
| `core/include/vivid/operator.h` | `ImDrawList*` forward declaration |

### Dependency Structure

```
vivid-core (shared library)
├── Compiles: ImGui + ImNodes sources
├── Exports: imgui_integration.h, ImDrawList forward decl
└── Does NOT include chain_visualizer (lives in executable)

vivid executable
├── Links: vivid-core, vivid-render3d, vivid-audio
├── Contains: chain_visualizer.cpp (depends on render3d)
└── Owns: Window, ImGui context, visualization

vivid-audio addon
├── 45+ operators implement drawVisualization()
├── Uses ImDrawList* parameter (no imgui include)
└── Works because ImDrawList is forward-declared

vivid-render3d addon
├── No drawVisualization implementations
├── Chain visualizer creates GeometryPreview instances
└── No imgui dependency
```

**Key insight**: Addons already don't depend on imgui headers - they just implement a virtual method that receives an `ImDrawList*`.

---

## Questions & Decisions

### 1. Move imgui to addons/vivid-gui?

**Decision: No**

The coupling is already minimal. Moving it would:
- Add complexity without benefit
- Still require vivid-gui as executable dependency
- Not enable headless builds (that's a separate concern)

### 2. Make drawVisualization optional?

**Decision: Yes**

Currently all 60+ operators with custom visualization implement:
```cpp
void drawVisualization(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) override;
```

This should be a no-op when running headless.

### 3. Make chain visualization optional?

**Decision: Yes**

Enable headless mode for:
- CLI video rendering/export
- Audio processing pipelines
- Server-side batch processing
- Embedding vivid in other apps

---

## Implementation Plan

### Phase 1: Runtime Headless Mode

Add `--headless` flag that skips window creation and all visualization.

#### Files to Modify

| File | Changes |
|------|---------|
| `core/src/main.cpp` | Add `--headless` flag, skip window/imgui init |
| `core/include/vivid/context.h` | Add `isHeadless()` accessor |
| `core/src/context.cpp` | Store headless state |

#### main.cpp Changes

```cpp
// Parse --headless flag
bool headless = false;
for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--headless") {
        headless = true;
    }
}

// Skip window creation in headless mode
GLFWwindow* window = nullptr;
if (!headless) {
    window = glfwCreateWindow(...);
    imgui::init(window, device, swapChainFormat);
}

// Main loop
while (running) {
    if (!headless) {
        glfwPollEvents();
        imgui::beginFrame(frameInput);
        chainVisualizer.render(frameInput, ctx);
        imgui::render(renderPass);
        // present to screen
    }

    // Always process chain (headless or not)
    userUpdate(ctx);

    if (headless) {
        // Check for completion (frame count, export done, etc.)
    }
}
```

#### Context Changes

```cpp
// context.h
class Context {
public:
    bool isHeadless() const { return m_headless; }
    void setHeadless(bool h) { m_headless = h; }
private:
    bool m_headless = false;
};
```

### Phase 2: Skip drawVisualization Calls

Chain visualizer already only calls `drawVisualization` during render. In headless mode, `chainVisualizer.render()` is never called, so no changes needed to operators.

However, for clarity and potential future use (e.g., visualization to file), add a guard:

```cpp
// chain_visualizer.cpp
void ChainVisualizer::renderOperatorNode(Operator* op, ...) {
    // ... existing code ...

    // Only call drawVisualization if we have a valid draw list
    if (drawList && !ctx.isHeadless()) {
        op->drawVisualization(drawList, pos, size);
    }
}
```

### Phase 3: Headless Video Export

The primary use case for headless mode is video export. Ensure it works:

```bash
./vivid myproject --headless --export output.mp4 --frames 300
```

#### Export Flow (Headless)

1. Parse `--headless --export <path> --frames <N>`
2. Initialize WebGPU (still needed for rendering)
3. Skip window/imgui initialization
4. Run chain.process() for N frames
5. VideoExporter captures each frame
6. Exit when complete

#### Files to Modify

| File | Changes |
|------|---------|
| `core/src/main.cpp` | Combine --headless with --export logic |
| `core/src/video_exporter.cpp` | Ensure works without window |

### Phase 4: Compile-Time Headless (Optional, Future)

For truly minimal builds (no imgui linked at all):

```cmake
option(VIVID_HEADLESS_ONLY "Build without ImGui/visualization" OFF)

if(NOT VIVID_HEADLESS_ONLY)
    # Fetch and compile ImGui
    FetchContent_Declare(imgui ...)
    # ... imgui sources ...
endif()
```

This would require:
- Conditional compilation in main.cpp
- Stub `ImDrawList` type when imgui not available
- Separate CMake target or build configuration

**Recommendation**: Defer Phase 4 until there's a concrete need. Runtime headless (Phases 1-3) covers most use cases.

---

## Testing Plan

1. **Basic headless**: `./vivid examples/getting-started/02-hello-noise --headless` (should exit after a few frames or run indefinitely)

2. **Headless snapshot**: `./vivid examples/... --headless --snapshot out.png`

3. **Headless export**: `./vivid examples/... --headless --export out.mp4 --frames 100`

4. **Normal mode unchanged**: Verify visualization still works normally

---

## Implementation Order

1. **Phase 1**: Add `--headless` flag and skip window creation (~30 min)
2. **Phase 2**: Guard drawVisualization calls (~10 min)
3. **Phase 3**: Test headless export workflow (~20 min)
4. **Phase 4**: Defer (compile-time option)

Total estimate: ~1 hour for runtime headless mode.

---

## Open Questions

1. **Exit condition in headless mode**: What determines when to exit?
   - Option A: Exit after `--frames N`
   - Option B: Exit when export completes
   - Option C: Run until SIGINT
   - Recommendation: Support all three (A for testing, B for export, C for servers)

2. **Audio in headless mode**: Should audio output work?
   - Probably yes for audio export
   - Maybe configurable (`--no-audio`)

3. **WebGPU in headless**: Need GPU for rendering even without display
   - Works on macOS/Linux with offscreen context
   - May need `--software-renderer` fallback for CI
