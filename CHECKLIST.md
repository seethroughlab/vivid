# Vivid Development Checklist

A step-by-step guide for building Vivid. Each phase builds on the previous one. Check off items as you complete them.

---

## Phase 1: Project Foundation
**Reference: [PLAN-01-overview.md](PLAN-01-overview.md)**

### 1.1 Initial Setup
- [x] Create project directory structure as shown in PLAN-01
- [x] Create root `CMakeLists.txt` with FetchContent dependencies
- [x] Create `runtime/CMakeLists.txt`
- [x] Verify CMake configures successfully and downloads dependencies
- [x] Verify wgpu-native downloads and links correctly

### 1.2 Verify Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
- [x] Build completes without errors (even if runtime does nothing yet)

---

## Phase 2: Window and WebGPU Context
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Window and Renderer sections**

### 2.1 Window Creation
- [x] Implement `runtime/src/window.h` and `window.cpp`
- [x] Create GLFW window with proper hints for WebGPU
- [x] Handle resize and close events
- [x] Test: Window opens and stays open

### 2.2 WebGPU Initialization
- [x] Implement `runtime/src/renderer.h` and `renderer.cpp`
- [x] Create WebGPU instance, surface, adapter, device, queue
- [x] Create swap chain (using wgpuSurfaceConfigure in modern wgpu API)
- [x] Implement `beginFrame()` and `endFrame()`
- [x] Test: Window clears to a solid color each frame

---

## Phase 3: Texture and Shader System
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Renderer sections**

### 3.1 Blit to Screen
- [x] Implement fullscreen triangle (no vertex buffer needed)
- [x] Load and compile the blit shader
- [x] Implement `blitToScreen()`

### 3.2 Texture Management
- [x] Implement `createTexture()`
- [x] Implement `destroyTexture()`
- [x] Implement `readTexturePixels()` (for preview capture)
- [x] Test: Create texture, render to it, blit to screen

### 3.3 Shader System
- [x] Implement `loadShader()` and `loadShaderFromFile()`
- [x] Implement uniform buffer creation and updates
- [x] Implement bind group creation for uniforms + textures
- [x] Implement `runShader()` — render to texture with uniforms
- [x] Test: Load noise shader, render to texture, display result

### 3.4 Shader Hot-Reload
- [x] Implement `reloadShader()`
- [x] Track shader file paths for reload matching
- [x] Test: Edit .wgsl file, see changes without restart (press 'R' to reload)
- [x] Shader compilation error capture and display

---

## Phase 4: Core Types and Context
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — Header files section**

### 4.1 Core Headers
- [x] Create `runtime/include/vivid/types.h`
- [x] Create `runtime/include/vivid/context.h`
- [x] Create `runtime/include/vivid/operator.h`
- [x] Create `runtime/include/vivid/node_macro.h`
- [x] Create `runtime/include/vivid/params.h`
- [x] Create `runtime/include/vivid/vivid.h` (main include)

### 4.2 Context Implementation
- [x] Implement Context class
- [x] Wire Context to Renderer for texture creation and shader execution
- [x] Implement input/output storage for operator communication
- [x] Test: Context can create textures and run shaders

---

## Phase 5: Hot-Reload System
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — HotLoader section**

### 5.1 Shared Library Loading
- [x] Implement `runtime/src/hotload.h` and `hotload.cpp`
- [x] Implement `load()` with dlopen/LoadLibrary
- [x] Implement `unload()` with dlclose/FreeLibrary
- [x] Implement symbol lookup for create/destroy functions
- [ ] Test: Manually load a test .so/.dylib/.dll

### 5.2 File Watcher
- [x] Implement `runtime/src/file_watcher.h` and `file_watcher.cpp`
- [x] Use efsw to watch project directory
- [x] Filter for .cpp, .h, .wgsl files
- [x] Callback on file changes
- [ ] Test: Edit file, see callback triggered

### 5.3 Compiler Integration
- [x] Implement `runtime/src/compiler.h` and `compiler.cpp`
- [x] Invoke CMake to build operator library
- [x] Capture stdout/stderr for error reporting
- [x] Return success/failure with library path
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

## Phase 12: Extended Media Support

### 12.0 Core Workflow Features
**Priority: High — These enable cleaner creative coding workflows**

- [ ] **Parameter References** — `"$nodeName"` syntax to reference other node outputs
- [ ] **Value Binding** — Automatically drive parameters from value-producing nodes
- [ ] **Passthrough** — Pass-through node for organization and explicit outputs
- [ ] **Reference** — Reference another operator by name
- [ ] **Switch** — Choose between multiple inputs by index
- [ ] **Math** — Combine, add, multiply, clamp, remap numeric values
- [ ] **Logic** — Comparisons, triggers, gates for control flow
- [ ] **Constant** — Generate solid colors or fixed values
- [ ] **Gradient** — Color gradient lookup/remapping
- [ ] **Resolution Independence** — Operators adapt to input resolution
- [ ] **Lazy Evaluation** — Only process nodes whose inputs changed

### 12.1 Image Loading
- [ ] Integrate stb_image for image loading
- [ ] Implement `ImageFile` operator (loads PNG, JPG, BMP, etc.)
- [ ] Support image reload on file change
- [ ] Handle various pixel formats (RGB, RGBA, grayscale)

### 12.2 Video Playback
- [ ] Integrate FFmpeg or similar for video decoding
- [ ] Implement `VideoFile` operator (MP4, MOV, WebM, etc.)
- [ ] Video playback controls (play, pause, seek, loop)
- [ ] Frame-accurate timing sync

### 12.3 Camera Input
- [ ] Integrate camera capture (platform-specific: AVFoundation/DirectShow/V4L2)
- [ ] Implement `Webcam` operator
- [ ] Camera selection (multiple devices)
- [ ] Resolution and format configuration

### 12.4 Audio Input & Analysis
- [ ] Integrate audio input (PortAudio or miniaudio)
- [ ] Implement `AudioIn` operator (microphone/line-in)
- [ ] Implement `FFT` operator (frequency spectrum)
- [ ] Implement `AudioBands` operator (bass, mid, treble, etc.)
- [ ] Beat detection

### 12.5 Audio Synthesis & Output
- [ ] Audio output support
- [ ] Basic oscillators (sine, saw, square, triangle)
- [ ] Implement `Oscillator` operator
- [ ] Implement `Envelope` operator (ADSR)
- [ ] Implement `Filter` operator (lowpass, highpass, bandpass)
- [ ] Audio file playback (WAV, MP3, OGG)

### 12.6 MIDI
- [ ] Integrate MIDI library (RtMidi or similar)
- [ ] Implement `MidiIn` operator
- [ ] Implement `MidiOut` operator
- [ ] MIDI learn functionality
- [ ] CC, note, and clock messages

### 12.7 OSC (Open Sound Control)
- [ ] Integrate OSC library (oscpack or liblo)
- [ ] Implement `OscIn` operator
- [ ] Implement `OscOut` operator
- [ ] Address pattern matching

### 12.8 3D Graphics & Instancing
**Instancing Path: 2D sprites → 3D geometry → Full instancing**

- [ ] **Point Sprites** — Render texture at positions from value arrays (2D instancing)
- [ ] **Tile/Grid** — Repeat texture in grid with per-tile transforms
- [ ] **Particle System** — Emit/update points with position, velocity, color, life
- [ ] 3D primitive generation (cube, sphere, plane, cylinder, torus)
- [ ] Implement `Geometry` operator base
- [ ] Basic 3D transforms (translate, rotate, scale)
- [ ] Camera/view matrix support
- [ ] **GPU Instancing** — Render geometry N times with instance buffer (transforms, colors)
- [ ] **Instance from Values** — Generate instance transforms from numeric arrays
- [ ] **Instance from Texture** — Use texture pixels as instance data (RGBA → transform)
- [ ] OBJ model loading
- [ ] GLTF model loading (optional)

### 12.9 Text & Vector
- [ ] Integrate font rendering (stb_truetype or FreeType)
- [ ] Implement `Text` operator
- [ ] Font selection and styling
- [ ] SVG loading and rendering (optional)

### 12.10 Recording & Output
- [ ] Image sequence export (PNG, JPG)
- [ ] Video recording (FFmpeg encoding)
- [ ] Implement `Record` operator or command
- [ ] Frame-accurate recording sync

### 12.11 Texture Sharing
- [ ] Syphon support (macOS)
- [ ] Spout support (Windows)
- [ ] NDI support (cross-platform, optional)
- [ ] Implement `SyphonOut`/`SpoutOut` operators
- [ ] Implement `SyphonIn`/`SpoutIn` operators

### 12.12 Input Devices
- [ ] Mouse position and button state in Context
- [ ] Keyboard state in Context
- [ ] Game controller support (SDL_GameController or similar)
- [ ] Implement `GamepadIn` operator

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
| Phase 12 complete | Load images/video, use audio, render 3D |
