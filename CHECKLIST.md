# Vivid Development Checklist

A step-by-step guide for building Vivid. Each phase builds on the previous one. Check off items as you complete them.

---

## Phase 0: Chain API (HIGHEST PRIORITY)
**The declarative operator chaining system — the core of Vivid's workflow**

The Chain API enables declarative composition of operators, replacing the current pattern where each example is a monolithic operator class. This is fundamental to Vivid's node-based visual programming paradigm.

### Target API
```cpp
// In chain.cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    chain.add<Noise>("noise").scale(4.0).speed(0.3);
    chain.add<Feedback>("fb").input("noise").decay(0.9);
    chain.add<Mirror>("mirror").input("fb").kaleidoscope(6);
    chain.add<HSV>("color").input("mirror").hueShift(0.1);
    chain.setOutput("color");
}

void update(Chain& chain, Context& ctx) {
    // Dynamic parameter changes based on time/input
    chain.get<Feedback>("fb").rotate(ctx.mouseNormX() * 0.1);
}
```

### 0.1 Chain Class Design
- [x] Create `runtime/include/vivid/chain.h`
- [x] `Chain` class to hold operator instances by name
- [x] `add<T>(name)` — instantiate operator and register by name
- [x] `get<T>(name)` — retrieve operator for parameter updates
- [x] `setOutput(name)` — designate final output operator
- [x] `connect(fromNode, toNode)` — explicit wiring (alternative to `.input()`)
- [x] Store operator instances in `std::unordered_map<std::string, std::unique_ptr<Operator>>`

### 0.2 Operator Registry
- [x] Create `runtime/include/vivid/chain.h` (OperatorRegistry in same file)
- [x] Registration macro: `VIVID_REGISTER_OPERATOR(ClassName)`
- [x] Factory function: `createOperator(typeName)` → `std::unique_ptr<Operator>`
- [x] Auto-registration at static init time (or explicit registry population)
- [x] All built-in operators registered (`Noise`, `Feedback`, `Mirror`, `HSV`, `Blur`, etc.)

### 0.3 Dependency Resolution
- [x] Track input dependencies from `.input("nodeName")` calls on operators
- [x] Build dependency graph from input references
- [x] Topological sort for execution order (`computeExecutionOrder()`)
- [x] Detect and report circular dependencies
- [x] Handle missing input references gracefully (warning, not crash)

### 0.4 Chain Execution
- [x] `Chain::init(Context&)` — call `init()` on all operators in dependency order
- [x] `Chain::process(Context&)` — call `process()` on all operators in dependency order
- [x] `Chain::cleanup()` — call `cleanup()` on all operators
- [x] Output routing: each operator's `setOutput()` stores in Context for next operator's `getInputTexture()`
- [x] Final output: `Chain::getOutput()` returns the designated output texture

### 0.5 Entry Point Detection
- [x] Modify `HotLoader` to look for `setup` and `update` symbols
- [x] Signature: `void setup(Chain& chain)`
- [x] Signature: `void update(Chain& chain, Context& ctx)`
- [x] Fallback: if no `setup` found, use legacy single-operator pattern
- [x] Runtime creates Chain, calls `setup()` once, calls `update()` each frame

### 0.6 Hot-Reload Integration
- [x] On reload: save operator states via `saveState()`
- [x] Recreate Chain from new library
- [x] Call `setup()` to rebuild operator graph
- [x] Restore states to matching operator names via `loadState()`
- [x] Handle added/removed operators gracefully

### 0.7 Built-in Operator Updates
- [x] Ensure all operators have proper `input()` method returning `*this`
- [x] Standardize fluent API across all operators
- [x] Add `VIVID_REGISTER_OPERATOR` to all built-in operators
- [x] Verify each operator works in Chain context

### 0.8 Example Migration
- [x] Migrate `examples/hello` to Chain API
- [x] Migrate `examples/feedback` to Chain API
- [x] Migrate `examples/webcam` to Chain API
- [x] Migrate `examples/video-playback` to Chain API
- [x] Create new `examples/chain-demo` showcasing the API
- [x] Update documentation with Chain API examples

### 0.9 Documentation
- [x] Document Chain API in `docs/CHAIN-API.md`
- [x] Update `docs/OPERATOR-API.md` with fluent interface requirements
- [x] Update README with Chain API examples
- [x] Add inline comments to `chain.h`

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
- [x] Test: Manually load a test .so/.dylib/.dll

### 5.2 File Watcher
- [x] Implement `runtime/src/file_watcher.h` and `file_watcher.cpp`
- [x] Use efsw to watch project directory
- [x] Filter for .cpp, .h, .wgsl files
- [x] Callback on file changes
- [x] Test: Edit file, see callback triggered

### 5.3 Compiler Integration
- [x] Implement `runtime/src/compiler.h` and `compiler.cpp`
- [x] Invoke CMake to build operator library
- [x] Capture stdout/stderr for error reporting
- [x] Return success/failure with library path
- [x] Test: Compile example operator, get .so path

### 5.4 Full Hot-Reload Loop
- [x] On .cpp change: save state → compile → unload → load → restore state
- [x] On .wgsl change: reload shader only
- [x] Test: Edit operator code, see changes without restart

---

## Phase 6: Operator Graph
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — App section**
**Reference: [PLAN-03-operators.md](PLAN-03-operators.md) — Operator API**

### 6.1 Graph Implementation
- [x] Implement `runtime/src/graph.h` and `graph.cpp`
- [x] Store operators from HotLoader
- [x] Determine execution order (simple: registration order)
- [x] Implement `execute()` — call process() on each operator
- [x] Implement `initAll()` and `cleanupAll()`
- [x] Implement `finalOutput()` — get last operator's output texture
- [x] Refactor main.cpp to use Graph class

### 6.2 State Preservation
- [x] Implement `saveAllStates()` — collect state from all operators
- [x] Implement `restoreAllStates()` — restore after reload
- [x] Test: Hot-reload preserves Graph state

### 6.3 Preview Capture
- [x] Implement `capturePreviews()` — read output of each operator
- [x] Encode as base64 JPEG (use stb_image_write)
- [x] Support both Texture and Value output kinds
- [x] Include operatorId, sourceLine, dimensions in Preview struct

---

## Phase 7: Built-in Operators
**Reference: [PLAN-03-operators.md](PLAN-03-operators.md)**

### 7.1 Operators CMake
- [x] Create `operators/CMakeLists.txt` with `add_operator()` function
- [x] Enhanced `Uniforms` struct with operator parameters (param0-7, vec0-1, mode)
- [x] Enhanced `Context::runShader()` to support `ShaderParams`
- [x] Two-texture support in `Context::runShader()` and `Renderer::runShader()` for compositing

### 7.2 Core Operators
Build and test each operator individually:

- [x] **Noise** — `operators/noise.cpp`
  - [x] Implement with scale, speed, octaves, lacunarity, persistence params
  - [x] Create `shaders/noise.wgsl` with simplex noise + FBM
  - [x] State preservation for animation phase
  - [x] Test: Animated noise with configurable parameters
  - [ ] Add noise type parameter (mode: 0=simplex, 1=perlin, 2=worley)
  - [ ] Implement classic Perlin noise in shader
  - [ ] Implement Worley (cellular) noise in shader

- [x] **LFO** — `operators/lfo.cpp`
  - [x] Implement with freq, min, max, phase, waveform params
  - [x] Four waveforms: sine, saw, square, triangle
  - [x] Output single value + history array

- [x] **Feedback** — `operators/feedback.cpp`
  - [x] Implement double-buffer ping-pong
  - [x] Create `shaders/feedback.wgsl` with transform
  - [x] Decay, zoom, rotate, translate params

- [x] **Composite** — `operators/composite.cpp`
  - [x] Implement blend modes (over, add, multiply, screen, difference)
  - [x] Create `shaders/composite.wgsl`
  - [x] Mix parameter for blending strength
  - [x] Two-texture support (proper alpha-over compositing with separate background/foreground)

- [x] **Brightness** — `operators/brightness.cpp`
  - [x] Implement brightness and contrast
  - [x] Accept value from another operator (node reference)
  - [x] Create `shaders/brightness.wgsl`

- [x] **HSVAdjust** — `operators/hsv.cpp`
  - [x] Implement hue shift, saturation, value
  - [x] Create `shaders/hsv.wgsl` with RGB<->HSV conversion

- [x] **Blur** — `operators/blur.cpp`
  - [x] Implement separable Gaussian blur
  - [x] Create `shaders/blur.wgsl`
  - [x] Multi-pass support for stronger blur

- [x] **Transform** — `operators/transform.cpp`
  - [x] Implement translate, scale, rotate, pivot
  - [x] Create `shaders/transform.wgsl`
  - [x] Inverse transform for correct texture sampling

- [x] **Gradient** — `operators/gradient.cpp`
  - [x] Generate linear, radial, angular, diamond gradients
  - [x] Create `shaders/gradient.wgsl`
  - [x] Configurable colors, angle, offset, scale
  - [x] Animated mode (mode 4) with HSV color cycling

- [x] **Displacement** — `operators/displacement.cpp`
  - [x] Distort texture using displacement map
  - [x] Create `shaders/displacement.wgsl`
  - [x] Multiple channel modes (luminance, R, G, RG)
  - [x] Two-texture support (source + any displacement map from Noise, Gradient, etc.)

- [x] **Edge** — `operators/edge.cpp`
  - [x] Sobel edge detection
  - [x] Create `shaders/edge.wgsl`
  - [x] Multiple output modes (edges only, overlay, inverted)

- [x] **Shape** — `operators/shape.cpp`
  - [x] SDF-based shape rendering (Circle, Rectangle, Triangle, Line, Ring, Star)
  - [x] Create `shaders/shape.wgsl` with SDF functions
  - [x] Fill, stroke, softness/antialiasing, rotation support

- [x] **ChromaticAberration** — `operators/chromatic_aberration.cpp`
  - [x] RGB channel separation for VHS/glitch aesthetic
  - [x] Create `shaders/chromatic_aberration.wgsl`
  - [x] Three modes: directional, radial, barrel distortion

- [x] **Pixelate** — `operators/pixelate.cpp`
  - [x] Reduce effective resolution for blocky mosaic effect
  - [x] Create `shaders/pixelate.wgsl`
  - [x] Square and aspect-corrected block modes

- [x] **Scanlines** — `operators/scanlines.cpp`
  - [x] CRT-style horizontal lines for retro monitor effect
  - [x] Create `shaders/scanlines.wgsl`
  - [x] Three modes: simple, alternating, RGB sub-pixel simulation

---

## Phase 8: Main Application Loop
**Reference: [PLAN-02-runtime.md](PLAN-02-runtime.md) — App section**

### 8.1 App Implementation
- [x] Main loop implemented in `runtime/src/main.cpp`
- [x] All subsystems created (Window, Renderer, Context, HotLoader, FileWatcher, Compiler, Graph)
- [x] File watcher callbacks wired up
- [x] Hot-reload with state preservation
- [x] _(Optional: Refactor into separate App class for cleaner architecture)_

### 8.2 Main Entry Point
- [x] Implement `runtime/src/main.cpp`
- [x] Parse command-line arguments (--width, --height, --fullscreen)
- [x] Project path from command line

### 8.3 Integration Test
- [x] `examples/hello/chain.cpp` with configurable Noise operator
- [x] Run vivid-runtime with example project
- [x] Verify: Window shows animated visuals
- [x] Verify: Edit chain.cpp, visuals update (hot-reload works)
- [x] Verify: Edit .wgsl shader, visuals update

---

## Phase 9: Preview Server
**Reference: [PLAN-04-extension.md](PLAN-04-extension.md) — Preview Server section**

### 9.1 WebSocket Server
- [x] Implement `runtime/src/preview_server.h` and `preview_server.cpp`
- [x] Use IXWebSocket for server
- [x] Implement `start()` and `stop()`
- [x] Implement `broadcast()` helper

### 9.2 Protocol Implementation
- [x] Implement `sendNodeUpdates()` — JSON with node previews
- [x] Implement `sendCompileStatus()` — success/error messages
- [x] Implement `sendError()`
- [x] Handle incoming messages (reload, param_change)

### 9.3 Wire to App
- [x] Start preview server in main.cpp
- [x] Call `sendNodeUpdates()` periodically (throttled at 10fps)
- [x] Call `sendCompileStatus()` after recompile
- [x] Test: Server starts and listens on port 9876

---

## Phase 10: VS Code Extension
**Reference: [PLAN-04-extension.md](PLAN-04-extension.md)**

### 10.1 Extension Setup
- [x] Create `extension/package.json`
- [x] Create `extension/tsconfig.json`
- [x] Run `npm install`
- [x] Verify `npm run compile` works

### 10.2 Runtime Client
- [x] Implement `extension/src/runtimeClient.ts`
- [x] Connect to WebSocket server
- [x] Parse incoming messages
- [x] Implement reconnection logic
- [x] Test: Connect to running runtime

### 10.3 Status Bar
- [x] Implement `extension/src/statusBar.ts`
- [x] Show connection status
- [x] Show compile status
- [x] Test: Status updates when runtime starts/stops

### 10.4 Inline Decorations
- [x] Implement `extension/src/decorations.ts`
- [x] Create decoration types for values and textures
- [x] Map line numbers to nodes
- [x] Show inline indicators (~ for values, [img] for textures)
- [x] Implement hover previews with MarkdownString
- [x] Test: See decorations next to NODE() calls

### 10.5 Preview Panel
- [x] Implement `extension/src/previewPanel.ts`
- [x] Create webview with node cards
- [x] Show texture previews, values, sparklines
- [x] Handle click to jump to source
- [x] Test: Panel shows all nodes with live updates

### 10.6 Extension Entry Point
- [x] Implement `extension/src/extension.ts`
- [x] Register all commands
- [x] Auto-connect on project open
- [x] Wire up all components
- [x] Test: Full extension functionality

---

## Phase 11: Polish and Documentation

### 11.1 Error Handling
- [x] Graceful handling of compile errors
- [x] Show errors inline in VS Code (DiagnosticCollection)
- [x] Handle runtime crashes without breaking extension

### 11.2 Performance — Async Shared Memory Previews
**Reference: [PLAN-04-extension.md](PLAN-04-extension.md) — Async Shared Memory Preview Architecture**

#### Phase 1: Async GPU Readback
- [x] Implement `runtime/src/async_readback.h` and `.cpp`
- [x] Use `wgpuBufferMapAsync` instead of blocking poll
- [x] Ring buffer of staging buffers (avoid per-frame allocation)
- [x] Callback-based completion notification
- [x] Test: Readback doesn't block main render thread

#### Phase 2: Preview Thread
- [x] Implement `runtime/src/preview_thread.h` and `.cpp`
- [x] Separate thread for readback completion polling
- [x] Queue-based communication with main thread
- [x] Thumbnail downsampling on preview thread (not render thread)
- [x] Test: Render loop stays at 60fps during preview capture

#### Phase 3: Shared Memory Segment
- [x] Implement `runtime/src/shared_preview.h` and `.cpp`
- [x] POSIX `shm_open`/`mmap` for macOS/Linux
- [x] Windows `CreateFileMapping`/`MapViewOfFile` implementation
- [x] Fixed layout: header + 64 operator slots
- [x] 128x128 RGB thumbnails per slot
- [x] Test: Runtime creates shared memory, can read from another process

#### Phase 4: Extension Native Module
- [x] Create `extension/native/` with node-gyp build
- [x] Implement `shared_preview_native.node` binding
- [x] Read shared memory from Node.js/VS Code
- [x] Convert raw RGB to displayable format (PPM data URL)
- [x] Test: Extension reads preview data from shared memory

#### Phase 5: Integration
- [x] WebSocket sends only metadata (operator list, slot indices, frame number)
- [x] Extension reads image data from shared memory on notification
- [x] Fallback to WebSocket base64 if shared memory unavailable
- [x] Remove blocking preview capture from main loop
- [x] Test: Full integration - runtime + extension shared memory

### 11.3 Documentation
- [x] Write user-facing README with accurate examples
- [x] Document operator API for custom operators (docs/OPERATOR-API.md)
- [x] Document shader conventions (docs/SHADER-CONVENTIONS.md)
- [x] Create more example projects (feedback, gradient-blend, lfo-modulation, shapes)
- [x] Configure Doxygen for API documentation (Doxyfile created, run `doxygen` to generate)
- [x] Add doc comments to `runtime/include/vivid/*.h`
- [x] Auto-generate operator reference from code (scripts/generate-operator-docs.js → docs/OPERATORS.md)

### 11.4 Distribution
- [x] Create build scripts for all platforms (scripts/build-*.sh, scripts/build-windows.bat)
- [x] Package VS Code extension (.vsix) - run `cd extension && npm run package`
- [x] Create release binaries (scripts/package-release.sh)
- [x] Write installation guide (docs/INSTALL.md)

---

## Phase 12: Extended Media Support

### 12.0 Core Workflow Features
**Priority: High — These enable cleaner creative coding workflows**

- [x] **Constant** — Generate solid colors or fixed values (`operators/constant.cpp`)
- [x] **Math** — Combine, add, multiply, clamp, remap numeric values (`operators/math.cpp`)
- [x] **Logic** — Comparisons, triggers, gates for control flow (`operators/logic.cpp`)
- [x] **Switch** — Choose between multiple inputs by index (`operators/switch.cpp`)
- [x] **Passthrough** — Pass-through node for organization and explicit outputs (`operators/passthrough.cpp`)
- [x] **Resolution Independence** — `ctx.createTextureMatching()` adapts to input resolution
- [x] **Gradient** — Already exists (`operators/gradient.cpp`)
- [x] **Parameter References** — `ParamRef<T>` template with `"$nodeName"` syntax (`param_ref.h`)
- [x] **Value Binding** — `ParamRef<T>::get(ctx)` resolves references at runtime
- [x] **Reference** — Reference another operator by name (`operators/reference.cpp`)
- [x] **Lazy Evaluation** — `needsUpdate()` virtual method, Graph checks before process()

### 12.1 Image Loading
- [x] Integrate stb_image for image loading (`runtime/src/image_loader.cpp`)
- [x] Implement `ImageFile` operator (`operators/imagefile.cpp`)
- [x] Support image reload on file change (via mtime checking)
- [x] Handle various pixel formats (converted to RGBA8 on load)

### 12.2 Video Playback
**Reference: [PLAN-05-media.md](PLAN-05-media.md)**
**Architecture: Platform-native (AVFoundation/Media Foundation) + HAP support**

#### 12.2a Core Infrastructure
- [x] Create `VideoLoader` interface (`runtime/src/video_loader.h`)
- [x] Add Snappy dependency to CMakeLists.txt (for HAP)
- [x] Add platform framework links (AVFoundation, Media Foundation)
- [x] Factory method for platform-appropriate loader (`VideoLoader::create()`)

#### 12.2b macOS Implementation (Primary)
- [x] Implement `VideoLoaderMacOS` (`runtime/src/video_loader_macos.mm`)
- [x] AVAssetReader setup and frame extraction
- [x] CVPixelBuffer → Texture upload (BGRA to RGBA conversion)
- [x] Hardware-accelerated decode (automatic via VideoToolbox)
- [x] Frame-accurate seeking (via reader recreation with time range)
- [x] Frame rate limiting (decode only at video's native framerate)
- [x] Test: H.264 playback (avc1 codec tested successfully)
- [ ] Test: ProRes, HEVC playback
- [ ] Zero-copy via IOSurface (optimization for later)

#### 12.2c HAP Codec Support (All Platforms)
- [x] Add FFmpeg for container demuxing and HAP decoding
- [x] Implement `HAPDecoder` (`runtime/src/hap_decoder.cpp`)
- [x] FFmpeg's built-in HAP decoder with swscale conversion
- [x] Auto-detection of HAP codec in video files
- [x] Test: HAP playback (Hap1 format tested at 1920x1080@60fps)

#### 12.2d VideoFile Operator
- [x] Implement `VideoFile` operator (`operators/videofile.cpp`)
- [x] Fluent API: `.path()`, `.loop()`, `.speed()`, `.play()`, `.pause()`, `.seek()`
- [x] Playback controls (play, pause, seek, loop, variable speed)
- [x] File change detection for hot-reload
- [x] Output: texture + duration/position/fps values

#### 12.2e Windows Implementation
- [ ] Implement `VideoLoaderWindows` (`runtime/src/video_loader_windows.cpp`)
- [ ] Media Foundation Source Reader integration
- [ ] Hardware decode via MFTs
- [ ] D3D11 texture → WebGPU path

#### 12.2f Linux Implementation
- [ ] Implement `VideoLoaderLinux` (`runtime/src/video_loader_linux.cpp`)
- [ ] Full FFmpeg decode path (libavformat + libavcodec)
- [ ] Optional VAAPI/VDPAU hardware acceleration

### 12.3 Camera Input
**Architecture: Platform-native (shares infrastructure with 12.2)**

- [x] Create `CameraCapture` interface (`runtime/src/camera_capture.h`)
- [x] macOS: AVFoundation `AVCaptureSession` (`runtime/src/camera_capture_macos.mm`)
- [ ] Windows: Media Foundation or DirectShow (`runtime/src/camera_capture_windows.cpp`)
- [ ] Linux: V4L2 (`runtime/src/camera_capture_linux.cpp`)
- [x] Implement `Webcam` operator (`operators/webcam.cpp`)
- [x] Camera enumeration (list available devices)
- [x] Camera selection by index or name
- [x] Resolution and format configuration
- [ ] Hot-swap camera detection

### 12.4 Audio Playback (Video)
- [x] Integrate miniaudio for audio playback
- [x] AudioPlayer class with ring buffer
- [x] Video audio track decoding (AVFoundation)
- [x] A/V sync support (audio plays alongside video)

### 12.5 Audio Input & Analysis
- [x] Implement `AudioIn` operator (microphone/line-in via miniaudio)
- [x] Implement `FFT` operator (frequency spectrum) — integrated into AudioIn
- [x] Implement `AudioBands` operator (bass, mid, treble, etc.) — integrated into AudioIn
- [ ] Beat detection (implement per `beat_detection.md` spec)

### 12.6 Audio Synthesis & Output
- [ ] Basic oscillators (sine, saw, square, triangle)
- [ ] Implement `Oscillator` operator
- [ ] Implement `Envelope` operator (ADSR)
- [ ] Implement `Filter` operator (lowpass, highpass, bandpass)
- [ ] Audio file playback (WAV, MP3, OGG)

### 12.7 MIDI
- [ ] Integrate MIDI library (RtMidi or similar)
- [ ] Implement `MidiIn` operator
- [ ] Implement `MidiOut` operator
- [ ] MIDI learn functionality
- [ ] CC, note, and clock messages

### 12.8 OSC (Open Sound Control)
- [ ] Integrate OSC library (oscpack or liblo)
- [ ] Implement `OscIn` operator
- [ ] Implement `OscOut` operator
- [ ] Address pattern matching

### 12.9 3D Graphics & Instancing
**Instancing Path: 2D sprites → 3D geometry → Full instancing**

#### Geometry & Instancing
- [x] **Point Sprites** — Render circles at positions with patterns (grid, random, circle, spiral) — `operators/pointsprites.cpp`
- [x] **Tile/Grid** — Repeat texture in grid with per-tile transforms — `operators/tile.cpp`, `shaders/tile.wgsl`
- [x] **Particle System** — Emit/update particles with position, velocity, color, life — `operators/particles.cpp`
- [x] 3D primitive generation (cube, sphere, plane, cylinder, torus, cone) — `runtime/src/mesh.cpp`
- [x] Implement `Geometry` operator base with Vertex3D format (position, normal, UV, tangent) — `runtime/src/mesh.h`
- [ ] Basic 3D transforms (translate, rotate, scale)
- [x] **GPU Instancing** — Render geometry N times with instance buffer (transforms, colors) — `drawMeshInstanced()`
- [ ] **Instance from Values** — Generate instance transforms from numeric arrays
- [ ] **Instance from Texture** — Use texture pixels as instance data (RGBA → transform)
- [x] 3D model loading via Assimp (`runtime/src/model_loader.cpp`, `ctx.loadMesh()`)
  - [x] FBX format support (tested with Wolf model: 8K vertices, 11K triangles)
  - [x] OBJ format support
  - [x] glTF/GLB format support
  - [x] COLLADA (.dae), 3DS, Blender, and 40+ other formats
- [x] `examples/model-loader` — Demonstrates FBX/OBJ/glTF loading with camera orbit
- [ ] **Skeletal Animation** — Full bone-based animation system (WIP - deformation issues)
  - [x] Skeleton extraction from FBX/glTF (bone hierarchy, inverse bind matrices) — `model_loader.cpp`
  - [x] Bone weights and IDs per vertex (max 4 bones per vertex) — `SkinnedVertex3D`
  - [x] Animation keyframe extraction (position, rotation, scale) — `AnimationClip`
  - [x] Keyframe interpolation with quaternion slerp — `animation.cpp`
  - [x] GPU skinning shader (WGSL) — `pipeline3d_skinned.cpp`
  - [x] `ctx.loadSkinnedMesh()`, `ctx.renderSkinned3D()` — `context.cpp`
  - [x] Animation playback controls (play, pause, loop, switch animations) — `AnimationPlayer`
  - [ ] Fix bone matrix computation (model deforms incorrectly during animation)
  - [x] ~~ozz-animation integration~~ (removed - not working correctly)

#### Camera System
- [x] `Camera3D` class with position, target, FOV, near/far planes — `runtime/src/camera3d.h`
- [x] View matrix generation (lookAt) — `Camera3D::viewMatrix()`
- [x] Projection matrix (perspective) — `Camera3D::projectionMatrix()`
- [x] Camera uniform buffer for shaders — `CameraUniform` struct
- [ ] **Orbit Camera** — Mouse-controlled orbit around target (drag to rotate, scroll to zoom)
- [ ] **Fly Camera** — WASD + mouse look for free navigation

#### Lighting System
- [x] Light base class with color and intensity — `Light` struct in `graphics3d.h`
- [x] `DirectionalLight` — Sun/moon style parallel rays — `Light::directional()`
- [x] `PointLight` — Omnidirectional with radius/attenuation — `Light::point()`
- [x] `SpotLight` — Cone with inner/outer angle falloff — `Light::spot()`
- [x] Light uniform buffer (support up to 8 lights per pass) — `LightsUniform` in `pipeline3d_lit.h`
- [x] `SceneLighting` class with ambient + light array — `graphics3d.h`

#### Shading Models
- [x] Depth buffer support in Renderer — `Renderer::createDepthBuffer()`
- [x] 3D render pipeline (vertex + fragment with depth test) — `runtime/src/pipeline3d.cpp`
- [x] **Phong/Blinn-Phong** shader — `pipeline3d_lit.cpp` (embedded WGSL)
  - [x] Ambient, diffuse, specular components
  - [x] Shininess parameter
  - [x] Multiple light support
- [x] **PBR** shader — `pipeline3d_lit.cpp` (embedded WGSL), Cook-Torrance BRDF
  - [x] Metallic-roughness workflow
  - [x] GGX normal distribution
  - [x] Smith geometry function
  - [x] Fresnel-Schlick approximation
- [x] Normal mapping (tangent space transforms) — `TexturedPBRMaterial.normalMap`, `normalStrength`
- [x] PBR texture maps (albedo, metallic/roughness, AO, emissive) — `TexturedPBRMaterial` struct

#### Image-Based Lighting (IBL)
- [x] `Environment` struct (irradianceMap, radianceMap, brdfLUT) — `graphics3d.h`
- [x] `Cubemap` struct — `graphics3d.h`
- [x] `loadEnvironment()` / `destroyEnvironment()` API — `context.h/cpp`
- [x] `render3DPBR()` overload with Environment parameter — `context.h/cpp`
- [x] `CubemapProcessor` class — `runtime/src/cubemap.h/cpp`
  - [x] WGSL compute shaders for IBL processing (equirect, irradiance, radiance, BRDF)
  - [x] GPU execution of compute shaders
- [x] Update PBR shader with IBL sampling (`PBR_IBL` shader variant) — `shaders3d::PBR_IBL`
- [x] Environment bind group in `Pipeline3DLit` — `renderPBRWithIBL()`, `renderPBRTexturedWithIBL()`
- [x] `examples/lighting-demo` with HDR environment — `environment.hdr`

#### Material Operators
- [x] `PhongMat` — ambient, diffuse, specular, shininess, emissive (`operators/phong_material.cpp`)
  - [x] Fluent API with presets: matte(), shiny(), metallic()
  - [x] getMaterial() accessor for use with ctx.render3DPhong()
- [x] `PBRMat` — albedo, metallic, roughness, AO, emissive (`operators/pbr_material.cpp`)
  - [x] Fluent API with material presets
  - [x] Metal presets: gold(), silver(), copper(), iron(), aluminum(), chrome()
  - [x] Dielectric presets: plastic(), rubber(), glass(), ceramic(), wood(), fabric(), skin()
  - [x] getMaterial() accessor for use with ctx.render3DPBR()

#### Render Operator
- [x] `Render3D` — Combines geometry + material + lights + camera → texture — `operators/render3d.cpp`

#### Stencil & Decals
- [x] **Stencil Buffer** — Enable stencil testing in render pipeline — `pipeline3d_lit.cpp`
- [x] **Stencil Operators** — Write/test stencil for masking effects — `pipeline3d_lit.h:StencilMode`
- [x] **Decal Projection** — Project textures onto 3D geometry (team logos, grime, weathering) — `pipeline3d_decal.cpp`
- [x] **Decal Blending** — Multiply, overlay, additive modes for decals — `pipeline3d_decal.cpp:createBlendPipeline()`

#### Advanced (Future)
- [ ] Shadow mapping (directional + point light shadows)
- [ ] Screen-space reflections
- [ ] Ambient occlusion (SSAO)

### 12.9b Post-Processing Effects
**Priority: High — Essential for polished visuals**

#### Bloom & Glow
- [x] **Bloom** — HDR-style bright area glow (`operators/bloom.h`)
  - [x] Brightness threshold extraction
  - [x] Multi-pass Gaussian blur on bright areas
  - [x] Additive blend back to source
- [x] **Glow** — Per-object glow via alpha channel or mask (`shaders/glow.wgsl`)

#### Color & Tone
- [x] **Vignette** — Darkened edges effect (`operators/vignette.h`)
- [x] **ColorGrade** — Math-based color grading with presets (`shaders/color_grade.wgsl`)
  - [x] Saturation, contrast, temperature, tint controls
  - [x] Shadows/midtones/highlights color offsets
  - [x] Presets: Vintage film, Teal & Orange, Bleach bypass
- [x] **Tonemap** — HDR to SDR mapping (`shaders/tonemap.wgsl`)
  - [x] ACES Filmic tone mapping
  - [x] Reinhard tone mapping
  - [x] Uncharted 2 (John Hable's formula)
  - [x] Exposure and gamma controls

#### Distortion
- [x] **LensDistortion** — Barrel/pincushion distortion (`shaders/lens_distortion.wgsl`)
  - [x] Brown-Conrady model (camera calibration style)
  - [x] Simple polynomial model
  - [x] Fisheye projection mode
- [x] **LensFlare** — Anamorphic lens flare from bright spots (`shaders/lens_flare.wgsl`)
  - [x] Horizontal anamorphic streaks
  - [x] Circular ghost artifacts
  - [x] Full cinematic mode combining both
- [x] **FilmGrain** — Animated noise overlay for cinematic look (`shaders/film_grain.wgsl`)
  - [x] Configurable intensity, size, speed
  - [x] Luminance-responsive grain (stronger in midtones)

#### Motion
- [x] **MotionBlur** — Direction/camera motion blur (`shaders/motion_blur.wgsl`)
  - [x] Directional (linear) blur
  - [x] Camera shake blur
  - [x] Simulated object motion (radial from center)
- [x] **RadialBlur** — Speed/zoom blur effect (`shaders/radial_blur.wgsl`)
  - [x] Zoom blur (outward from center)
  - [x] Spin blur (rotational)

### 12.10 Text, Debug Display & Vector

#### Debug Display
- [x] FPS counter in window title (updates every 0.5s)
- [x] On-screen FPS overlay via stb_truetype (F1 to toggle)
- [x] Debug info overlay (resolution, frame time, operator count)

#### Text Rendering
- [x] Integrate stb_truetype for font rendering
- [x] Font atlas generation from TTF files (FontAtlas class)
- [x] TextRenderer class for GPU text rendering
- [x] Implement Text operator with text string input
- [x] Font selection and styling (size, color, alignment)
- [x] Multi-line text support

#### Vector Graphics
- [ ] SVG loading and rendering (optional)

### 12.11 Recording & Output
**Note: FFmpeg used for encoding (separate from decode path)**

- [ ] Image sequence export (PNG, JPG via stb_image_write)
- [ ] Video encoding via FFmpeg (H.264, H.265, ProRes)
- [ ] HAP encoding support (for VJ interchange)
- [ ] Implement `Record` operator or global record command
- [ ] Frame-accurate offline rendering (fixed timestep)
- [ ] Audio mixdown to video (when audio support added)

### 12.12 Texture Sharing

#### Addon Architecture
**Pre-built static libraries with auto-detection for fast hot-reload**

Each addon compiles once into a static library during Vivid's build. User projects automatically link against pre-built libraries when addon headers are detected in source files.

- [ ] **Addon Build System** — Addons compile as static libraries during Vivid build
  - [ ] Create `addons/CMakeLists.txt` to build all addons
  - [ ] Each addon produces `lib/vivid-{name}.lib` (Windows) / `libvivid-{name}.a` (Unix)
  - [ ] Addon metadata in `addon.json` (name, platforms, detect_headers, libraries, dependencies)

- [ ] **Auto-Detection** — Compiler scans source for addon includes
  - [ ] Scan `chain.cpp` for `#include <vivid/{addon}/...>` patterns
  - [ ] Match includes against `addon.json` `detect_headers` field
  - [ ] Auto-generate CMakeLists with required addon linkage
  - [ ] No custom CMakeLists.txt required for addon usage

- [ ] **Addon JSON Schema**
  ```json
  {
    "name": "spout",
    "platforms": ["windows"],
    "detect_headers": ["vivid/spout/spout.h"],
    "include_dirs": ["include"],
    "libraries": ["lib/vivid-spout.lib"],
    "dependencies": ["opengl32"]
  }
  ```

#### Spout (Windows)
- [x] Create `addons/vivid-spout/` directory structure
- [x] Implement `spout.h` public API (Sender, Receiver, SenderInfo)
- [x] Implement `spout_sender.cpp` using Spout2 SDK
- [x] Implement `spout_receiver.cpp` using Spout2 SDK
- [x] Create `addon.cmake` build integration (builds Spout2 from source)
- [x] Create `examples/spout-out` — send textures to other apps
- [x] Create `examples/spout-in` — receive textures from other apps
- [x] Test sender functionality (verified working)
- [x] Test receiver functionality (verified working)
- [ ] Migrate to pre-built static library architecture

#### Syphon (macOS)
- [ ] Create `addons/vivid-syphon/` directory structure
- [ ] Implement `syphon.h` public API (Sender, Receiver, ServerInfo)
- [ ] Implement `syphon_sender.mm` using Syphon framework
- [ ] Implement `syphon_receiver.mm` using Syphon framework
- [ ] Create `addon.json` metadata
- [ ] Create `examples/syphon-out` — send textures to other apps
- [ ] Create `examples/syphon-in` — receive textures from other apps

#### NDI (Cross-platform, optional)
- [ ] NDI support (cross-platform, optional)
- [ ] Implement `NdiOut`/`NdiIn` operators

### 12.13 Input Devices
- [ ] Mouse position and button state in Context
- [x] Keyboard state in Context (`isKeyDown`, `wasKeyPressed`, `wasKeyReleased`)
- [x] Key constants in `vivid/keys.h` (matching GLFW codes)
- [ ] Mouse event callbacks (click, drag, scroll)
- [ ] Game controller support (SDL_GameController or similar)
- [ ] Implement `GamepadIn` operator

### 12.14 Advanced Window Management
**Priority: High — Essential for performance/installation work**

> **Note:** GLFW provides cross-platform windowing. Most features work on all platforms
> with the same code, but need testing. Platform tags indicate tested/verified status.

**Cross-platform API (GLFW-based):**
- [ ] Implement `WindowSettings` struct
- [x] `ctx.setFullscreen(bool)` / `ctx.toggleFullscreen()`
- [x] `ctx.setBorderless(bool)` - borderless window mode
- [x] `ctx.setWindowSize(w, h)` / `ctx.setWindowPosition(x, y)`
- [x] `ctx.setAlwaysOnTop(bool)`
- [x] `ctx.setCursorVisible(bool)`
- [x] `ctx.printMonitors()` - list available displays
- [x] `ctx.moveToMonitor(index)` - move window to specific monitor
- [ ] Command-line args: `--fullscreen`, `--width`, `--height`, `--monitor`, `--borderless`

**Tested/Verified:**
- [x] Fullscreen toggle (macOS)
- [ ] Fullscreen toggle (Windows)
- [ ] Fullscreen toggle (Linux)
- [x] Borderless mode (macOS)
- [ ] Borderless mode (Windows)
- [ ] Borderless mode (Linux)
- [x] Multi-monitor (macOS)
- [ ] Multi-monitor (Windows)
- [ ] Multi-monitor (Linux)
- [x] Always-on-top (macOS)
- [ ] Always-on-top (Windows)
- [ ] Always-on-top (Linux)

**Platform-specific (if needed):**
- [ ] macOS: Native fullscreen vs GLFW fullscreen behavior
- [ ] Windows: Exclusive fullscreen for lowest latency
- [ ] Linux: X11 vs Wayland considerations

### 12.15 Native GUI / WebView
- [ ] Integrate native WebView (WebView2 on Windows, WKWebView on macOS)
- [ ] Optional control panel window for parameter tweaking
- [ ] HTML/CSS/JS-based UI for custom operator interfaces
- [ ] Communication bridge between WebView and runtime

### 12.16 VST/AU Plugin Support (Future)
**Priority: Low — Nice to have for advanced audio work**

- [ ] Research plugin hosting framework (JUCE vs VST3 SDK)
- [ ] Implement `VSTHost` operator
- [ ] Plugin scanning and preset management
- [ ] Parameter automation from Vivid operators

### 12.17 Machine Learning / ONNX Runtime
**Priority: Medium — Enables pose detection, style transfer, segmentation**

#### Core Infrastructure
- [ ] Integrate ONNX Runtime (cross-platform ML inference)
- [ ] Create `runtime/src/ml_inference.h` interface
- [ ] GPU acceleration via DirectML (Windows), CoreML (macOS), CUDA (optional)
- [ ] Texture → Tensor conversion (GPU-side if possible)
- [ ] Tensor → Texture conversion for output masks/heatmaps
- [ ] Model loading and caching

#### ML Operators
- [ ] **ONNXModel** — Generic ONNX model runner (`operators/onnx_model.cpp`)
  - [ ] Load .onnx model file
  - [ ] Configure input/output bindings
  - [ ] Run inference on texture input
  - [ ] Output tensor data as texture or values

#### Pose Detection (MoveNet Demo)
- [ ] Download/bundle MoveNet ONNX model (Lightning or Thunder variant)
- [ ] Implement `PoseDetector` operator
  - [ ] Input: webcam/video texture
  - [ ] Output: keypoint positions (17 joints × 3 values: x, y, confidence)
  - [ ] Output: skeleton overlay texture (optional)
- [ ] Create `examples/pose-detection` demo
  - [ ] Webcam → PoseDetector → Skeleton visualization
  - [ ] Drive other operators with keypoint data (e.g., particles at hand positions)

#### Future ML Models
- [ ] Background segmentation (MediaPipe Selfie Segmentation)
- [ ] Hand tracking (MediaPipe Hands)
- [ ] Face mesh (MediaPipe Face Mesh)
- [ ] Style transfer models
- [ ] Depth estimation (MiDaS)

---

## Phase 13: Export & Distribution

### 13.1 Video/Movie Export
- [ ] High-quality offline rendering mode (fixed timestep)
- [ ] Video encoding via FFmpeg (H.264, H.265, ProRes)
- [ ] Audio mixdown to video
- [ ] Configurable output resolution and framerate
- [ ] Progress indicator during export

### 13.2 WASM Export
- [ ] WebGPU backend for browser (via Emscripten)
- [ ] Compile operators to WASM
- [ ] HTML/JS wrapper for web deployment
- [ ] Asset bundling for web
- [ ] Touch input support for mobile browsers

### 13.3 Standalone Export
- [ ] Bundle runtime + operators into single executable
- [ ] Embed shaders and assets
- [ ] Cross-platform packaging (macOS .app, Windows .exe, Linux AppImage)

---

## Phase 14: Examples & Templates

### 14.1 Core Feature Examples
**Each example demonstrates a specific capability**

#### Input & Interaction
- [x] `examples/webcam` — Camera input processing
- [x] `examples/video-playback` — Video file input (H.264 tested, VideoFile operator)
- [ ] `examples/mouse-draw` — Mouse position/click to draw trails or control effects
- [ ] `examples/keyboard-control` — Keyboard triggers and parameter control
- [ ] `examples/gamepad` — Game controller input for live performance
- [ ] `examples/image-sequence` — Load and animate through image sequences

#### Audio & Music
- [x] `examples/audio-reactive` — Audio input driving visuals (mic/line-in)
- [ ] `examples/beat-detection` — Audio beat detection (see `beat_detection.md`)
- [ ] `examples/fft-visualizer` — Frequency spectrum display
- [ ] `examples/midi-control` — MIDI CC controlling parameters
- [ ] `examples/osc-remote` — OSC input for remote control (TouchOSC, etc.)

#### Texture Sharing & Output
- [ ] `examples/syphon-out` — Send texture to other apps via Syphon (macOS)
- [ ] `examples/spout-out` — Send texture to other apps via Spout (Windows)
- [ ] `examples/syphon-in` — Receive texture from other apps via Syphon
- [ ] `examples/spout-in` — Receive texture from other apps via Spout
- [ ] `examples/ndi-stream` — NDI network video streaming
- [ ] `examples/record-video` — Export to H.264/ProRes video file

#### Effects & Processing
- [x] `examples/hello` — Basic noise operator
- [x] `examples/chain-demo` — Chain API with kaleidoscope feedback
- [ ] `examples/feedback` — Feedback/trail effect variations
- [ ] `examples/composite-modes` — Blend mode comparison (add, multiply, screen, etc.)
- [ ] `examples/displacement` — Texture displacement techniques
- [ ] `examples/lfo-modulation` — LFO driving parameters over time
- [x] `examples/post-processing` — Bloom, vignette, color grading chain

#### 3D & Geometry
- [x] `examples/3d-demo` — 3D rendering basics (primitives, camera, instancing)
- [ ] `examples/3d-geometry` — 3D rendering basics (primitives, camera, lighting)
- [x] `examples/model-loader` — Load OBJ models with orbit camera
- [x] `examples/lighting-demo` — Phong/PBR shading with multiple lights
- [ ] `examples/particles` — Particle system with emitters
- [x] `examples/3d-instancing` — GPU instancing for many objects
- [ ] `examples/orbit-camera` — Interactive 3D camera controls

#### Text & Overlay
- [ ] `examples/text` — Text rendering with fonts
- [ ] `examples/debug-overlay` — On-screen debug info display

#### Advanced
- [ ] `examples/multi-window` — Multiple output windows
- [ ] `examples/pose-detection` — MoveNet skeleton tracking from webcam (requires ONNX)
- [ ] `examples/web-server` — Host a simple web interface that interacts with nodes
- [ ] `examples/time-machine` — 3D texture frame cache with temporal displacement

### 14.1b Showcase Examples
**Larger examples demonstrating multiple features working together**

- [ ] `examples/wipeout-vehicle` — Audio-reactive Wipeout 2097-style hover vehicle generator
  - [ ] Procedural angular 3D geometry (wedge shapes, fins, cockpit)
  - [ ] PBR metallic materials with roughness variation
  - [ ] Team livery decals via stencil projection
  - [ ] Grime/weathering texture overlays
  - [ ] Glowing engine exhaust (emissive + bloom)
  - [ ] Orbit camera for viewing
  - [ ] Audio reactivity: beat → engine glow, bass → hover height, mids → color cycle
  - [ ] Particle exhaust trail

### 14.2 Template Projects
- [ ] Minimal template (single operator)
- [ ] VJ performance template (audio-reactive + MIDI)
- [ ] Installation template (multi-screen + OSC)
- [ ] Generative art template (recording-ready)

---

## Phase 14b: Addon System Architecture
**Foundation for community-contributed addons with fast hot-reload**

The addon system enables modular extensions (Spout, Syphon, NDI, etc.) that compile once and link quickly during hot-reload. This is foundational for community contributions.

### 14b.1 Addon Directory Structure
```
addons/
  vivid-spout/           # Example addon
    include/
      vivid/spout/
        spout.h          # Public API header
    src/
      spout_sender.cpp   # Implementation
      spout_receiver.cpp
    lib/                 # Pre-built output (created by build)
      vivid-spout.lib    # Windows
      libvivid-spout.a   # macOS/Linux
    addon.json           # Addon metadata
    CMakeLists.txt       # Addon build script
```

### 14b.2 Addon Build System
- [ ] Create `addons/CMakeLists.txt` — master addon builder
- [ ] Integrate addon building into main Vivid CMake build
- [ ] Each addon builds as static library (`add_library(vivid-spout STATIC ...)`)
- [ ] Install addon libraries to `build/addons/lib/`
- [ ] Install addon headers to `build/addons/include/`
- [ ] Copy `addon.json` to `build/addons/`

### 14b.3 Addon Metadata (addon.json)
- [ ] Define JSON schema for addon metadata
- [ ] Required fields: `name`, `version`, `platforms`, `detect_headers`
- [ ] Optional fields: `dependencies`, `description`, `author`, `license`
- [ ] Platform-specific library paths
- [ ] Example:
  ```json
  {
    "name": "spout",
    "version": "1.0.0",
    "description": "Spout texture sharing for Windows",
    "platforms": ["windows"],
    "detect_headers": ["vivid/spout/spout.h"],
    "include_dirs": ["include"],
    "libraries": {
      "windows": ["lib/vivid-spout.lib", "opengl32.lib"]
    }
  }
  ```

### 14b.4 Compiler Auto-Detection
- [ ] Implement `AddonRegistry` class in `runtime/src/addon_registry.h`
- [ ] Load all `addon.json` files from `addons/` directory at startup
- [ ] Implement `scanSourceForAddons(path)` — parse #include directives
- [ ] Match includes against `detect_headers` patterns
- [ ] Return list of required addons for a project
- [ ] Generate CMakeLists.txt with addon linkage automatically

### 14b.5 CMakeLists Generation
- [ ] Update `Compiler` class to use addon detection
- [ ] Template-based CMakeLists generation with addon includes
- [ ] Add addon include directories to generated CMake
- [ ] Add addon libraries to `target_link_libraries`
- [ ] Handle platform-specific addon selection

### 14b.6 User Experience
- [ ] No custom CMakeLists.txt needed when using addons
- [ ] Just `#include <vivid/spout/spout.h>` and it works
- [ ] Fast hot-reload (only user code recompiles)
- [ ] Clear error messages if addon not found/not built

### 14b.7 Migration
- [ ] Update vivid-spout to new architecture
- [ ] Create vivid-syphon with new architecture
- [ ] Remove need for custom CMakeLists in spout-in/spout-out examples
- [ ] Document addon creation process

---

## Phase 15: Community & Package Registry
**Reference: [PLAN-07-community.md](PLAN-07-community.md)**

A package management system for sharing and distributing Vivid operators (like npm/pip/Cargo).

### 15.1 Package Format (Foundation)
- [ ] Define `vivid.toml` manifest schema
- [ ] Document package directory structure
- [ ] Create `vivid init` command — scaffold new package
- [ ] Package template with README, LICENSE, examples
- [ ] Update runtime to read manifest metadata
- [ ] Operator discovery from manifest

### 15.2 Local Package Management
- [ ] Package cache directory (`~/.vivid/packages/`)
- [ ] `vivid install <path>` — install from local path
- [ ] `vivid install <git-url>` — install from git repository
- [ ] `vivid list` — show installed packages
- [ ] `vivid uninstall` — remove packages
- [ ] `vivid build` — build package locally
- [ ] Dependency resolution for local packages

### 15.3 Registry Server
- [ ] Design registry REST API
- [ ] Package metadata storage (PostgreSQL)
- [ ] Package file storage (S3/MinIO)
- [ ] User accounts and authentication
- [ ] API token management
- [ ] Package upload/download endpoints
- [ ] Search and discovery API
- [ ] Rate limiting

### 15.4 Publishing & Distribution
- [ ] `vivid register` — create account
- [ ] `vivid login` / `vivid logout` — authentication
- [ ] `vivid check` — validate package before publish
- [ ] `vivid publish` — upload to registry
- [ ] Pre-publish validation (manifest, build, security scan)
- [ ] Version yanking (soft delete)
- [ ] Package ownership management
- [ ] README and documentation extraction

### 15.5 Web Interface
- [ ] Package browser (featured, recent, popular)
- [ ] Package detail pages with README rendering
- [ ] Version history and changelogs
- [ ] Download statistics
- [ ] User profiles
- [ ] Search with filters (category, keyword, author)
- [ ] Documentation pages

### 15.6 Advanced Features (Future)
- [ ] Semantic version resolution (^, ~, ranges)
- [ ] Transitive dependency resolution
- [ ] Package namespacing (`@author/package`)
- [ ] Package signing (GPG)
- [ ] Checksum verification
- [ ] Private/enterprise registries
- [ ] Organization accounts
- [ ] 2FA support

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
| **Phase 0 complete** | **Declaratively chain operators with `setup()` / `update()` API** |
| Phase 2 complete | See a window with a solid color |
| Phase 3 complete | See animated noise via shader |
| Phase 5 complete | Edit .cpp, see changes without restart |
| Phase 7 complete | Chain multiple operators together |
| Phase 8 complete | Run the full hello example |
| Phase 10 complete | See inline previews in VS Code |
| Phase 11 complete | Share with others |
| Phase 12 complete | Load images/video, use audio, render 3D |
| Phase 13 complete | Export video files, deploy to web |
| Phase 14 complete | Have examples demonstrating all features |
| Phase 15 complete | Share operators via community registry |
