# Refactor Plan: Large File Decomposition

Audit date: 2026-04-02

## Summary

The core architecture is solid — operator plugin API, domain separation (Audio/GPU/Control), runtime layering, and test coverage are all in good shape. The pain is concentrated in the UI layer (`NodeGraphUI` god class) and two runtime files (`main.cpp`, `control_server.cpp`). This plan addresses these incrementally.

### The Big 6

| File | Lines | Core Issue |
|------|------:|------------|
| `src/ui/node_graph_draw.cpp` | 5,459 | All drawing code in one file |
| `src/runtime/main.cpp` | 4,800 | `main()` is 2,653 lines alone |
| `src/runtime/control_server.cpp` | 3,742 | HTTP routing + serialization + validation |
| `src/ui/node_graph_input.cpp` | 3,303 | `on_key()` is 997 lines |
| `src/ui/node_graph.cpp` | 2,361 | Implementation of god class |
| `src/ui/node_graph.h` | 1,097 | ~158 member vars, 28 boolean state flags, 140+ methods |

---

## Phase 1: Decompose NodeGraphUI (highest leverage)

- [ ] **Complete**

Extract state and methods into focused controllers. `NodeGraphUI` becomes a thin coordinator that delegates to these:

1. **InspectorController** — parameter editing, layout, custom inspectors
2. **SessionGridController** — variations, presets, quantization
3. **ChooserController** — operator chooser, search, categories
4. **DialogManager** — preferences, package browser, example browser, about, MCP setup (replace boolean flags with a modal stack)
5. **StickyNoteController** — note editing, positioning, rendering

**Notes:**


---

## Phase 2: Split main.cpp

- [ ] **Complete**

Extract from the 2,653-line `main()` function:

1. **Window/event setup** — GLFW init, monitor management, event callbacks
2. **UI test runner** — test script parsing and execution
3. **Capture pipeline** — screen capture, export coordination

**Notes:**


---

## Phase 3: Refactor input dispatch

- [ ] **Complete**

Replace the 997-line `on_key()` switch with a **command registry** — a table mapping (key, modifiers) to named commands.

Benefits: discoverable keybindings, remappable shortcuts, testable actions.

**Notes:**


---

## Phase 4: Split control_server.cpp

- [ ] **Complete**

Extract separable concerns:

1. **Graph serialization** — JSON snapshot building (`handle_inspect_graph`, `handle_introspect_nodes`)
2. **Check/diagnostic engine** — validation framework
3. Keep route dispatch in `control_server.cpp` as thin handler

**Notes:**


---

## Phase 5: Thumbnail framework (lower priority)

- [ ] **Complete**

~34 operators duplicate GPU pipeline setup for thumbnail rendering. Create `operator_api/thumbnail_helpers.h` with builders for common visualization types (waveform, spectrum, bar meter) to eliminate per-operator boilerplate.

**Notes:**


---

## Verification (after each phase)

- All existing tests pass (`ctest --test-dir build`)
- MCP tools still work (`inspect_graph`, `set_param`, etc.)
- UI renders and interacts correctly (manual smoke test)
- No new compiler warnings
