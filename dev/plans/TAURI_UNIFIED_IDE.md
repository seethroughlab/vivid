# Tauri Unified IDE Implementation Plan

## Overview

Build a single-window Vivid IDE using Tauri with native wgpu rendering and transparent webview overlay for UI.

**Status: Phases 0-4 Complete** (2025-01-17)
- No flickering observed
- Transparency working
- wgpu + webview overlay architecture confirmed viable
- Terminal with PTY working (Claude Code can run inside)
- Monaco editor with C++/WGSL syntax highlighting
- Vivid EditorBridge WebSocket connection with auto-reconnect
- C API for vivid-core complete (libvivid-c.dylib)
- Rust FFI bindings complete (vivid-sys + vivid crates)
- egui node graph rendering working (wgpu + egui in single window!)
- **wgpu surface creation issue SOLVED** - use MainEventsCleared + frame delay

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ Tauri Window                                                    │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │                                                             │ │
│ │              [wgpu surface - renders Vivid chain]           │ │
│ │                                                             │ │
│ │  ┌─────────────────────────────────────────────────────┐    │ │
│ │  │  Transparent WebView Overlay                        │    │ │
│ │  │  ┌─────────────┬─────────────────────────────────┐  │    │ │
│ │  │  │ Inspector   │  Terminal (xterm.js)            │  │    │ │
│ │  │  │ HTML widgets│  Claude Code runs here          │  │    │ │
│ │  │  ├─────────────┼─────────────────────────────────┤  │    │ │
│ │  │  │ Node Graph  │  Monaco Editor                  │  │    │ │
│ │  │  └─────────────┴─────────────────────────────────┘  │    │ │
│ │  └─────────────────────────────────────────────────────┘    │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Progress Tracking

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| 0 | Spike - Validate architecture | ✅ Complete | No flickering, transparency works |
| 1 | Terminal Integration | ✅ Complete | PTY spawning, xterm.js connected |
| 2 | Monaco Editor | ✅ Complete | C++/WGSL highlighting, file open/save, Cmd+S |
| 3 | Vivid Connection | ✅ Complete | EditorBridge WebSocket, auto-reconnect, status UI |
| 4 | Embed vivid-core | ✅ Complete | C API, Rust bindings, egui node graph rendering working |
| 5 | Inspector Panel | ⬜ Not started | egui parameter controls |
| 6 | Polish | ⬜ Not started | Keyboard shortcuts, layouts |
| 7 | Repository Separation | ⬜ Future | Extract to vivid-ide repo when C API stable |

---

## How to Run

```bash
cd /Users/jeff/Developer/vivid/tauri

# Build (if needed)
cargo tauri build --debug

# Run the app
./src-tauri/target/debug/vivid-tauri

# Or use npm for development
npm run tauri dev
```

To test Vivid connection:
1. Start the Tauri IDE
2. In another terminal, run Vivid: `./build/bin/vivid projects/getting-started/02-hello-noise`
3. The status indicator in the IDE titlebar should turn green

---

## Project Structure

```
vivid/tauri/
├── src-tauri/
│   ├── Cargo.toml              # Rust dependencies
│   ├── tauri.conf.json         # Tauri config (macOSPrivateApi enabled)
│   ├── capabilities/
│   │   └── default.json        # Permissions for events, dialogs, shell
│   └── src/
│       ├── main.rs             # App entry, wgpu render loop, command registration
│       ├── lib.rs              # Module exports
│       ├── wgpu_state.rs       # WebGPU surface, pipeline, uniforms
│       ├── shader.wgsl         # Animated noise shader (placeholder)
│       ├── pty.rs              # PTY management for terminal
│       └── file_ops.rs         # File read/write commands
├── src/
│   ├── main.ts                 # Frontend entry, terminal, editor, Vivid connection
│   ├── vivid-connection.ts     # WebSocket client for EditorBridge protocol
│   └── styles.css              # Transparent overlay styles, connection status
├── index.html                  # App shell with panel layout
├── package.json                # NPM dependencies
├── vite.config.ts              # Vite + Monaco editor plugin
└── tsconfig.json               # TypeScript config
```

---

## Phase 0: Spike (Complete)

**Goal:** Validate that Tauri + wgpu + transparent webview works without flickering.

**Key Technical Details:**
- wgpu 23 with Metal backend on macOS
- Tauri 2.0 with `macOSPrivateApi: true` for transparency
- `CompositeAlphaMode::PostMultiplied` required (PreMultiplied not supported)
- Uniform buffer alignment: vec2 needs 8-byte alignment, added padding fields
- Used `create_surface_unsafe` for Tauri window surface

**Files:**
- `src-tauri/src/wgpu_state.rs` - wgpu initialization and render loop
- `src-tauri/src/shader.wgsl` - Animated noise shader

---

## Phase 1: Terminal Integration (Complete)

**Goal:** Connect xterm.js to a real PTY so Claude Code can run inside the app.

**Key Technical Details:**
- `portable-pty` crate for cross-platform PTY
- Tauri commands: `spawn_shell`, `write_pty`, `resize_pty`, `close_pty`
- PTY output sent via Tauri events (`pty-output`, `pty-exit`)
- Window dragging: `data-tauri-drag-region` didn't work, used programmatic `getCurrentWindow().startDragging()`
- Capabilities file required for event listening permissions

**Files:**
- `src-tauri/src/pty.rs` - PtyManager with session tracking
- `src-tauri/capabilities/default.json` - Event and window permissions

---

## Phase 2: Monaco Editor (Complete)

**Goal:** Add a code editor panel for editing chain.cpp and shaders.

**Key Technical Details:**
- Monaco editor with transparent background (`vivid-dark` theme)
- Custom WGSL syntax highlighting via Monarch tokenizer
- File dialogs via `tauri-plugin-dialog`
- Cmd+S keyboard shortcut for save
- Modified indicator (●) in filename when unsaved

**Files:**
- `src/main.ts` - `initEditor()`, `registerWGSLLanguage()`, `openFile()`, `saveFile()`
- `src-tauri/src/file_ops.rs` - `read_file`, `write_file`, `get_file_name`
- `vite.config.ts` - Monaco editor Vite plugin configuration

**Dependencies Added:**
- `monaco-editor` - Code editor
- `vite-plugin-monaco-editor` - Vite integration for workers
- `tauri-plugin-dialog` - Native file dialogs
- `@tauri-apps/plugin-dialog` - JS API

---

## Phase 3: Vivid Connection (Complete)

**Goal:** Connect the Tauri IDE to a running Vivid instance for live preview.

**Key Technical Details:**
- WebSocket connection to `ws://localhost:9876` (EditorBridge)
- Full protocol implementation with TypeScript types
- Auto-reconnect every 2 seconds when disconnected
- Connection status indicator: green (connected), yellow (connecting), red (disconnected)
- Ready to receive: `operator_list`, `param_values`, `chain_structure`, `compile_status`, `frame_info`

**Files:**
- `src/vivid-connection.ts` - `VividConnection` class with all protocol messages
- `src/main.ts` - `initVividConnection()`, `handleVividMessage()`, `updateVividStatus()`
- `src/styles.css` - Connection status indicator styles with animations

**Available Commands (via vividConnection):**
- `setParam()`, `setParamImmediate()` - Parameter control
- `soloNode()`, `soloExit()` - Preview isolation
- `selectNode()`, `focusNode()` - Node selection
- `reload()` - Trigger hot-reload
- `commitChanges()`, `discardChanges()` - Pending changes workflow
- `captureFrame()` - Screenshot
- `advanceFrames()`, `resetTime()` - Animation control
- Window controls: fullscreen, borderless, alwaysOnTop, monitor

---

## Phase 4: Embed vivid-core (Not Started)

**Goal:** Run vivid-core as an embedded library within Tauri, sharing the GPU context.

**Why this approach:**
- Enables live texture previews in node graph without streaming
- The IDE owns the wgpu device, vivid-core uses it
- Chain visualizer code moves OUT of vivid-core into the IDE
- Cleaner separation: vivid-core = runtime, IDE = visualization

**Architecture:**
```
Tauri (Rust)
  └── owns wgpu::Device
  └── embeds vivid-core (C++ via FFI)
  └── queries operator texture views directly
  └── renders node graph with egui
```

**Sub-phases:**

### 4a: Create C API for vivid-core ✅ Complete
- [x] Define C API in `include/vivid/vivid_c.h`
- [x] Expose: chain create/destroy, load project, process frame
- [x] Expose: operator iteration, get name/type/connections
- [x] Expose: `vivid_operator_output_view()` → WGPUTextureView
- [x] Expose: parameter get/set
- [x] Build as shared library (libvivid-c.dylib)

### 4b: Accept external wgpu device ✅ Complete
- [x] Modify Context to accept external WGPUDevice/WGPUQueue (headless constructor)
- [x] Input injection methods for headless mode
- [ ] Ensure texture formats match between Tauri and vivid-core
- [ ] Test: Tauri creates device, passes to vivid-core, renders

### 4c: Rust bindings ✅ Complete
- [x] Create `vivid-sys` crate with FFI bindings
- [x] Create safe `vivid` wrapper crate
- [x] Integrate into Tauri build (workspace + dependency)

**Files created:**
- `modules/vivid-core/include/vivid/vivid_c.h` - C API header
- `modules/vivid-core/src/vivid_c.cpp` - C API implementation
- `tauri/crates/vivid-sys/` - Raw FFI bindings crate
- `tauri/crates/vivid/` - Safe Rust wrapper crate
- `tauri/Cargo.toml` - Workspace configuration

### 4d: egui node graph ✅ Complete
- [x] Add egui + egui_node_graph2 to Tauri (egui 0.29, egui-wgpu 0.29, wgpu 22)
- [x] Create `EguiState` struct with rendering pipeline
- [x] Create `VividNodeGraph` struct with operator nodes
- [x] Implement data types: Texture, Value, Audio, Geometry, Camera, Light, Event
- [x] Implement connections between nodes
- [x] Add solo mode for viewing single node output
- [x] Integrate egui rendering into wgpu render loop
- [x] **SOLVED:** Enable native rendering via MainEventsCleared + frame delay
- [ ] Display operator texture previews via `egui::Image::from_texture`
- [ ] Implement: pan, zoom (provided by egui_node_graph2)
- [ ] Port mini-map from current NodeGraph implementation

**Files created:**
- `src-tauri/src/egui_state.rs` - egui context, wgpu renderer, input injection
- `src-tauri/src/node_graph.rs` - VividNodeGraph using egui_node_graph2
- `src-tauri/src/main.rs` - Deferred wgpu init via MainEventsCleared

**Reference:** [egui_node_graph2](https://github.com/trevyn/egui_node_graph2) - Used by Blackjack (3D procedural modeler)

**What to do with existing visualizer code in vivid-core:**
- `chain_visualizer.cpp/.h` — Remove or keep behind `#ifdef VIVID_STANDALONE`
- `gui/node_graph.cpp/.h` — Remove (replaced by egui_node_graph2)
- `gui/overlay_canvas.cpp/.h` — Remove (replaced by egui rendering)
- `--show-ui` flag — Keep for standalone mode, but minimal (no node graph)

---

## Phase 5: Inspector Panel (Not Started)

**Goal:** Live parameter controls that update operators in real-time.

**Planned Approach:**
- Build inspector as egui panel (native, not webview)
- Query parameters via C API from vivid-core
- Update parameters directly (no WebSocket round-trip)
- Track pending changes for Claude-first workflow

**Tasks:**
- [ ] egui widgets for each param type (sliders, checkboxes, color pickers)
- [ ] Query operator params via `vivid_operator_get_params()`
- [ ] Set params via `vivid_operator_set_param()`
- [ ] "Pending changes" indicator for Claude workflow
- [ ] Keyboard shortcut to commit/discard changes

**Parameter Types to Support:**
- Float, Int, Bool (sliders, checkboxes)
- Vec2, Vec3, Vec4 (multi-value sliders)
- Color (color picker)
- String, FilePath (text input, file browser)

---

## Phase 6: Polish (Not Started)

**Tasks:**
- [ ] Keyboard shortcuts (Cmd+1/2/3 for panels)
- [ ] Persistent layout (localStorage)
- [ ] Error display from compile_status
- [ ] App icon and branding
- [ ] macOS menu bar integration

---

## Phase 7: Repository Separation (Future)

**Goal:** Extract the Tauri IDE to a separate repository (`vivid-ide`) once the C API stabilizes.

**Why wait:**
- During active development, the C API is evolving
- Easier to iterate with vivid-core and IDE in one repo
- Need to stabilize: API surface, texture format contracts, build system

**When ready:**
- Version the C API (e.g., `VIVID_API_VERSION` constant)
- Treat `vivid_c.h` as a stable contract
- Publish `libvivid.dylib` / `.dll` / `.so` as release artifacts
- Document the C API formally
- Extract `tauri/` to `github.com/seethroughlab/vivid-ide`
- IDE links against published libvivid

**What stays in vivid repo:**
- vivid-core (runtime, operators, chain)
- C API (`vivid_c.h`, libvivid)
- CLI (`./vivid` for standalone/headless use)
- WebSocket API (for MCP, external tools)

**What moves to vivid-ide repo:**
- Tauri app
- egui node graph
- Inspector panel
- Monaco editor integration
- Terminal/PTY

**Benefits after separation:**
- Keeps Vivid "pure" — focused on runtime/operators/chain
- Independent release cycles
- Smaller clone size for framework-only users
- IDE can iterate faster without touching core
- Other IDEs/tools can link against libvivid

---

## Technical Notes

### Transparency on macOS
Requires `macOSPrivateApi: true` in tauri.conf.json and appropriate alpha mode:
```rust
let alpha_mode = surface_caps.alpha_modes.iter()
    .find(|m| **m == wgpu::CompositeAlphaMode::PostMultiplied)
    .unwrap_or(&surface_caps.alpha_modes[0]);
```

### Uniform Buffer Alignment
WGSL requires proper alignment for uniform buffers:
```rust
#[repr(C)]
struct Uniforms {
    time: f32,            // offset 0
    _pad1: f32,           // offset 4 (align vec2 to 8)
    resolution: [f32; 2], // offset 8
    _pad2: [f32; 2],      // offset 16 (total 24 bytes)
}
```

### Window Surface Lifetime
Use `create_surface_unsafe` for Tauri window:
```rust
let surface = unsafe {
    instance.create_surface_unsafe(
        wgpu::SurfaceTargetUnsafe::from_window(&window)
            .expect("Failed to create surface target")
    ).expect("Failed to create surface")
};
```

### Window Dragging
The `data-tauri-drag-region` HTML attribute doesn't work reliably. Use programmatic dragging:
```typescript
titlebar.addEventListener("mousedown", async (e) => {
    if ((e as MouseEvent).button === 0) {
        await getCurrentWindow().startDragging();
    }
});
```

### wgpu Surface Creation in Tauri (SOLVED)

**Problem:** Creating a wgpu surface from Tauri's webview window during `setup()` causes a null pointer dereference on macOS. The Metal layer isn't available at that point.

**Solution:** Use `MainEventsCleared` event with a frame delay, and initialize on the main thread.

**Key insights:**
1. Metal layer requires initialization on the **main UI thread** (not a tokio worker)
2. The layer isn't ready during `setup()` or even `RunEvent::Ready`
3. Waiting ~30 frames via `MainEventsCleared` gives the layer time to initialize

**Working Implementation:**
```rust
static WGPU_STATE: OnceLock<Arc<WgpuState>> = OnceLock::new();
static FRAME_COUNT: AtomicU64 = AtomicU64::new(0);
static INIT_ATTEMPTED: AtomicBool = AtomicBool::new(false);

.run(|app_handle, event| {
    match event {
        RunEvent::MainEventsCleared => {
            let frame = FRAME_COUNT.fetch_add(1, Ordering::SeqCst);

            // Wait ~30 frames before init (main thread, after Metal layer ready)
            if frame == 30 && !INIT_ATTEMPTED.swap(true, Ordering::SeqCst) {
                if let Some(window) = app_handle.get_webview_window("main") {
                    // Use pollster::block_on (NOT async_runtime::spawn!)
                    // This keeps us on the main thread
                    let wgpu_state = pollster::block_on(WgpuState::new(&window));
                    WGPU_STATE.set(Arc::new(wgpu_state)).ok();
                }
            }

            // Render every frame after init
            if let Some(state) = WGPU_STATE.get() {
                state.render(time).ok();
            }
        }
        _ => {}
    }
});
```

**What doesn't work:**
- `setup()` callback - Metal layer not ready, null pointer
- `RunEvent::Ready` - Metal layer still not ready
- `async_runtime::spawn()` - Runs on tokio worker thread, Metal requires main thread

### Tauri 2.0 Capabilities
Permissions are defined in `src-tauri/capabilities/default.json`:
```json
{
  "permissions": [
    "core:default",
    "core:event:allow-listen",
    "core:event:allow-emit",
    "core:window:allow-start-dragging",
    "dialog:default",
    "dialog:allow-open",
    "dialog:allow-save",
    "shell:default"
  ]
}
```

---

## Dependencies

### Rust (Cargo.toml)
```toml
tauri = { version = "2", features = ["macos-private-api"] }
tauri-plugin-dialog = "2"
tauri-plugin-shell = "2"
wgpu = "22"  # Pinned to 22 for egui-wgpu 0.29 compatibility
tokio = { version = "1", features = ["time", "rt", "sync", "io-util"] }
portable-pty = "0.8"
bytemuck = { version = "1", features = ["derive"] }
parking_lot = "0.12"

# egui for native UI (pinned to 0.29 for egui_node_graph2 compatibility)
egui = "0.29"
egui-wgpu = "0.29"
egui_node_graph2 = "0.7"

# Vivid embedding
vivid = { path = "../crates/vivid" }
```

### JavaScript (package.json)
```json
{
  "dependencies": {
    "@xterm/xterm": "^5.5.0",
    "@xterm/addon-fit": "^0.10.0",
    "@xterm/addon-web-links": "^0.11.0",
    "monaco-editor": "^0.55.1"
  },
  "devDependencies": {
    "@tauri-apps/api": "^2.0.0",
    "@tauri-apps/plugin-dialog": "^2.0.0",
    "@tauri-apps/cli": "^2.0.0",
    "vite-plugin-monaco-editor": "^1.1.0"
  }
}
```

---

## References

- [tauri-wgpu-cam](https://github.com/clearlysid/tauri-wgpu-cam) - Reference project for wgpu+webview
- [Tauri Discussion #11944](https://github.com/tauri-apps/tauri/discussions/11944) - wgpu overlay discussion
- [portable-pty](https://docs.rs/portable-pty) - Cross-platform PTY crate
- [Monaco Editor](https://microsoft.github.io/monaco-editor/) - VS Code's editor component
- [Elk.js](https://github.com/kieler/elkjs) - Graph layout library (for Phase 4)
- [EditorBridge Protocol](../docs/WEBSOCKET_API.md) - Full WebSocket API documentation
