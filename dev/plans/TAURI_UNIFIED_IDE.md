# Tauri Unified IDE Implementation Plan

## Overview

Build a single-window Vivid IDE using Tauri with native wgpu rendering and transparent webview overlay for UI.

**Status: Phases 0-5 Complete, Phase 6 In Progress** (2025-01-18)
- No flickering observed
- Transparency working
- wgpu + webview overlay architecture confirmed viable
- Terminal with PTY working (Claude Code can run inside)
- Monaco editor with C++/WGSL syntax highlighting
- C API for vivid-core complete (libvivid-c.dylib)
- Rust FFI bindings complete (vivid-sys + vivid crates)
- egui node graph rendering working (wgpu + egui in single window!)
- Inspector panel with operator list and parameter controls
- Hot-reload on save (Cmd+S triggers reload)
- **wgpu surface creation issue SOLVED** - use MainEventsCleared + frame delay

---

## Architecture Decision: C API vs WebSocket

**Decision: Use C API for all Tauri IDE communication** (2025-01-18)

### Context
Vivid has two communication interfaces:
1. **C API** (`libvivid-c.dylib`) - For embedding vivid-core in external apps
2. **WebSocket RuntimeAPI** (port 9876) - For external tools (MCP, VS Code extension)

### Why C API for Tauri IDE

1. **RuntimeAPI is in CLI, not vivid-core**: The WebSocket server lives in `src/cli/runtime_api.cpp`. When Tauri embeds vivid-core directly, the CLI isn't running, so WebSocket isn't available.

2. **Single-window architecture requires embedding**: The IDE renders vivid directly in its window via `Context::with_window()`. This requires C API for window handle passing and render loop control.

3. **C API is already complete**: All needed functions exist:
   - Window/render: `with_window()`, `render_frame()`, `resize()`
   - Input: `set_mouse_position()`, `set_mouse_button()`, `scroll()`
   - Control: `get_operators()`, `get_params()`, `set_param()`, `reload()`
   - Selection: `select_operator()`, `get_selected_operator()`

4. **No duplicate maintenance**: Both C API and RuntimeAPI wrap the same `Context`/`Chain` methods. The IDE uses C API; external tools use WebSocket. Each serves its purpose.

### What WebSocket is for

The RuntimeAPI WebSocket (port 9876) remains the interface for:
- **MCP server** (`vivid mcp`) - Claude Code integration
- **Future external tools** - Any tool that connects to a running vivid process

The Tauri IDE **replaces** the VS Code extension. External tools connect to a **running vivid process** (CLI), while the Tauri IDE **embeds** vivid-core directly.

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Tauri IDE (embeds vivid-core)                                           │
│                                                                         │
│   TypeScript UI ──Tauri Commands──► Rust ──C API──► vivid-core          │
│                                                                         │
│   - Inspector panel                 - get_operators()                   │
│   - Parameter controls              - set_param()                       │
│   - Monaco editor                   - reload_project()                  │
│   - Error banner                    - get_compile_status()              │
│                                                                         │
│   Uses: invoke("get_operators")     Uses: libvivid-c.dylib              │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│ External Tools (connect to running vivid CLI)                           │
│                                                                         │
│   MCP Server ────┬──WebSocket (9876)──► RuntimeAPI ──► vivid-core     │
│   Future Tools ──┘                                                      │
│                                                                         │
│   Uses: ws://localhost:9876         Lives in: src/cli/runtime_api.cpp │
└─────────────────────────────────────────────────────────────────────────┘
```

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
| 3 | Vivid Connection | ✅ Complete | Now uses Tauri commands (C API) instead of WebSocket |
| 4 | Embed vivid-core | ✅ Complete | C API, Rust bindings, chain renders with visualizer |
| 5 | Inspector Panel | ✅ Complete | Operator list, params, bidirectional selection sync, zoom |
| 6 | Polish | ✅ Complete | Error display, shortcuts, layout, icons, menus |
| 7 | Repository Separation | ✅ Complete | https://github.com/seethroughlab/vivid-ide |

---

## How to Run

```bash
# First, build vivid-core (required for the C API library)
cd /Users/jeff/Developer/vivid
cmake -B build && cmake --build build

# Then run the Tauri IDE
cd tauri
npm run tauri dev
```

The IDE will auto-load a test project (`projects/getting-started/02-operator-pipeline`) on startup.

**Note:** The embedded architecture means vivid-core runs inside the Tauri app - no separate vivid process needed. The chain renders directly in the IDE window.

---

## Project Structure

```
vivid/tauri/
├── src-tauri/
│   ├── Cargo.toml              # Rust dependencies
│   ├── tauri.conf.json         # Tauri config (macOSPrivateApi enabled)
│   ├── build.rs                # Copies libvivid-c.dylib to target dir
│   ├── capabilities/
│   │   └── default.json        # Permissions for events, dialogs, shell
│   └── src/
│       ├── main.rs             # App entry, vivid-core integration, Tauri commands
│       ├── lib.rs              # Module exports
│       ├── pty.rs              # PTY management for terminal
│       └── file_ops.rs         # File read/write commands
├── crates/
│   ├── vivid-sys/              # Raw C FFI bindings (bindgen-style)
│   └── vivid/                  # Safe Rust wrapper for vivid-core
├── src/
│   ├── main.ts                 # Frontend entry, terminal, editor, inspector
│   ├── vivid-api.ts            # C API wrappers via Tauri commands
│   ├── vivid-connection.ts     # WebSocket client (unused, kept for reference)
│   └── styles.css              # Transparent overlay styles
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

**Goal:** Connect the IDE to vivid-core for live preview and control.

**Implementation:**
Originally planned as WebSocket connection, but switched to **C API via Tauri commands** because:
- Tauri embeds vivid-core directly (no separate process)
- RuntimeAPI WebSocket server is in CLI, not vivid-core
- C API provides all needed functionality

**Key Technical Details:**
- Tauri commands invoke C API functions via Rust FFI
- Status indicator shows "Vivid Active" when rendering
- Polling for state updates (operators, compile status, selection)

**Files:**
- `src/vivid-api.ts` - TypeScript wrappers for Tauri commands
- `src/main.ts` - `initVividState()`, `refreshVividState()`, polling loops
- `src-tauri/src/main.rs` - Tauri command handlers calling C API

**Note:** `src/vivid-connection.ts` still exists with WebSocket client code, but is not used by the Tauri IDE. It remains as reference for how external tools connect to a running vivid process.

---

## Phase 4: Embed vivid-core (Complete)

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

## Phase 5: Inspector Panel (Complete)

**Goal:** Live parameter controls that update operators in real-time.

**Implementation (2025-01-17):**
- Built inspector in webview (HTML/CSS/TypeScript) instead of egui
- Uses Tauri commands to query/set parameters via C API
- Operator list shows all chain operators
- Click operator to see its parameters
- Real-time parameter updates affect live rendering
- **Bidirectional selection sync:** Click in webview Inspector ↔ updates vivid-core node graph
- **Zoom/scroll:** Mouse wheel over node graph area zooms/pans correctly
- **Enum labels:** Fixed dangling pointer issue with thread-local caching

**Tauri Commands Added:**
- `get_project_info()` - Returns project path and chain.cpp location
- `get_compile_status()` - Returns compilation errors with line numbers
- `get_operators()` - Returns list of operators with names/types
- `get_operator_params(op_name)` - Returns parameter declarations and current values
- `set_param(op_name, param_name, value)` - Updates parameter in real-time
- `reload_project()` - Triggers chain reload
- `get_selected_operator()` - Gets selected operator name from vivid-core
- `select_operator(name)` - Selects operator in vivid-core visualizer

**Parameter Types Supported:**
- [x] Float, Int (sliders with min/max)
- [x] Bool (checkboxes)
- [x] Vec2, Vec3, Vec4 (multi-component sliders)
- [x] Color (color picker)
- [x] Enum (dropdown select)
- [ ] String, FilePath (text input, file browser) - TODO

**Keyboard Shortcuts:**
- Tab - Toggle visualizer (outside editor/terminal)
- Cmd+R - Reload project (outside editor)

---

## Phase 6: Polish (In Progress)

**Completed:**
- [x] Error display from compile_status
  - Prominent error banner above editor with click-to-jump
  - Shows error message and line:column location
  - Editor panel gets red border/glow when errors exist
  - Flash animation when jumping to error line
  - Success message shows briefly then fades
  - Dismiss button to hide banner
- [x] Hot-reload on save (Cmd+S triggers reload for .cpp/.h/.hpp files)

**All Tasks Complete:**
- [x] **Open Project UI** - Folder button in titlebar, Cmd+O shortcut, title shows project name
- [x] **Keyboard shortcuts** - Cmd+1 terminal, Cmd+2 inspector, Cmd+3 editor, Cmd+E editor, Cmd+O open project, Cmd+N new project, Cmd+R reload, Tab toggle visualizer
- [x] **Persistent layout** - Panel collapsed states saved to localStorage, restored on startup
- [x] **App icon and branding** - Configured in tauri.conf.json with bundle metadata
- [x] **macOS menu bar integration** - Native menu with File, Edit, View, Window, Help menus

---

## Phase 7: Repository Separation & Distribution

**Status:** ✅ Complete - Repository created at https://github.com/seethroughlab/vivid-ide

**Goal:** Extract the Tauri IDE to a separate repository (`vivid-ide`) with comprehensive distribution strategy.

### Preparation Complete:
- [x] **C API Versioning** - `VIVID_API_VERSION 1` defined in `vivid_c.h`
  - Compile-time constants: `VIVID_API_VERSION`, `VIVID_VERSION_STRING`, `VIVID_VERSION_MAJOR/MINOR/PATCH`
  - Runtime functions: `vivid_get_api_version()`, `vivid_get_version()`
  - Version history documented in header
- [x] **C API Documentation** - Full doxygen-style docs in `vivid_c.h`
- [x] **Repository Creation Script** - `dev/scripts/create-vivid-ide-repo.sh`
  - Creates vivid-ide directory structure
  - Sets up vivid as git submodule
  - Copies Tauri app files
  - Configures build.rs for library paths
  - Creates README, LICENSE, and .gitignore
- [x] **CI/CD Templates** - `dev/templates/vivid-ide-release.yml`
  - Downloads pre-built vivid runtime from releases
  - Builds Tauri app for all platforms
  - Code signing configuration ready

### To Execute Separation:

```bash
# 1. Run the creation script
./dev/scripts/create-vivid-ide-repo.sh ~/Developer/vivid-ide

# 2. Create GitHub repo and push
cd ~/Developer/vivid-ide
gh repo create seethroughlab/vivid-ide --public --source=. --push

# 3. Set up secrets for code signing (in GitHub repo settings)
# - APPLE_CERTIFICATE, APPLE_CERTIFICATE_PASSWORD
# - APPLE_SIGNING_IDENTITY, APPLE_ID, APPLE_PASSWORD, APPLE_TEAM_ID

# 4. Copy CI workflow
mkdir -p .github/workflows
cp /path/to/vivid/dev/templates/vivid-ide-release.yml .github/workflows/release.yml
git add .github && git commit -m "Add release workflow" && git push
```

### When to Execute:
- When the C API is considered stable (current version: 1)
- When ready for public IDE releases
- After testing the creation script in a local environment

### What stays in vivid repo:
- vivid-core (runtime, operators, chain)
- C API (`vivid_c.h`, libvivid)
- CLI (`./vivid` for standalone/headless use)
- WebSocket API (for MCP, external tools)

### What moves to vivid-ide repo:
- Tauri app
- egui node graph
- Inspector panel
- Monaco editor integration
- Terminal/PTY

### Benefits after separation:
- Keeps Vivid "pure" — focused on runtime/operators/chain
- Independent release cycles
- Smaller clone size for framework-only users
- IDE can iterate faster without touching core
- Other IDEs/tools can link against libvivid

---

## Runtime Distribution Strategy

### Distribution Philosophy

**Key Decision: IDE bundles everything, no download logic**

Unlike VS Code extensions that download language servers on first run, the Vivid IDE bundles the complete runtime. This simplifies:
- Installation (one download, works immediately)
- Offline usage (no network required after install)
- Version consistency (IDE and runtime always match)
- Security (no runtime downloads from external sources)

### Release Artifacts Structure

Each release produces **parallel artifacts** - users choose what fits their workflow:

#### 1. Runtime-Only Downloads (existing, unchanged)
For CLI users, VS Code extension users, and developers embedding vivid-core.

```
vivid-v0.2.0-macos-arm64.tar.gz
├── bin/
│   └── vivid                    # CLI binary
├── lib/
│   ├── libvivid-c.dylib        # C API shared library
│   └── libvivid-core.dylib     # Core runtime
├── include/
│   └── vivid/
│       └── vivid_c.h           # C API header
└── share/
    └── vivid/
        └── modules/            # Built-in modules

vivid-v0.2.0-macos-x64.tar.gz
vivid-v0.2.0-windows-x64.zip
vivid-v0.2.0-linux-x64.tar.gz
```

#### 2. IDE Bundles (new)
Self-contained app bundles with everything included.

```
# macOS
Vivid-IDE-v0.2.0-macos-arm64.dmg
Vivid-IDE-v0.2.0-macos-x64.dmg
└── Vivid IDE.app/
    └── Contents/
        ├── MacOS/
        │   └── vivid-ide        # Tauri binary (embeds libvivid-c)
        ├── Frameworks/
        │   └── libvivid-c.dylib # Bundled runtime
        └── Resources/
            └── modules/         # Built-in modules

# Windows
Vivid-IDE-v0.2.0-windows-x64.msi
Vivid-IDE-v0.2.0-windows-x64-portable.zip
└── Vivid IDE/
    ├── vivid-ide.exe
    ├── vivid-c.dll
    └── modules/

# Linux
Vivid-IDE-v0.2.0-linux-x64.AppImage
Vivid-IDE-v0.2.0-linux-x64.deb
```

### CI/CD Pipeline Changes

Add Tauri build jobs to `.github/workflows/release.yml`:

```yaml
jobs:
  # Existing job - unchanged
  build-runtime:
    strategy:
      matrix:
        include:
          - os: macos-14
            target: aarch64-apple-darwin
            artifact: vivid-macos-arm64
          - os: macos-13
            target: x86_64-apple-darwin
            artifact: vivid-macos-x64
          - os: windows-latest
            target: x86_64-pc-windows-msvc
            artifact: vivid-windows-x64
          - os: ubuntu-latest
            target: x86_64-unknown-linux-gnu
            artifact: vivid-linux-x64
    steps:
      - uses: actions/checkout@v4
      - name: Build vivid
        run: cmake -B build && cmake --build build --config Release
      - name: Package runtime
        run: ./scripts/package-runtime.sh ${{ matrix.artifact }}
      - uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.artifact }}
          path: dist/${{ matrix.artifact }}.*

  # New job - Tauri IDE builds
  build-ide:
    needs: build-runtime  # IDE embeds the runtime
    strategy:
      matrix:
        include:
          - os: macos-14
            target: aarch64-apple-darwin
            artifact: Vivid-IDE-macos-arm64
          - os: macos-13
            target: x86_64-apple-darwin
            artifact: Vivid-IDE-macos-x64
          - os: windows-latest
            target: x86_64-pc-windows-msvc
            artifact: Vivid-IDE-windows-x64
          - os: ubuntu-latest
            target: x86_64-unknown-linux-gnu
            artifact: Vivid-IDE-linux-x64
    steps:
      - uses: actions/checkout@v4
        with:
          repository: seethroughlab/vivid-ide
          submodules: recursive  # Pulls vivid as submodule

      - name: Download runtime artifact
        uses: actions/download-artifact@v4
        with:
          name: vivid-${{ matrix.target }}
          path: vivid-runtime/

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: 20

      - name: Setup Rust
        uses: dtolnay/rust-action@stable

      - name: Install Tauri dependencies (Linux)
        if: matrix.os == 'ubuntu-latest'
        run: |
          sudo apt-get update
          sudo apt-get install -y libwebkit2gtk-4.1-dev librsvg2-dev patchelf

      - name: Build Tauri app
        uses: tauri-apps/tauri-action@v0
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          VIVID_LIB_PATH: ${{ github.workspace }}/vivid-runtime/lib
        with:
          tagName: v__VERSION__
          releaseName: 'Vivid IDE v__VERSION__'
          releaseBody: 'See the assets for downloads.'
          releaseDraft: true
          prerelease: false

  # Combine into single release
  create-release:
    needs: [build-runtime, build-ide]
    runs-on: ubuntu-latest
    steps:
      - name: Download all artifacts
        uses: actions/download-artifact@v4
      - name: Create GitHub Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            vivid-*/*
            Vivid-IDE-*/*
          draft: true
```

### Submodule Approach for vivid-ide Repository

The `vivid-ide` repository uses `vivid` as a git submodule for building:

```
vivid-ide/
├── .gitmodules
├── vivid/                    # git submodule -> seethroughlab/vivid
│   ├── modules/vivid-core/
│   │   └── include/vivid/vivid_c.h
│   └── ...
├── src-tauri/
│   ├── Cargo.toml
│   ├── build.rs              # Finds libvivid-c from VIVID_LIB_PATH or builds
│   └── src/
├── crates/
│   ├── vivid-sys/            # FFI bindings (references vivid/include)
│   └── vivid/                # Safe wrapper
├── src/                      # TypeScript frontend
└── package.json
```

**.gitmodules:**
```ini
[submodule "vivid"]
    path = vivid
    url = https://github.com/seethroughlab/vivid.git
    branch = master
```

**Development workflow:**
```bash
# Clone with submodule
git clone --recursive https://github.com/seethroughlab/vivid-ide.git

# Or init submodule after clone
git clone https://github.com/seethroughlab/vivid-ide.git
cd vivid-ide
git submodule update --init --recursive

# Build vivid first (for local development)
cd vivid && cmake -B build && cmake --build build && cd ..

# Then build IDE
npm install && npm run tauri build
```

**build.rs library resolution:**
```rust
fn main() {
    // Priority 1: CI provides pre-built library
    if let Ok(lib_path) = std::env::var("VIVID_LIB_PATH") {
        println!("cargo:rustc-link-search=native={}", lib_path);
    }
    // Priority 2: Local vivid submodule build
    else if Path::new("vivid/build/lib").exists() {
        println!("cargo:rustc-link-search=native=vivid/build/lib");
    }
    // Priority 3: System-installed vivid
    else {
        println!("cargo:rustc-link-search=native=/usr/local/lib");
    }

    println!("cargo:rustc-link-lib=dylib=vivid-c");
}
```

### Installation Documentation

**README.md for vivid-ide:**

```markdown
# Vivid IDE

Visual creative coding IDE with integrated runtime, node-based chain visualizer,
and built-in terminal for Claude Code.

## Installation

### Option 1: Download IDE (Recommended)

Download the latest release for your platform:
- **macOS (Apple Silicon)**: `Vivid-IDE-vX.X.X-macos-arm64.dmg`
- **macOS (Intel)**: `Vivid-IDE-vX.X.X-macos-x64.dmg`
- **Windows**: `Vivid-IDE-vX.X.X-windows-x64.msi`
- **Linux**: `Vivid-IDE-vX.X.X-linux-x64.AppImage`

The IDE includes everything you need - just install and run.

### Option 2: Runtime Only (Advanced)

If you prefer VS Code or command-line workflow:

1. Download the runtime: `vivid-vX.X.X-<platform>.tar.gz`
2. Extract and add `bin/` to your PATH
3. Install the [VS Code extension](https://marketplace.visualstudio.com/...)
4. Run projects with `vivid <project-path>`

### Option 3: Build from Source

```bash
git clone --recursive https://github.com/seethroughlab/vivid-ide.git
cd vivid-ide

# Build the vivid runtime
cd vivid && cmake -B build && cmake --build build && cd ..

# Build the IDE
npm install
npm run tauri build
```

## Updating

The IDE checks for updates on launch. When a new version is available:
1. Download the new release
2. Replace the existing installation
3. Your projects and settings are preserved

## VS Code Extension Compatibility

The runtime-only download works with the existing VS Code extension. If you
prefer VS Code's editing experience but want Vivid's visual preview, this
is the right choice.
```

### Release Page Layout

GitHub Releases page organization:

```
v0.2.0

## Vivid IDE (Recommended)

Full IDE with visual chain editor, Monaco code editor, and integrated terminal.

| Platform | Download |
|----------|----------|
| macOS (Apple Silicon) | [Vivid-IDE-v0.2.0-macos-arm64.dmg](link) |
| macOS (Intel) | [Vivid-IDE-v0.2.0-macos-x64.dmg](link) |
| Windows | [Vivid-IDE-v0.2.0-windows-x64.msi](link) |
| Linux | [Vivid-IDE-v0.2.0-linux-x64.AppImage](link) |

## Runtime Only (Advanced)

CLI and libraries for VS Code users or those embedding vivid-core.

| Platform | Download |
|----------|----------|
| macOS (Apple Silicon) | [vivid-v0.2.0-macos-arm64.tar.gz](link) |
| macOS (Intel) | [vivid-v0.2.0-macos-x64.tar.gz](link) |
| Windows | [vivid-v0.2.0-windows-x64.zip](link) |
| Linux | [vivid-v0.2.0-linux-x64.tar.gz](link) |

## What's New

- [changelog entries]
```

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
- [RuntimeAPI Protocol](../docs/WEBSOCKET_API.md) - Full WebSocket API documentation
