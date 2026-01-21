# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Note**: This project is currently in **alpha**. APIs may change between releases.
> The first stable release will be `v0.1.0`.

## [Unreleased]

## [0.1.0-alpha.8] - 2026-01-20

*Windows build fix*

### Fixed

- **Windows build failure** - Fixed MSVC compilation errors in vivid-audio module:
  - Added missing `#include <algorithm>` to `modulator.h` for `std::clamp`
  - Made `Voice` struct explicitly movable-only to fix `vector::resize` with `unique_ptr` member

## [0.1.0-alpha.7] - 2026-01-20

*Production bundle architecture redesign, WebView improvements*

### Added

- **WebView character input** - Added character input support for text editors and terminals in WebView:
  - New `characterInput()` method on Context returns Unicode codepoints from GLFW character callback
  - Characters forwarded to WebView via `InputEvent` for proper text insertion
  - Supports full Unicode including emoji and non-ASCII characters
  - Enables Monaco Editor and xterm.js integration (Phase 1 of embedded editor/terminal plan)

- **WebView focus management** - Added keyboard focus tracking for multiple WebViews:
  - `hasFocus()`, `requestFocus()`, `releaseFocus()` methods on WebView operator
  - Click on a WebView automatically gives it keyboard focus
  - Only the focused WebView receives keyboard and character input
  - Static `focusedWebView()` returns the currently focused WebView (Phase 2 of embedded editor/terminal plan)

### Changed

#### Production Bundle Architecture Redesign
Production bundles now use a dedicated runtime instead of the development `vivid` executable with stubs disabled:

- **New `Runtime` class** (`modules/vivid-core/include/vivid/runtime.h`) - Minimal WebGPU runtime loop with no HotReload, MCP, or Visualizer dependencies
- **New `main_production.cpp`** (`src/cli/main_production.cpp`) - Clean entry point that calls `vivid_setup`/`vivid_update` directly
- **New `vivid-production` build target** - Dedicated executable for bundled apps
- **Removed stub files** - Deleted `hot_reload_stub.cpp` and `runtime_api_stub.cpp` (no longer needed)
- **Simplified bundle command** - Builds `vivid-production` target instead of configuring `vivid` with production flags

Benefits:
- Production bundles contain no dev tool code (not even disabled stubs)
- Cleaner separation between development and production code paths
- Bundled apps start immediately (no "Loading chain..." message)
- No MCP server or visualizer symbols in production binaries

### Fixed

- **WebView mouse interaction** - Fixed slider dragging and mouse events not working in WebView operator:
  - External JS/CSS files now load correctly (changed to `loadFileURL:allowingReadAccessToURL:` for file:// URLs)
  - Mouse coordinates now properly scaled by `backingScaleFactor` for Retina displays
  - Fixed JavaScript syntax errors caused by `//` comments in single-line injected scripts
  - Added page-ready check to prevent events before DOM is loaded
  - Coordinates now clamped to viewport bounds

- **Production bundle asset loading** - Fixed videos and other assets not loading in bundled apps. Production runtime now correctly sets project directory via `AssetLoader::instance().setProjectDir()`.

## [0.1.0-alpha.6] - 2026-01-17

*Texture sharing, new operators, per-voice modulation, MCP improvements, improved Noise defaults*

### Added

- **Per-voice modulation system** - Attach modulators (LFO, ADSR) to synths for per-voice parameter control:
  - `LFO` modulator - Periodic waveforms (Sine, Triangle, Square, Saw, SampleHold) with tempo sync
  - `ADSRMod` modulator - Attack-Decay-Sustain-Release envelope
  - Per-voice vs global modes: `perVoice = true` for independent modulation per note
  - Modulation routing: `synth.modulate(mod, "param", depth, bipolar)`
  - New example: `modular-modulation` demonstrating per-voice filter envelopes and LFOs
- **vivid-texshare module** - Texture sharing between applications via Syphon (macOS) and Spout (Windows). New operators:
  - `TextureShareOut` - Publish textures to other apps (VJ software, media servers)
  - `TextureShareIn` - Receive textures from other apps
  - Syphon framework auto-downloaded if not installed; Windows Spout support is stub for now
- **`Fit` operator** - Resolution fitting with letterbox, pillarbox, fill, and stretch modes. Configurable alignment and background color.
- **`Sweep` operator** - Extrude 2D profiles along 3D parametric paths. Built-in paths (Line, Helix, Circle, Arc) and profiles (Circle, Square, Star, Triangle). Supports twist, scale taper, and UV mapping.
- **`get_example` MCP tool** - Returns working code snippets for operators from RECIPES.md and example projects. Call before writing code to see correct API patterns (e.g., `get_example("Sequencer")` shows `setPattern(0x1111)` bitmask syntax).
- **Examples vs Showcase documentation** - Module READMEs now document the distinction: `examples/` contains minimal (~50-100 line) API demonstrations for quick reference, while `showcase/` contains rich (200+ line) creative applications for inspiration. Added showcase/IDEAS.md files with future project proposals for modules without existing showcases.
- **Noise `colorNoise` parameter** - When true, generates RGB noise with 3 independent channels instead of grayscale. Each color channel samples from a spatially offset noise field.
- **Noise `centerOrigin` parameter** - When true (now default), scaling happens from the center of the texture instead of the corner. Makes audio-reactive scale modulation pulse outward naturally.
- **New recipes in RECIPES.md** - Four non-noise recipes added at the top of the documentation:
  - **Pulsing Shapes** - Audio-reactive geometry with Shape operator
  - **Geometric Flash** - Beat-synced shapes with Flash operator
  - **Gradient Pulse** - Color gradients driven by audio (no procedural noise)
  - **Particle Burst** - Beat-synced particle system
- **Lesson 11: shapes-reactive** - New getting-started example demonstrating audio-reactive visuals without noise, using Shape, Gradient, and Composite operators
- **Alternative Approaches documentation** - Added guidance to AGENTS.md files showing alternatives to noise (Shape, Ramp, Particles, Flash, Feedback)

### Changed

- **`search_docs` uses OR logic** - Doc search now matches ANY query word (was ALL words required). Results ranked by relevance score. Query "audio mixer connect" now returns useful results instead of "No matches found".
- **`get_operator` no longer guesses methods** - Removed ~195 lines of hardcoded method heuristics that caused false positives. Claude now reads the actual header files via `headerPath` to discover the real API.
- **Noise default behavior** - `centerOrigin` now defaults to `true`, so scale modulation pulses from center. Set to `false` for legacy corner-origin behavior.
- **Documentation organization** - RECIPES.md now categorizes visual effects into "No Noise" and "With Noise" sections to highlight alternatives

### Fixed

- **`validate_chain` false positives** - Now parses compile errors into structured JSON with file, line, column, severity, and message. Returns `valid: false` when errors are detected, even if exit code is 0. Includes suggestions for fixing errors.

## [0.1.0-alpha.5] - 2026-01-13

*Lookup operator, audio-thread triggers, MCP debugging tools*

### Added

- **CpuPixels output kind** - New `OutputKind::CpuPixels` for operators that output CPU pixel buffers instead of GPU textures. Chain visualizer automatically uploads these to a scratch texture for preview rendering.
- **MCP debugging tools** - New tools for visual debugging and parameter control:
  - `set_param` - Set operator parameters immediately without pending queue
  - `advance_frames` - Advance simulation by N frames for animation testing
  - `orbit_camera` - Position camera around a target point (sets center, distance, azimuth, elevation)
  - `capture_at_frame` - Advance to specific frame and capture snapshot
- **Visual validation workflow** - Updated documentation and project templates to guide Claude through the recommended workflow: run project, capture frames, monitor slider changes, verify compile status
- **Audio-thread trigger pattern** - Sequencer and Euclidean now inherit from `AudioOperator` and run on the audio thread for sample-accurate timing. Use `setTriggerSource("clock")` instead of manual `advance()` calls.
  ```cpp
  // New pattern (sample-accurate, runs on audio thread)
  kick_seq.setTriggerSource("clock");
  kick.setTriggerSource("kick_seq");

  // Old pattern (still works, ~16ms jitter)
  if (clock.triggered()) {
      kick_seq.advance();
      if (kick_seq.triggered()) kick.trigger();
  }
  ```
- **Clock swing enable/disable** - `clock.setSwingEnabled(bool)` to programmatically enable/disable swing. Disabled by default.
- **Lookup operator** - Color lookup table effect for colorizing grayscale textures using a gradient/LUT. Uses input luminance (or R/G/B channel) to sample from a horizontal gradient texture. Perfect for heat maps, false color, and artistic colorization.
- **Transitive module dependencies** - Hot-reload compiler now resolves transitive dependencies declared in `module.json`. For example, vivid-midi depending on vivid-audio now automatically includes vivid-audio's include paths.
- **vivid-midi module.json** - Added module metadata with vivid-audio dependency declaration

### Changed

- **vivid-opencv compatibility** - Enables vivid-opencv module to use simplified CPU-only architecture without requiring wgpu linking
- **Example chains updated** - drum-machine, drum-synthesis, and euclidean-rhythms examples now use audio-thread trigger pattern for better timing accuracy
- **Lesson 02** - Rewrote operator pipeline tutorial to use Noise → Blur → Lookup chain, demonstrating grayscale colorization with the new Lookup operator

### Fixed

- **Missing chain.cpp error reporting** - When a project directory has no chain.cpp, MCP now correctly reports `compileStatus.success: false` with "Chain file not found" message instead of misleading success with 0 operators
- **Clock swing triggers lost** - Fixed swing triggers being permanently skipped when swing amount changed mid-beat. Swing delay is now captured at odd beat time and won't change until the trigger fires.
- **Visual triggers missing** - Fixed main thread (~60fps) missing audio triggers due to timing mismatch with audio blocks (~5ms). Sequencer and Euclidean now use dual-flag pattern: one flag for audio thread (cleared each block), one for main thread (accumulated until read).
- **Blur radius=0 black output** - Fixed Gaussian blur producing black output when radius was 0 (division by zero in shader). Now passes through input unchanged when radius < 0.5.
- **Documentation API errors** - Fixed incorrect API references in docs/CHAIN-API.md and getting-started lessons
- **midi-input example** - Fixed outdated API usage
- **Build: Linux OpenCV** - Fixed opencv-mobile header detection for Linux x64 and ARM64
- **Build: Broken symlink** - Fixed absolute symlink to HDR asset that only worked locally
- **CI: Self-hosted runner cleanup** - Added cross-platform build directory clean and file permission fixes

## [0.1.0-alpha.4] - 2026-01-11

*Glitch effects suite, display scaling modes, video audio sync*

### Added

#### Glitch Effects Suite
Complete tempo-synced audio manipulation system inspired by Ableton's Beat Repeat and Ned Rush's Lucky 16 MaxForLive pack:

- **BeatRepeat** - Captures and loops audio slices rhythmically with configurable repeat count and decay
- **Reverse** - Real-time backwards playback of captured audio with crossfade
- **Stutter** - Rapid repeats with volume envelopes (Flat, Decay, Build, Triangle) for build-ups and breakdowns
- **Scratch** - DJ-style varispeed playback with motion types (Forward, Backward, BackForth, Random)
- **TapeStop** - Turntable slowdown/speedup effect with exponential curves
- **FrequencyShift** - Bode frequency shifter using Hilbert transform for metallic, inharmonic textures
- **Stretch** - Granular time-stretch without pitch change using overlapping Hann-windowed grains
- **Glitch** - Meta-effect combining all 6 effects with per-effect probability controls

**Core Infrastructure:**
- **CircularAudioBuffer** - Header-only circular buffer with interpolated read/write for all glitch effects
- **rate_utils.h** - Tempo sync utilities: `divisionToSamples()`, `divisionToHz()`, `divisionToSeconds()`
- **glitch-effects example** - Interactive demo of all 8 glitch operators with mouse control

#### Display Scaling Modes
New display mode system for controlling how the final output texture is rendered to the window:

- **DisplayMode enum** with 5 modes:
  - `Stretch` - Fill window, ignore aspect ratio (may distort)
  - `Fit` - Maintain aspect ratio, letterbox/pillarbox as needed (default)
  - `Fill` - Maintain aspect ratio, fill window, crop edges
  - `FillHorizontal` - Maintain aspect ratio, fill width, may crop top/bottom
  - `FillVertical` - Maintain aspect ratio, fill height, may crop left/right

- **ChainConfig.displayMode** - Set initial display mode:
  ```cpp
  VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
      .windowWidth = 1280,
      .windowHeight = 720,
      .displayMode = vivid::DisplayMode::Fill
  }))
  ```

- **Runtime API** - Change display mode dynamically:
  ```cpp
  ctx.displayMode(DisplayMode::Fit);
  DisplayMode current = ctx.displayMode();
  ```

- **Screen inspector panel** - Display Mode dropdown in chain visualizer when Screen node is selected
- **display-modes example** - Interactive demo showing all 5 modes with keyboard controls

#### Video Audio Sync
PTS-based A/V synchronization when routing video audio through the effects chain:

- **Timestamp tracking** - Audio samples tagged with Presentation Timestamps (PTS) for sync calculation
- **Drift detection** - Monitors video-audio offset with configurable thresholds (100ms tolerance, 500ms critical)
- **Automatic correction** - Skips audio when behind, inserts silence when ahead
- **Seek support** - Resyncs audio buffer after seeking
- **Shutdown safety** - Atomic flags prevent race conditions on cleanup

**Platform support:**
| Platform | Decoder | Status |
|----------|---------|--------|
| macOS | AVFPlaybackDecoder | Full implementation |
| Windows | DShowDecoder | Full implementation |
| Windows | MFDecoder | Full implementation |
| Linux | FFmpegDecoder | Infrastructure stub |

#### MCP & Documentation
- **MCP: Module documentation search** - `search_docs` now searches module READMEs and example CLAUDE.md files
- **Docs: Video audio modes** - Documentation for internal audio vs chain audio routing modes

### Changed

- Default display mode changed from implicit stretch to explicit `Fit` mode
- Blit shader now uses uniform buffer for aspect-ratio-aware UV calculations
- **Release: Self-contained modules** - Built-in modules now include `include/`, `README.md`, and `examples/` in releases
- **CI: Build parallelism limits** - Self-hosted runners now limit parallel jobs to prevent OOM

### Fixed

- **Hot reload** - Fixed hot reload not triggering in certain conditions
- **CI: darwin-arm64 builds** - Added `WGPU_FORCE_ARCH=arm64` for correct architecture detection on Apple Silicon
- **CI: cmake arm64→aarch64** - Convert arm64 to aarch64 for wgpu-native downloads
- **Docs: Windows OpenCV known issue** - Documented MSVC STL ABI incompatibility
- **CI: Linux x64 OOM** - Added `--parallel 4` limit for Linux x64 release builds

## [0.1.0-alpha.3] - 2026-01-10

*Self-describing operators, event system, Copy operator*

### Added

#### Event Operator System
New `OutputKind::Event` type and input operators for exposing keyboard, mouse, and window events to user chains:

- **KeyboardIn** - Keyboard input events with polling and convenience accessors
  - `events()` returns vector of discrete key events per frame
  - `keyPressed(key)`, `keyHeld(key)`, `keyReleased(key)` convenience methods
  - Modifier state: `shiftHeld()`, `ctrlHeld()`, `altHeld()`, `superHeld()`
- **MouseIn** - Mouse input events with position and button state
  - `events()` for discrete mouse events (press, release, move, scroll)
  - `position()`, `positionNorm()`, `delta()`, `scroll()` accessors
  - `buttonPressed(btn)`, `buttonHeld(btn)`, `buttonReleased(btn)` methods
- **WindowEvents** - Window lifecycle events
  - `resized()`, `width()`, `height()`, `aspect()` accessors

**Event Source Visualization:**
- `setEventSource(op)` / `setEventSource("name")` for connecting event flow in chain visualizer
- Green dashed lines show event connections (similar to cyan trigger lines)

#### Copy Operator
TouchDesigner-style replication operator that creates multiple copies of an input texture with per-copy transforms:
- **Linear mode**: Trail effects with offset, rotation step, and scale step
- **Radial mode**: Circular arrays like clock faces or flower petals
- **Grid mode**: Uniform grids with configurable columns and spacing
- Opacity falloff for fade trails
- Dynamic shader generation with pipeline caching (1-16 copies)
- **copy-patterns example** - Demonstrates all three Copy modes with animated parameters

#### Value Operators & Inspector
- **Value operators** - LFO, Ramp, and other value-producing operators for parameter modulation
- **New inspector widgets** - Improved parameter editing in the inspector panel
- **`withExamples()` OperatorMeta** - Operators can now declare example project paths in metadata

#### Inspector & Render3D
- **Inspector panel scrolling** - Parameter panels with many sliders now scroll when content exceeds visible area
- **Dynamic window resize for Render3D** - Render target follows window size unless `setResolution()` is explicitly called

### Changed

#### Self-Describing Operator Registration
All 120 operators now define metadata via a static `describe()` method in their header files. This consolidates operator information in one place instead of spreading it across registration macros and separate init blocks.

**Before (multiple files):**
```cpp
// In operator_registrations.cpp
REGISTER_OPERATOR(Displace, "Effects", "Texture displacement", true);
// Elsewhere: separate metadata setup
```

**After (single location in header):**
```cpp
class Displace : public TextureOperator {
public:
    static OperatorDescriptor describe() {
        return OperatorDescriptor("Displace", "Effects", "Texture displacement")
            .requireInput()
            .withAliases({"Warp", "Distort"})
            .withUsage("auto& d = chain.add<Displace>(\"d\");...");
    }
    // ... rest of class
};

// In .cpp: just REGISTER(Displace);
```

Benefits:
- **Single source of truth** - Everything about an operator in one place
- **IDE navigation** - Jump from registration to full class definition
- **Self-documenting** - Reading the class tells you everything
- **Less boilerplate** - `REGISTER(Displace)` vs multi-argument macro + separate metadata

#### Other Changes
- **Distortion example** - Restored 4-quadrant layout with video + grid overlay
- **Deprecated legacy light methods** - `setLightDirection()` and `setLightColor()` are now deprecated; use `setParam()` or `setLightInput()` instead
- **3d-basics example** - Updated to use `DirectionalLight` operator
- **Preview renderers** - SceneComposer and MeshOperator preview thumbnails now use `setParam()` API

### Fixed

- **particles example** - Updated to use new `Param<>` property assignment syntax
- **Chain visualizer connections** - Fixed stale connections persisting after hot reloads
- **Chain visualizer auto-layout** - Fixed layout not resetting after chain hot-reload
- **Video decoder crash on macOS 15+** - Fixed memory management bug in async track loading
- **GPU encoder sharing** - Fixed SimpleTextureEffect and SimpleGeneratorEffect to use shared GPU encoder
- **Render3D texture resize** - Fixed depth/color buffer size mismatch when following window size

## [0.1.0-alpha.2] - 2026-01-07

*ARM64 support, Param<> properties, MCP reliability*

### Added

#### API Method Signatures for LLM Discovery
- All 120 operators now include `.api()` metadata with method signatures
- Enables LLMs to discover setter methods, configuration options, and static functions
- Example: `FFT` shows `.setSize(int n)`, `MidiOut` shows `.noteOn(ch, note, velocity)`, etc.

#### Examples in Release Packages
- Release archives now include `examples/` directory with all example projects
- Module-specific examples included in `modules/*/examples/`
- Users can run examples immediately after downloading

#### Raspberry Pi ARM64 Support
- Added Raspberry Pi ARM64 to release builds

#### Utility Examples for MCP Context
Three new examples demonstrating core API patterns for LLM discoverability:
- **input-handling** - Mouse position/buttons, keyboard input, modifier keys, drag patterns
- **window-control** - Window resize, fullscreen, vsync, time functions (dt, realDt, frame)
- **param-modulation** - Lambda bindings with `bind()`, LFO modulation, time-based animation

Each includes comprehensive `CLAUDE.md` documentation with code patterns and API reference.

#### Unified CLI with CLI11
- All runtime options now handled by CLI11 argument parser
- `vivid --help` now documents all 14 runtime options
- Runtime options: `--snapshot`, `--snapshot-frame`, `--headless`, `--record`, `--width`, `--height`, `--fps`, `--window`, `--frames`, `--vsync`, `--fullscreen`, `--float`, `--span`, `--debug-mode`
- Simplified main.cpp from ~250 lines to ~50 lines

#### webcam-displace Example
- Demonstrates video operators interacting with generator operators (Webcam → Noise → Displace pipeline)

#### LLM Metadata for Module Operators
- Added `@see` tags and class-level documentation to audio, video, render3d, network, serial, and MIDI operators for better MCP discoverability
- Dynamic MCP docs discovery - All docs/*.md files now exposed as MCP resources automatically

### Changed

#### Particles Operator Uses Param<> Properties
Replaced 20+ setter methods with public `Param<>` properties for consistency with other operators:
- Before: `particles.emitter(EmitterShape::Disc); particles.position(0.5f, 0.5f);`
- After: `particles.emitterShape = EmitterShape::Disc; particles.position.set(0.5f, 0.5f);`
- Action methods preserved: `burst()`, `seed()`, `setTexture()`, `setSpin()`

#### MCP Connection Reliability
- Added heartbeat mechanism, better error handling, and proper state synchronization on connect

#### Module Operator Registration
- All 80+ module operators now register with their module name for better MCP discoverability

#### Simplified MCP Server
- Reduced from 19 to 15 tools by removing redundant endpoints
- Dynamic module discovery for `list_examples` and `search_docs`
- Simplified operator registry - Removed verbose `REGISTER_OPERATOR_FULL` macros

### Removed

- **LLM-REFERENCE.md** - Static operator reference (845 lines) removed; MCP tools provide dynamic, always-accurate operator information
- Removed `vivid://docs/reference` MCP resource endpoint

### Fixed

- **Critical: Hot reload include path** - Fixed `hot_reload.cpp` to find headers at `modules/vivid-core/include` instead of old path
- **Critical: OperatorRegistry crash on exit** - Changed to leaky singleton to prevent static destruction order crash
- **MCP server tools** - Fixed `list_operators`, `get_operator`, `search_docs`, and `list_modules` returning empty/incorrect results
- **Documentation examples** - Fixed incorrect fluent-style syntax in RECIPES.md and CHAIN-API.md
- **Showcase examples** - Updated flow-field and audio-visualizer to use correct property assignment syntax
- **Release build: darwin-arm64** - Scoped `-Wsuggest-override` to C++ only via generator expression
- **Release build: win32-x64** - Fixed PowerShell packaging script failing when copying duplicate module headers
- **nlohmann JSON** - Fixed forward declaration causing build errors with newer compilers
- Fixed Doxyfile paths after project restructure
- Fixed operator registration example paths in metadata
- Snapshot mode now exits immediately on compile error or context error
- Added 30-second timeout for snapshot mode if no frames captured
- Use `powershell` instead of `pwsh` on Windows
- Add missing `<mutex>` include for Linux/Windows builds

## [0.1.0-alpha.1] - 2026-01-06

*Initial release*

### Added

#### Core Runtime
- WebGPU-based rendering engine with hot-reload support
- Operator pattern for composable visual effects
- Chain system for connecting operators
- Parameter system with runtime inspection
- Custom node graph with Sugiyama hierarchical layout, mini-map, and keyboard navigation
- MCP server (`vivid mcp`) for Claude Code integration with live parameter editing
- Multi-window support with window spanning and multi-monitor configurations
- String-based connections - operators use string names for inputs/outputs (type-safe)

#### MCP Server
- `create_project` tool - Create new projects with template and addon selection
- `run_project` / `stop_project` tools - Start and stop Vivid projects from Claude Code
- `capture_snapshot` tool - Render frame(s) to PNG for testing and verification
- `validate_chain` tool - Check if chain.cpp compiles without running
- `bundle_project` tool - Package projects as standalone applications
- `list_addons` tool - List installed addons

#### VS Code Extension
- Auto-configure `~/.claude.json` with Vivid MCP server on extension activation
- "Vivid: Configure Claude Code Integration" command
- Warning notifications when MCP config is missing or broken
- Validation by running `vivid mcp --help` to verify binary works

#### Operator Metadata System
- Extended `OperatorMeta` struct with `limitations`, `related`, and `examples` vectors
- New `OperatorMetaBuilder` class with fluent API for registering operators with rich metadata
- All 35+ core operators now have related operators documented
- Key operators include limitations and example paths

#### Modular Particle Force System
New force stack API: `ps.addForce<T>()`, `ps.clearForces()`, `ps.getForce<T>()`

8 composable force types that work on both CPU and GPU:
- `GravityForce` - Constant directional acceleration
- `DragForce` - Velocity damping
- `CurlNoiseForce` - Organic, divergence-free flow fields
- `TurbulenceForce` - Random jitter/turbulence
- `PointAttractorForce` - Point attraction/repulsion
- `VortexForce` - Rotational force around an axis
- `WindForce` - Directional wind with turbulent gusts
- `VelocityFieldForce` - Procedural flow fields (CPU-only)

Forces generate their own WGSL shader code for GPU compute simulation with dynamic shader composition.

#### ParticleSystem Operator
- Unified particle system supporting 2D and 3D particles
- 3 simulation modes: CPU, GPU (WebGPU compute)
- 3 render modes: Circle (2D), Billboard (3D), Mesh (3D instanced)
- 8 emitter shapes: Point, Line, Ring, Disc, Rectangle, Sphere, Box, Cone
- Color modes: Solid, Gradient, Rainbow, Random
- Velocity-aligned mesh rendering for trail effects
- 14 example projects demonstrating various force configurations

#### cpuPixels API
- `cpuPixels()` virtual method on `Operator` base class - returns `std::optional<io::ImageData>` for CPU-side pixel access
- `Webcam::cpuPixels()` implementation - enables ML inference without GPU readback

#### LLM Metadata for Module Operators
All 81 module operators now have rich metadata for LLM assistants including:
- `related` - Links to related operators for discovery
- `limitations` - Constraints and caveats
- `examples` - Paths to example projects

**Module coverage:**
| Module | Operators |
|--------|-----------|
| vivid-render3d | 17 |
| vivid-audio | 50 |
| vivid-video | 3 |
| vivid-network | 5 |
| vivid-serial | 3 |
| vivid-midi | 3 |

#### 2D Effects
- 25+ texture operators: Noise, Blur, Bloom, Feedback, Composite, etc.
- Particle systems: Particles, PointSprites, Plexus
- Canvas for procedural drawing
- Color manipulation: HSV, Brightness, Quantize, Dither

#### 3D Rendering (`vivid-render3d`)
- PBR materials with metallic/roughness workflow
- CSG operations (union, subtract, intersect)
- GPU instancing for thousands of objects
- glTF model loading
- Multiple light types (point, directional, spot)
- Fog effect - depth-based atmospheric fog with Linear, Exponential, and ExponentialSquared modes
- PCF soft shadows - Vogel disk sampling for smooth shadow edges
- Frustum culling with debug visualization
- Debug gizmos for cameras (frustum) and lights (direction/cone)
- Shadow controls: `receiveShadow`/`castShadow` toggles, manual shadow update control

#### Video (`vivid-video`)
- HAP codec support for high-performance video
- Platform video decoders (AVFoundation on macOS, Media Foundation on Windows)

#### Audio (`vivid-audio`)
- FFT analysis with configurable bins
- Beat detection and band splitting (bass, mid, high)
- Wavetable synth with morphing, unison, and modulation
- MultiSampler - Kontakt-style multi-sample instrument with key zones, velocity layers, round-robin
- Synth presets - JSON save/load system for FMSynth (8 factory presets)
- Custom chain visualizer graphics for all synths and audio effects

#### IO (`vivid-io`)
- Image loading (PNG, JPEG, etc. via stb_image)
- Font atlas generation

#### Network/MIDI/Serial Modules
- `vivid-midi` - MIDI input/output, file playback, and controller mapping
- `vivid-serial` - Serial port communication and DMX output (Enttec devices)
- `vivid-network` - OSC, UDP, and WebSocket operators

#### System
- Custom app icons - bundled apps can have project-specific icons and window titles
- Snapshot mode (`--snapshot`) for CI testing and thumbnail generation

### Changed

#### Rename "libraries" to "modules"
Renamed to avoid confusion with `lib/` (compiled .dylib files):
- `libs/` → `modules/`
- `~/.vivid/libs/` → `~/.vivid/modules/`
- `library.json` → `module.json`
- CLI: `vivid libs` → `vivid modules`
- Classes: `LibraryManager` → `ModuleManager`

#### Project Restructure
- Renamed `addons/` directory to `libs/` for clarity
- Renamed "addon" terminology to "library" throughout codebase
- All libraries now use `library.json` instead of `addon.json`

#### MCP Updates
- MCP server now exposes 15 tools
- MCP `get_operator` tool now returns limitations, related operators, and examples from registry
- MCP `get_operator` now returns enriched metadata for all 120 operators
- Removed hardcoded operator metadata maps from `mcp_server.cpp`
- Version string includes git hash for dev builds

### Breaking Changes

#### Particle Force API
Removed legacy physics params from ParticleSystem:
- `gravity`, `drag`, `turbulence`
- `attractorPosition`, `attractorStrength`
- `curlStrength`, `curlScale`, `curlSpeed`, `curlOctaves`

Use force stack API instead:
```cpp
// Old (removed):
ps.gravity.set(0.0f, -9.8f, 0.0f);
ps.curlStrength = 1.5f;

// New:
ps.addForce<GravityForce>().direction.set(0.0f, -9.8f, 0.0f);
ps.addForce<CurlNoiseForce>().strength = 1.5f;
```

### Fixed

- Restored OPERATOR-API.md that was accidentally deleted
- Test assets moved to external S3 storage
- Prevent Windows min/max macro conflicts
- Add missing `<cstring>` include for Linux build
- Add missing `<algorithm>` include for `std::min`/`std::max` with initializer lists
- SDK release package now includes library headers
- CI: Use self-hosted runners for macOS, Windows, and Raspberry Pi builds
- CI: Fix build parallelism on Pi to avoid OOM

### Technical Details

- Built with CMake 3.20+
- C++17 required
- WebGPU via wgpu-native v27.0.4.0
- Cross-platform: macOS, Windows, Linux

### Repository Structure

```
src/
  vivid-core/       Runtime engine + 2D effects
  cli/              Command-line interface
modules/            Optional modular features (video, render3d, audio, etc.)
projects/           Curated example projects
tests/              Test suites and fixtures
docs/               Documentation
```

[Unreleased]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.7...HEAD
[0.1.0-alpha.7]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.6...v0.1.0-alpha.7
[0.1.0-alpha.6]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.5...v0.1.0-alpha.6
[0.1.0-alpha.5]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.4...v0.1.0-alpha.5
[0.1.0-alpha.4]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.3...v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.2...v0.1.0-alpha.3
[0.1.0-alpha.2]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.1...v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/seethroughlab/vivid/releases/tag/v0.1.0-alpha.1
