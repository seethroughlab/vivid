# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Note**: This project is currently in **alpha**. APIs may change between releases.
> The first stable release will be `v0.1.0`.

## [Unreleased]

## [0.1.0-alpha.11] - 2026-01-06

### Added

#### Utility Examples for MCP Context
Three new examples demonstrating core API patterns for LLM discoverability:

- **input-handling** - Mouse position/buttons, keyboard input, modifier keys, drag patterns
- **window-control** - Window resize, fullscreen, vsync, time functions (dt, realDt, frame)
- **param-modulation** - Lambda bindings with `bind()`, LFO modulation, time-based animation

Each includes comprehensive `CLAUDE.md` documentation with code patterns and API reference.

### Changed

#### Unified CLI with CLI11
- All runtime options now handled by CLI11 argument parser
- `vivid --help` now documents all 14 runtime options (previously missing from help)
- Runtime options: `--snapshot`, `--snapshot-frame`, `--headless`, `--record`, `--width`, `--height`, `--fps`, `--window`, `--frames`, `--vsync`, `--fullscreen`, `--float`, `--span`, `--debug-mode`
- Subcommands retain full help: `vivid new --help`, `vivid bundle --help`, etc.
- Simplified main.cpp from ~250 lines to ~50 lines

### Fixed
- Snapshot mode now exits immediately on compile error (was hanging indefinitely)
- Snapshot mode now exits immediately on context error (e.g., missing chain file)
- Added 30-second timeout for snapshot mode if no frames captured
- Fixed smoke test path after examples restructure (`src/vivid-core/examples/`)

## [0.1.0-alpha.10] - 2026-01-06

### Added

#### API Method Signatures for LLM Discovery
- All 120 operators now include `.api()` metadata with method signatures
- Enables LLMs to discover setter methods, configuration options, and static functions
- Example: `FFT` shows `.setSize(int n)`, `MidiOut` shows `.noteOn(ch, note, velocity)`, etc.

#### Examples in Release Packages
- Release archives now include `examples/` directory with all example projects
- Module-specific examples included in `modules/*/examples/`
- Users can run examples immediately after downloading

### Fixed
- Fixed nlohmann JSON forward declaration causing build errors with newer compilers

## [0.1.0-alpha.9] - 2026-01-06

### Added

#### LLM Metadata for All Module Operators
- All 81 module operators now have rich metadata for LLM assistants
- New `REGISTER_OPERATOR_FULL_EX` macro for non-Texture operators with metadata builder
- Each operator includes:
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

### Changed
- MCP `get_operator` now returns enriched metadata for all 120 operators (core + modules)
- Simplified operator registration macros - removed `REGISTER_ADDON_*` variants

## [0.1.0-alpha.8] - 2026-01-06

### Changed

#### Rename "libraries" to "modules"
Renamed to avoid confusion with `lib/` (compiled .dylib files):
- `libs/` → `modules/`
- `~/.vivid/libs/` → `~/.vivid/modules/`
- `library.json` → `module.json`
- CLI: `vivid libs` → `vivid modules`
- Classes: `LibraryManager` → `ModuleManager`, `LibraryRegistry` → `ModuleRegistry`

#### Module ownership
- Moved network examples into `modules/vivid-network/examples/`
- Moved video examples into `modules/vivid-video/examples/`

## [0.1.0-alpha.7] - 2026-01-06

### Added
- `cpuPixels()` virtual method on `Operator` base class - returns `std::optional<io::ImageData>` for CPU-side pixel access
- `Webcam::cpuPixels()` implementation - enables ML inference without GPU readback

### Fixed
- SDK release package now includes library headers (`vivid-audio`, `vivid-video`, etc.)

## [0.1.0-alpha.6] - 2026-01-06

### Changed

#### Project Restructure
- Renamed `addons/` directory to `libs/` for clarity
- Renamed "addon" terminology to "library" throughout codebase
- `AddonManager` → `LibraryManager`
- All libraries now use `library.json` instead of `addon.json`

### Fixed
- Add missing `<algorithm>` include for `std::min`/`std::max` with initializer lists (GCC 14 compatibility)
- CI: Use self-hosted runners for macOS, Windows, and Raspberry Pi builds
- CI: Fix build parallelism on Pi to avoid OOM

## [0.1.0-alpha.5] - 2026-01-04

### Added

#### Modular Particle Force System
- New force stack API: `ps.addForce<T>()`, `ps.clearForces()`, `ps.getForce<T>()`
- 8 composable force types that work on both CPU and GPU:
  - `GravityForce` - Constant directional acceleration
  - `DragForce` - Velocity damping
  - `CurlNoiseForce` - Organic, divergence-free flow fields
  - `TurbulenceForce` - Random jitter/turbulence
  - `PointAttractorForce` - Point attraction/repulsion
  - `VortexForce` - Rotational force around an axis (new)
  - `WindForce` - Directional wind with turbulent gusts (new)
  - `VelocityFieldForce` - Procedural flow fields (CPU-only, new)
- Forces generate their own WGSL shader code for GPU compute simulation
- Dynamic shader composition - pipeline rebuilds when force stack changes
- 14 example projects demonstrating various force configurations

#### ParticleSystem Operator
- Unified particle system supporting 2D and 3D particles
- 3 simulation modes: CPU, GPU (WebGPU compute)
- 3 render modes: Circle (2D), Billboard (3D), Mesh (3D instanced)
- 8 emitter shapes: Point, Line, Ring, Disc, Rectangle, Sphere, Box, Cone
- Color modes: Solid, Gradient, Rainbow, Random
- Velocity-aligned mesh rendering for trail effects
- Built-in elongated cube mesh for curl noise visualizations

### Changed
- Force parameters moved from ParticleSystem to individual force classes
- GPU particle simulation uses dynamically generated shaders based on active forces

### Breaking Changes
- Removed legacy physics params from ParticleSystem:
  - `gravity`, `drag`, `turbulence`
  - `attractorPosition`, `attractorStrength`
  - `curlStrength`, `curlScale`, `curlSpeed`, `curlOctaves`
- Use force stack API instead:
  ```cpp
  // Old (removed):
  ps.gravity.set(0.0f, -9.8f, 0.0f);
  ps.curlStrength = 1.5f;

  // New:
  ps.addForce<GravityForce>().direction.set(0.0f, -9.8f, 0.0f);
  ps.addForce<CurlNoiseForce>().strength = 1.5f;
  ```

## [0.1.0-alpha.4] - 2026-01-03

### Added

#### Operator Metadata System
- Extended `OperatorMeta` struct with `limitations`, `related`, and `examples` vectors
- New `OperatorMetaBuilder` class with fluent API for registering operators with rich metadata
- `REGISTER_OPERATOR_FULL` and `REGISTER_ADDON_OPERATOR_FULL` macros for extended registration
- All 35+ core operators now have related operators documented
- Key operators (Particles, Feedback, FrameCache, etc.) include limitations and example paths

### Changed
- MCP `get_operator` tool now returns limitations, related operators, and examples from registry
- Removed hardcoded operator metadata maps from `mcp_server.cpp` - metadata now lives with operators
- Version string includes git hash for dev builds (e.g., `0.1.1-dev+abc123`)

## [0.1.0-alpha.3] - 2026-01-03

### Added

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

### Changed
- MCP server now exposes 15 tools (up from 8)
- All CLI commands accessible via MCP without interactive prompts (uses `-y` flag)

## [0.1.0-alpha.2] - 2026-01-03

### Changed
- Reorganized documentation for better LLM and human discoverability
- Moved test assets to external S3 storage (LFS budget exceeded)

## [0.1.0-alpha.1] - 2026-01-03

First alpha release of Vivid. This consolidates all previous development into a single
pre-release package for testing.

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

#### 2D Effects (`vivid-effects-2d`)
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

#### Network/MIDI/Serial Addons
- `vivid-midi` - MIDI input/output, file playback, and controller mapping
- `vivid-serial` - Serial port communication and DMX output (Enttec devices)
- `vivid-network` - OSC, UDP, and WebSocket operators

#### System
- Custom app icons - bundled apps can have project-specific icons and window titles
- Snapshot mode (`--snapshot`) for CI testing and thumbnail generation

### Technical Details

- Built with CMake 3.20+
- C++17 required
- WebGPU via wgpu-native v27.0.4.0
- Cross-platform: macOS, Windows, Linux

### Repository Structure

```
src/
  core/           Runtime engine + 2D effects
  cli/            Command-line interface
  addons/         Optional modular features (video, render3d, audio, etc.)
projects/         Curated example projects
tests/            Test suites and fixtures
docs/             Documentation
```

[Unreleased]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.11...HEAD
[0.1.0-alpha.11]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.10...v0.1.0-alpha.11
[0.1.0-alpha.10]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.9...v0.1.0-alpha.10
[0.1.0-alpha.9]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.8...v0.1.0-alpha.9
[0.1.0-alpha.8]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.7...v0.1.0-alpha.8
[0.1.0-alpha.7]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.6...v0.1.0-alpha.7
[0.1.0-alpha.6]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.5...v0.1.0-alpha.6
[0.1.0-alpha.5]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.4...v0.1.0-alpha.5
[0.1.0-alpha.4]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.3...v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.2...v0.1.0-alpha.3
[0.1.0-alpha.2]: https://github.com/seethroughlab/vivid/compare/v0.1.0-alpha.1...v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/seethroughlab/vivid/releases/tag/v0.1.0-alpha.1
