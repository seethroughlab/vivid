# Embedded Code Editor + Terminal in Vivid Runtime

## Goal

Build code editing (Monaco Editor) and terminal (xterm.js for Claude Code) directly into the Vivid runtime using the WebView module, eliminating the need for a separate vivid-ide Tauri app.

## Current State

**WebView module (macOS) now supports:**
- ✅ JavaScript execution and native callbacks
- ✅ Mouse input (click, drag, scroll) - fixed slider dragging
- ✅ Transparent overlays
- ✅ External JS/CSS loading
- ✅ Console.log bridging to stdout
- ✅ **Character input** - GLFW char callback → Context → WebView → InputEvent (Phase 1 complete)
- ✅ **Focus management** - click-to-focus, only focused WebView receives keyboard (Phase 2 complete)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Vivid Window                            │
├─────────────────────┬───────────────────────────────────────┤
│                     │  WebView (Monaco Editor)              │
│   Chain Output      │  - chain.cpp editing                  │
│   (rendered scene)  │  - Syntax highlighting                │
│                     │  - Error squiggles                    │
│                     ├───────────────────────────────────────┤
│                     │  WebView (xterm.js Terminal)          │
│   Node Graph        │  - Claude Code / LLM interaction      │
│   (optional)        │  - Shell access                       │
│                     │  - Build output                       │
└─────────────────────┴───────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: Character Input Support ✅ COMPLETE

**Problem:** WebView only receives `KeyEventType::Down/Up` with GLFW key codes, not actual characters.

**Solution:** Hook GLFW character callback and forward to WebView.

**Files modified:**
- `src/cli/app.cpp` - Added `glfwSetCharCallback`
- `modules/vivid-core/include/vivid/context.h` - Added `characterInput()`, `addCharacter()` methods
- `modules/vivid-core/src/context.cpp` - Clear character queue in `endFrame()`
- `modules/vivid-webview/src/webview.cpp` - Forward characters via `sendKeyEvent(KeyEventType::Char, ...)`
- `modules/vivid-webview/src/webview_macos.mm` - Dispatch `InputEvent` with `beforeinput`/`input` for text insertion

**Implementation details:**
- Full Unicode support including emoji (surrogate pairs for characters outside BMP)
- Proper JavaScript escaping for special characters
- Uses `InputEvent` instead of deprecated `keypress` for better Monaco/xterm.js compatibility

### Phase 2: Focus Management ✅ COMPLETE

**Problem:** Multiple WebViews (editor + terminal) need proper focus tracking.

**Solution:** Track which WebView has focus and route keyboard input only to focused view.

**Files modified:**
- `modules/vivid-webview/include/vivid/webview/webview.h` - Added `hasFocus()`, `requestFocus()`, `releaseFocus()`, static `focusedWebView()`
- `modules/vivid-webview/src/webview.cpp` - Implemented focus methods, click-to-focus in `handleInputEvents()`

**Implementation details:**
- Static `s_focusedWebView` tracks the globally focused WebView
- `requestFocus()` unfocuses previous WebView and focuses new one
- Click inside WebView bounds calls `requestFocus()`
- Keyboard/character events only forwarded if `hasFocus()` returns true
- Focus released automatically in `cleanup()` when WebView is destroyed

### Phase 3: Editor Panel Integration

**HTML/JS for Monaco Editor:**
```
modules/vivid-webview/examples/code-editor/
├── assets/
│   ├── editor.html
│   ├── editor.js
│   └── editor.css
└── chain.cpp
```

**Features:**
- Load Monaco Editor from CDN or bundled
- `window.vivid.loadFile(path)` - Request file content from Vivid
- `window.vivid.saveFile(path, content)` - Write file back
- `window.vivid.onCompileError(callback)` - Receive error updates
- Error decorations on lines with compile errors

**Native side:**
```cpp
editor.registerCallback("loadFile", [](const std::string& args) {
    auto path = json::parse(args)["path"];
    std::string content = readFile(path);
    editor.executeJS("setContent(" + jsonEscape(content) + ")");
});

editor.registerCallback("saveFile", [](const std::string& args) {
    auto data = json::parse(args);
    writeFile(data["path"], data["content"]);
    // Hot-reload will detect file change automatically
});
```

### Phase 4: Terminal Integration

**HTML/JS for xterm.js:**
```
modules/vivid-webview/examples/terminal/
├── assets/
│   ├── terminal.html
│   ├── terminal.js
│   └── terminal.css
└── chain.cpp
```

**Features:**
- PTY integration via native callback
- `window.vivid.terminalInput(data)` - Send user input to PTY
- `window.vivid.onTerminalOutput(callback)` - Receive PTY output

**Native side (requires PTY handling):**
```cpp
// Fork PTY process (or connect to existing Claude Code session)
terminal.registerCallback("terminalInput", [&pty](const std::string& data) {
    pty.write(data);
});

// In update loop
std::string output = pty.read();
if (!output.empty()) {
    terminal.executeJS("terminalOutput(" + jsonEscape(output) + ")");
}
```

### Phase 5: MCP Integration

Extend existing MCP server with editor-specific tools:

```cpp
// New MCP tools in mcp_server.cpp
"read_file" -> readFile(path) -> content
"write_file" -> writeFile(path, content)
"get_compile_errors" -> getErrorsJson()
"focus_editor" -> bring editor panel to front
```

This allows Claude Code (in terminal WebView) to:
1. Read chain.cpp
2. Edit it
3. Write changes
4. Check compile status
5. See errors

## Files to Create/Modify

**New files:**
```
modules/vivid-webview/examples/code-editor/
modules/vivid-webview/examples/terminal/
modules/vivid-webview/src/pty.cpp (or pty_macos.mm)
```

**Modify:**
```
modules/vivid-core/src/window_manager.cpp    # Character callback
modules/vivid-core/src/input_manager.cpp     # Character queue
modules/vivid-core/include/vivid/context.h   # characterInput()
modules/vivid-webview/src/webview.cpp        # Character forwarding
modules/vivid-webview/src/webview_macos.mm   # sendCharEvent()
modules/vivid-webview/include/vivid/webview/webview_backend.h  # sendCharEvent()
src/cli/mcp_server.cpp                       # read_file, write_file tools
```

## Verification

1. **Character input:** Type in Monaco Editor, verify characters appear
2. **Hot reload:** Edit chain.cpp in embedded editor, verify recompilation
3. **Error display:** Introduce syntax error, verify red squiggle appears
4. **Terminal:** Type commands, verify PTY echoes output
5. **Claude Code:** Run Claude Code in terminal, verify it can edit chain.cpp

## Open Questions

1. Should this be a built-in feature or a separate module/example?
2. PTY approach: fork child process vs connect to existing Claude Code session?
3. Layout: fixed panels vs resizable/dockable?
4. Should the editor replace chain visualizer or coexist?
