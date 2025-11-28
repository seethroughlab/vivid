# Vivid Development Checklist

A step-by-step guide for building Vivid. Each phase builds on the previous one. Check off items as you complete them.

---

## Phase 1: Project Foundation
**Reference: [PLAN-01-overview.md](PLAN-01-overview.md)**

### 1.1 Initial Setup
- [ ] Create project directory structure as shown in PLAN-01
- [ ] Create root `CMakeLists.txt` with FetchContent dependencies
- [ ] Create `runtime/CMakeLists.txt`
- [ ] Verify CMake configures successfully and downloads dependencies
- [ ] Verify wgpu-native downloads and links correctly

### 1.2 Verify Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
- [ ] Build completes without errors (even if runtime does nothing yet)

---

## Phase 2: Window and WebGPU Context
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Window and Renderer sections**

### 2.1 Window Creation
- [ ] Implement `runtime/src/window.h` and `window.cpp`
- [ ] Create GLFW window with proper hints for WebGPU
- [ ] Handle resize and close events
- [ ] Test: Window opens and stays open

### 2.2 WebGPU Initialization
- [ ] Implement `runtime/src/renderer.h` and `renderer.cpp`
- [ ] Create WebGPU instance, surface, adapter, device, queue
- [ ] Create swap chain
- [ ] Implement `beginFrame()` and `endFrame()`
- [ ] Test: Window clears to a solid color each frame

### 2.3 Basic Rendering
- [ ] Implement fullscreen triangle (no vertex buffer needed)
- [ ] Load and compile the blit shader
- [ ] Implement `blitToScreen()`
- [ ] Test: Solid color fills the window via shader

---

## Phase 3: Texture and Shader System
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Renderer sections**

### 3.1 Texture Management
- [ ] Implement `createTexture()`
- [ ] Implement `destroyTexture()`
- [ ] Implement `readTexturePixels()` (for preview capture)
- [ ] Test: Create texture, render to it, blit to screen

### 3.2 Shader System
- [ ] Implement `loadShader()` and `loadShaderFromFile()`
- [ ] Implement uniform buffer creation and updates
- [ ] Implement bind group creation for uniforms + textures
- [ ] Implement `runShader()` — render to texture with uniforms
- [ ] Test: Load noise shader, render to texture, display result

### 3.3 Shader Hot-Reload
- [ ] Implement `reloadShader()`
- [ ] Track shader file paths for reload matching
- [ ] Test: Edit .wgsl file, see changes without restart

---

## Phase 4: Core Types and Context
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Header files section**

### 4.1 Core Headers
- [ ] Create `runtime/include/vivid/types.h`
- [ ] Create `runtime/include/vivid/context.h`
- [ ] Create `runtime/include/vivid/operator.h`
- [ ] Create `runtime/include/vivid/node_macro.h`
- [ ] Create `runtime/include/vivid/params.h`
- [ ] Create `runtime/include/vivid/vivid.h` (main include)

### 4.2 Context Implementation
- [ ] Implement Context class
- [ ] Wire Context to Renderer for texture creation and shader execution
- [ ] Implement input/output storage for operator communication
- [ ] Test: Context can create textures and run shaders

---

## Phase 5: Hot-Reload System
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — HotLoader section**

### 5.1 Shared Library Loading
- [ ] Implement `runtime/src/hotload.h` and `hotload.cpp`
- [ ] Implement `load()` with dlopen/LoadLibrary
- [ ] Implement `unload()` with dlclose/FreeLibrary
- [ ] Implement symbol lookup for create/destroy functions
- [ ] Test: Manually load a test .so/.dylib/.dll

### 5.2 File Watcher
- [ ] Implement `runtime/src/file_watcher.h` and `file_watcher.cpp`
- [ ] Use efsw to watch project directory
- [ ] Filter for .cpp, .h, .wgsl files
- [ ] Callback on file changes
- [ ] Test: Edit file, see callback triggered

### 5.3 Compiler Integration
- [ ] Implement `runtime/src/compiler.h` and `compiler.cpp`
- [ ] Invoke CMake to build operator library
- [ ] Capture stdout/stderr for error reporting
- [ ] Return success/failure with library path
- [ ] Test: Compile example operator, get .so path

### 5.4 Full Hot-Reload Loop
- [ ] On .cpp change: save state → compile → unload → load → restore state
- [ ] On .wgsl change: reload shader only
- [ ] Test: Edit operator code, see changes without restart

---

## Phase 6: Operator Graph
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — App section**
**Reference: [PLAN-03-operators.md](PLAN-03-operators.md) — Operator API**

### 6.1 Graph Implementation
- [ ] Implement `runtime/src/graph.h` and `graph.cpp`
- [ ] Store operators from NodeRegistry
- [ ] Determine execution order (simple: registration order)
- [ ] Implement `execute()` — call process() on each operator
- [ ] Implement `initAll()` and `cleanupAll()`

### 6.2 State Preservation
- [ ] Implement `saveAllStates()` — collect state from all operators
- [ ] Implement `restoreAllStates()` — restore after reload
- [ ] Test: Operator with animation state preserves phase across reload

### 6.3 Preview Capture
- [ ] Implement `capturePreviews()` — read output of each operator
- [ ] Downscale textures to thumbnail size
- [ ] Encode as base64 JPEG (use stb_image_write)
- [ ] Test: Get base64 string for operator output

---

## Phase 7: Built-in Operators
**Reference: [PLAN-03-operators.md](PLAN-03-operators.md)**

### 7.1 Operators CMake
- [ ] Create `operators/CMakeLists.txt` with `add_operator()` function
- [ ] Build operators as shared libraries

### 7.2 Core Operators
Build and test each operator individually:

- [ ] **Noise** — `operators/noise.cpp`
  - [ ] Implement with scale, speed, octaves params
  - [ ] Create `shaders/noise.wgsl`
  - [ ] Test: See animated noise

- [ ] **LFO** — `operators/lfo.cpp`
  - [ ] Implement with freq, min, max, waveform params
  - [ ] Output single value
  - [ ] Test: See oscillating value

- [ ] **Feedback** — `operators/feedback.cpp`
  - [ ] Implement double-buffer ping-pong
  - [ ] Create `shaders/feedback.wgsl`
  - [ ] Test: See trailing/echo effect

- [ ] **Composite** — `operators/composite.cpp`
  - [ ] Implement blend modes (over, add, multiply, screen, difference)
  - [ ] Create `shaders/composite.wgsl`
  - [ ] Test: Blend two textures

- [ ] **Brightness** — `operators/brightness.cpp`
  - [ ] Implement brightness and contrast
  - [ ] Accept value from another operator
  - [ ] Create `shaders/brightness.wgsl`
  - [ ] Test: Modulate brightness with LFO

- [ ] **HSVAdjust** — `operators/hsv.cpp`
  - [ ] Implement hue shift, saturation, value
  - [ ] Create `shaders/hsv.wgsl`
  - [ ] Test: Color cycling

- [ ] **Blur** — `operators/blur.cpp`
  - [ ] Implement separable Gaussian blur
  - [ ] Create `shaders/blur.wgsl`
  - [ ] Test: Soft glow effect

- [ ] **Transform** — `operators/transform.cpp`
  - [ ] Implement translate, scale, rotate
  - [ ] Create `shaders/transform.wgsl`
  - [ ] Test: Zoom and rotate texture

---

## Phase 8: Main Application Loop
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — App section**

### 8.1 App Implementation
- [ ] Implement `runtime/src/app.h` and `app.cpp`
- [ ] Create all subsystems in constructor
- [ ] Implement main loop in `run()`
- [ ] Wire up file watcher callbacks
- [ ] Handle recompile flag

### 8.2 Main Entry Point
- [ ] Implement `runtime/src/main.cpp`
- [ ] Parse command-line arguments
- [ ] Create App and run

### 8.3 Integration Test
- [ ] Create `examples/hello/chain.cpp` (from PLAN-03)
- [ ] Run vivid-runtime with example project
- [ ] Verify: Window shows animated visuals
- [ ] Verify: Edit chain.cpp, visuals update
- [ ] Verify: Edit .wgsl shader, visuals update

---

## Phase 9: Preview Server
**Reference: [PLAN-04-extension.md](PLAN-04-extension.md) — Preview Server section**

### 9.1 WebSocket Server
- [ ] Implement `runtime/src/preview_server.h` and `preview_server.cpp`
- [ ] Use IXWebSocket for server
- [ ] Implement `start()` and `stop()`
- [ ] Implement `broadcast()` helper

### 9.2 Protocol Implementation
- [ ] Implement `sendNodeUpdates()` — JSON with node previews
- [ ] Implement `sendCompileStatus()` — success/error messages
- [ ] Implement `sendError()`
- [ ] Handle incoming messages (reload, param_change)

### 9.3 Wire to App
- [ ] Start preview server in App constructor
- [ ] Call `sendNodeUpdates()` periodically (throttled)
- [ ] Call `sendCompileStatus()` after recompile
- [ ] Test with WebSocket client (wscat or browser)

---

## Phase 10: VS Code Extension
**Reference: [PLAN-04-extension.md](PLAN-04-extension.md)**

### 10.1 Extension Setup
- [ ] Create `extension/package.json`
- [ ] Create `extension/tsconfig.json`
- [ ] Run `npm install`
- [ ] Verify `npm run compile` works

### 10.2 Runtime Client
- [ ] Implement `extension/src/runtimeClient.ts`
- [ ] Connect to WebSocket server
- [ ] Parse incoming messages
- [ ] Implement reconnection logic
- [ ] Test: Connect to running runtime

### 10.3 Status Bar
- [ ] Implement `extension/src/statusBar.ts`
- [ ] Show connection status
- [ ] Show compile status
- [ ] Test: Status updates when runtime starts/stops

### 10.4 Inline Decorations
- [ ] Implement `extension/src/decorations.ts`
- [ ] Create decoration types for values and textures
- [ ] Map line numbers to nodes
- [ ] Show inline indicators (∿ for values, 🖼 for textures)
- [ ] Implement hover previews with MarkdownString
- [ ] Test: See decorations next to NODE() calls

### 10.5 Preview Panel
- [ ] Implement `extension/src/previewPanel.ts`
- [ ] Create webview with node cards
- [ ] Show texture previews, values, sparklines
- [ ] Handle click to jump to source
- [ ] Test: Panel shows all nodes with live updates

### 10.6 Extension Entry Point
- [ ] Implement `extension/src/extension.ts`
- [ ] Register all commands
- [ ] Auto-connect on project open
- [ ] Wire up all components
- [ ] Test: Full extension functionality

---

## Phase 11: Polish and Documentation

### 11.1 Error Handling
- [ ] Graceful handling of compile errors
- [ ] Show errors inline in VS Code (DiagnosticCollection)
- [ ] Handle runtime crashes without breaking extension

### 11.2 Performance
- [ ] Throttle preview updates (10-20 fps)
- [ ] Limit thumbnail size for network efficiency
- [ ] Profile and optimize hot path

### 11.3 Documentation
- [ ] Write user-facing README with screenshots
- [ ] Document operator API for custom operators
- [ ] Document shader conventions
- [ ] Create more example projects

### 11.4 Distribution
- [ ] Create build scripts for all platforms
- [ ] Package VS Code extension (.vsix)
- [ ] Create release binaries
- [ ] Write installation guide

---

## Quick Reference: Test Commands

```bash
# Build everything
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run with example
./build/bin/vivid-runtime examples/hello

# Build extension
cd extension && npm install && npm run compile

# Test extension (in VS Code, press F5 in extension folder)

# WebSocket test (while runtime is running)
npx wscat -c ws://localhost:9876
```

---

## Milestone Checkpoints

| Milestone | You should be able to... |
|-----------|--------------------------|
| Phase 2 complete | See a window with a solid color |
| Phase 3 complete | See animated noise via shader |
| Phase 5 complete | Edit .cpp, see changes without restart |
| Phase 7 complete | Chain multiple operators together |
| Phase 8 complete | Run the full hello example |
| Phase 10 complete | See inline previews in VS Code |
| Phase 11 complete | Share with others |
