# Native IDE Module (vivid-ide)

## Executive Summary

Native terminal and code editor integrated into Vivid, rendering via OverlayCanvas. This replaces the CEF-based approach (rejected due to GPU sharing issues) and WebView approach (rejected due to platform inconsistencies).

**Key decisions:**
- **Terminal:** libtmt (minimal VT100 emulator, ~500 lines C)
- **Editor:** Simple native editor initially, Zep integration later
- **Rendering:** OverlayCanvas (same as chain visualizer)
- **Architecture:** Separate `vivid-ide` module, dynamically loaded like `vivid-visualizer`

**Why native instead of CEF/WebView:**
- No +100MB binary size increase
- No GPU texture sharing issues on macOS/Linux
- Same rendering path as visualizer (consistent look)
- Simpler architecture (no IPC, no subprocess)

---

## Milestones

### Milestone 1: Terminal Panel ✅ IN PROGRESS

**Goal:** Run shell commands and Claude Code inside Vivid window.

**Scope:**
- [x] Create vivid-ide module structure (CMakeLists.txt, module.json)
- [x] Vendor libtmt for VT100 emulation
- [x] TerminalPanel class with PTY integration
- [ ] ide_exports.cpp for dynamic loading
- [ ] app.cpp integration to load/render vivid-ide
- [ ] Keyboard routing (when terminal focused, keys go to PTY)

**Files:**
```
modules/vivid-ide/
├── CMakeLists.txt                    ✅ Created
├── module.json                       ✅ Created
├── deps/libtmt/tmt.{c,h}            ✅ Vendored
├── include/vivid/ide/
│   ├── terminal_panel.h             ✅ Created
│   ├── editor_panel.h               ✅ Created
│   └── ide_panel.h                  ✅ Created
└── src/
    ├── terminal_panel.cpp           ✅ Created
    ├── editor_panel.cpp             ✅ Created
    ├── ide_panel.cpp                ⬜ TODO
    └── ide_exports.cpp              ⬜ TODO
```

**Success criteria:**
1. `cmake --build build` compiles vivid-ide module
2. Press backtick (`) to show terminal panel
3. Terminal spawns user's default shell
4. Can type commands, see output
5. Can run `claude` (Claude Code) in terminal
6. Ctrl+C sends SIGINT to running process

**Implementation notes:**
- libtmt handles VT100 escape sequence parsing
- PTY class already exists at `src/cli/include/vivid/pty.h`
- Terminal renders each character cell via `canvas.text()`
- ANSI colors mapped to glm::vec4 palette

---

### Milestone 2: Basic Editor Panel

**Goal:** Edit chain.cpp with syntax highlighting, trigger hot-reload on save.

**Scope:**
- [ ] EditorPanel renders text file with line numbers
- [ ] Basic cursor movement (arrows, home/end, page up/down)
- [ ] Text insertion/deletion (typing, backspace, delete)
- [ ] C++/WGSL keyword highlighting
- [ ] Comment and string highlighting
- [ ] Cmd+S saves file and triggers hot-reload
- [ ] Error line highlighting from compile failures

**Files:**
- `src/editor_panel.cpp` - Expand existing stub

**Success criteria:**
1. Press Tab to switch to editor tab
2. Opens chain.cpp from current project
3. Syntax highlighting for keywords, comments, strings
4. Can navigate with arrow keys
5. Can type to insert text
6. Cmd+S saves and triggers hot-reload
7. Compile error highlights the error line

**Implementation notes:**
- Simple line-based text buffer (vector of strings)
- No undo/redo in M2 (added in M4 with Zep)
- Highlighting via keyword matching, not full parser
- Error info comes from hot-reload system

---

### Milestone 3: IdePanel Orchestrator

**Goal:** Tab bar, panel dragging/resizing, keyboard shortcuts.

**Scope:**
- [ ] Tab bar UI: Terminal | Editor
- [ ] Click tabs to switch
- [ ] Cmd+1/2 to switch tabs
- [ ] Cmd+` to toggle IDE visibility
- [ ] Draggable title bar
- [ ] Resizable edges (left, right, bottom)
- [ ] Escape returns focus to chain output

**Files:**
- `src/ide_panel.cpp` - Main orchestrator

**Success criteria:**
1. Tab bar shows Terminal and Editor tabs
2. Clicking tab switches content
3. Cmd+1 shows terminal, Cmd+2 shows editor
4. Can drag panel by title bar
5. Can resize panel by dragging edges
6. Escape unfocuses panel (keys go to chain)

**Implementation notes:**
- Similar to inspector panel in chain_visualizer
- Track bounds, handle hit testing
- Store panel state for restore on relaunch (optional)

---

### Milestone 4: Zep Editor Integration (Optional)

**Goal:** Replace simple editor with Zep for professional editing features.

**Scope:**
- [ ] Vendor Zep library
- [ ] Write ZepDisplay_OverlayCanvas backend
- [ ] Vim mode support
- [ ] Proper undo/redo
- [ ] Multiple buffers
- [ ] Find/replace

**Files:**
- `deps/zep/` - Vendored Zep
- `src/zep_display_overlay.cpp` - Rendering backend
- `src/editor_panel.cpp` - Replace with Zep integration

**Success criteria:**
1. All M2 features still work
2. Can use vim keybindings (i, Esc, :w, etc.)
3. Undo/redo with Cmd+Z / Cmd+Shift+Z
4. Cmd+F opens find dialog

**Implementation notes:**
- Zep is rendering-agnostic, needs display backend
- ZepDisplay_OverlayCanvas implements Zep's display interface
- About 500-1000 lines for the backend

---

### Milestone 5: Contour Terminal (Optional)

**Goal:** Replace libtmt with Contour vtbackend for full xterm compatibility.

**Scope:**
- [ ] Vendor Contour vtbackend/vtparser/vtpty
- [ ] True color (24-bit RGB) support
- [ ] Unicode grapheme clusters
- [ ] Better vim/emacs compatibility

**Files:**
- `deps/contour/` - Vendored Contour libs
- `src/terminal_panel.cpp` - Replace libtmt with Contour

**Success criteria:**
1. All M1 features still work
2. True color output (e.g., `colortest` shows gradients)
3. Complex TUI apps work (htop, vim with plugins)

**Implementation notes:**
- Contour is C++20, may need compiler flag updates
- More complex than libtmt but production-quality
- Only needed if users report libtmt limitations

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Vivid Window (WebGPU)                                       │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Chain Output (main render)                              │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ vivid-visualizer (if enabled)                           │ │
│ │   Node Graph | Inspector Panel                          │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ vivid-ide (if enabled)                                  │ │
│ │ ┌─────────────────────────────────────────────────────┐ │ │
│ │ │ Tab Bar: [Terminal] [Editor]                        │ │ │
│ │ ├─────────────────────────────────────────────────────┤ │ │
│ │ │ Terminal Panel (libtmt + PTY)                       │ │ │
│ │ │  or                                                 │ │ │
│ │ │ Editor Panel (text buffer + syntax highlighting)    │ │ │
│ │ └─────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Loading sequence:**
1. app.cpp loads vivid-ide.dylib via dlopen (like vivid-visualizer)
2. Calls `vivid_ide_init()` to create IdePanel
3. Each frame: `vivid_ide_update()` processes PTY, `vivid_ide_render()` draws panel
4. Keyboard events routed based on focus state

**Exclusion for production:**
- Don't link vivid-ide in VIVID_PRODUCTION builds
- `vivid bundle` excludes the library from app bundles

---

## Key Interfaces

### C-linkage exports (ide_exports.cpp)

```cpp
extern "C" {
    void vivid_ide_init(WGPUDevice, WGPUQueue, WGPUTextureFormat);
    void vivid_ide_shutdown();
    void vivid_ide_update();
    void vivid_ide_render(WGPURenderPassEncoder, const FrameInput*, float w, float h);
    bool vivid_ide_consumed_input();
    bool vivid_ide_is_visible();
    void vivid_ide_set_visible(bool);
    void vivid_ide_toggle_visible();
    void vivid_ide_set_working_dir(const char*);
    void vivid_ide_open_file(const char*);
    void vivid_ide_set_compile_status(bool success, const char* message);
    void vivid_ide_on_char(uint32_t codepoint);
    void vivid_ide_on_key(int key, int mods);
}
```

### TerminalPanel

```cpp
class TerminalPanel {
    bool init(int cols, int rows);
    bool spawn(const std::string& shell, const std::string& workingDir);
    void update();  // Read PTY, feed to libtmt
    void render(OverlayCanvas&, const glm::vec4& bounds, int fontIndex);
    void onChar(uint32_t codepoint);
    void onKeyDown(int key, int mods);
    void resize(int cols, int rows);
};
```

### EditorPanel

```cpp
class EditorPanel {
    bool init();
    bool openFile(const std::string& path);
    bool save();
    void render(OverlayCanvas&, const glm::vec4& bounds, int fontIndex);
    void onChar(uint32_t codepoint);
    void onKeyDown(int key, int mods);
    void setError(int line, const std::string& message);
};
```

---

## Dependencies

| Library | Version | License | Size | Purpose |
|---------|---------|---------|------|---------|
| libtmt | master | BSD-3 | ~16KB | VT100 terminal emulation |
| Zep (M4) | master | MIT | ~200KB | Code editor core |
| Contour (M5) | master | Apache 2.0 | ~500KB | Full xterm emulation |

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| libtmt too limited | Can't run vim/htop properly | Upgrade to Contour in M5 |
| Editor too basic | Users want more features | Zep in M4, or just use VS Code |
| Font rendering issues | Characters misaligned | Test with JetBrains Mono, add kerning |
| PTY issues on Windows | Terminal doesn't work | ConPTY integration (already in pty.cpp) |

---

## Current Progress

**Milestone 1: COMPLETE**
- [x] Module structure (CMakeLists.txt, module.json)
- [x] Header files for all panels
- [x] libtmt vendored from GitHub
- [x] TerminalPanel implementation with PTY integration
- [x] EditorPanel implementation (basic text editing)
- [x] IdePanel orchestrator with tab bar, drag/resize
- [x] ide_exports.cpp (C-linkage for dynamic loading)
- [x] app.cpp updates (load module, route input, toggle visibility)
- [x] Font loading via FontAtlas
- [x] Build successfully compiles

**Verification needed:**
- Visual test: Does the panel render correctly?
- Terminal test: Can you type commands?
- Editor test: Can you edit text?
- Input routing: Do keystrokes go to the right panel?

**Remaining for M2 (Editor Polish):**
- Improve syntax highlighting
- Add error line highlighting from compile failures
- Cmd+S triggers hot-reload
- Scroll to error on compile fail

---

## Testing Plan

### M1 Terminal Tests
1. Start Vivid with `--show-ui`
2. Press backtick to show IDE panel
3. Verify shell prompt appears
4. Type `ls` and press Enter - verify output
5. Type `echo "hello"` - verify hello appears
6. Run `claude` - verify Claude Code starts
7. Ctrl+C while something is running - verify it interrupts
8. Close Vivid - verify clean shutdown

### M2 Editor Tests
1. Switch to Editor tab
2. Verify chain.cpp content displayed
3. Arrow keys move cursor
4. Type characters - verify insertion
5. Backspace deletes
6. Cmd+S saves (check file on disk)
7. Introduce syntax error, save, verify error highlight

---

## Related Documents

- `dev/plans/EMBEDDED_EDITOR_TERMINAL.md` - Previous WebView-based approach
- `dev/plans/VIVID-CEF-PLAN.md` - CEF approach (abandoned)
- `modules/vivid-visualizer/` - Reference for module pattern
