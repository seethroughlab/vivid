# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.2] - 2026-01-03

### Changed

#### Repository Structure
- **Source Reorganization** - All source code now lives under `src/`:
  - `core/` → `src/core/`
  - `cli/` → `src/cli/`
  - `addons/` → `src/addons/`
- **Examples → Projects** - Renamed `examples/` to `projects/` to better reflect their purpose
- **Development Files** - Moved `plans/`, `tools/`, `scripts/` into `dev/` directory
- **Test Organization** - Moved `testing-fixtures/` to `tests/fixtures/`, `testing-checklists/` to `tests/checklists/`

#### Asset Management
- **Assets Folder Eliminated** - No more shared `assets/` folder at root:
  - Framework fonts and icons → `src/core/assets/`
  - Project-specific assets → `projects/<name>/assets/`
  - Test assets → `tests/assets/` with Git LFS tracking
- **Git LFS** - Large test files (videos, sample packs, meshes, HDRIs) now use Git LFS for faster clones

### Fixed
- **Hot Reload** - Fixed include path resolution after folder restructuring
- **Addon Discovery** - Fixed addon registry path to find addons in `src/addons/`
- **Particles Example** - Fixed Composite input API (string names instead of pointers)
- **Test Paths** - Updated all test references from `examples/` to `projects/`

## [0.1.1] - 2025-01-XX

### Added

#### Audio
- **Wavetable Synth** - Full-featured wavetable synthesizer with morphing, unison, and modulation
- **MultiSampler** - Kontakt-style multi-sample instrument with key zones, velocity layers, round-robin, and keyswitch groups
- **Synth Presets** - JSON preset save/load system for FMSynth (8 factory presets: EPiano, Bass, Bell, Brass, Organ, Pad, Pluck, Lead)
- **Audio Operator Visualizations** - Custom chain visualizer graphics for all synths and audio effects

#### 3D Rendering
- **Fog Effect** - Depth-based atmospheric fog post-processor with Linear, Exponential, and ExponentialSquared modes
- **PCF Soft Shadows** - Vogel disk sampling for smooth shadow edges on all light types
- **Frustum Culling** - Automatic culling of off-screen geometry with debug visualization
- **Debug Gizmos** - Wireframe visualizations for cameras (frustum) and lights (direction/cone)
- **Shadow Controls** - `receiveShadow` toggle for objects, `castShadow` toggle for lights, manual shadow update control

#### System
- **Custom Node Graph** - Replaced ImNodes with custom Sugiyama hierarchical layout, mini-map, and keyboard navigation
- **Claude-First Architecture** - MCP server (`vivid mcp`) for Claude Code integration with live parameter editing
- **Custom App Icons** - Bundled apps can have project-specific icons and window titles
- **Multi-Window Support** - Window spanning and multi-monitor configurations
- **String-Based Connections** - Operators now use string names for inputs/outputs (type-safe)

#### Addons
- **vivid-midi** - MIDI input/output, file playback, and controller mapping
- **vivid-serial** - Serial port communication and DMX output (Enttec devices)
- **vivid-network** - OSC, UDP, and WebSocket operators (moved from core)

### Changed

- **wgpu-native upgraded to v27.0.4.0** - Includes critical memory leak fix (PR #542)
- **Chain visualizer moved to core** - No longer requires addon dependencies
- **ImGui removed from core** - Addons use `drawVisualization()` override instead
- **Operator visualizations** - Network, serial, and MIDI operators now have custom chain visualizer graphics
- **Plan files organized** - Moved to `plans/` directory

### Fixed

- **Memory leak** - Fixed persistent ~1MB/10s leak via wgpu-native upgrade
- **Noise operator contrast** - Output was mostly grey, now full dynamic range
- **Aspect ratio** - Noise and Ramp generators respect aspect ratio
- **Audio-video recording** - Non-blocking GPU poll, audio tap instead of blocking export
- **Windows video export** - Snapshot save now works on Windows
- **Point light shadows** - Fixed cube map sampling issues with 6 separate textures
- **CI builds** - Ubuntu 24.04 compatibility, Raspberry Pi cross-compilation fixes

### Documentation

- MCP server documentation complete
- WebSocket API documentation added
- Updated README with current API patterns

## [0.1.0] - 2024-12-XX

### Added

- **Core Runtime**
  - WebGPU-based rendering engine with hot-reload support
  - Operator pattern for composable visual effects
  - Chain system for connecting operators
  - ImGui-based visualizer for debugging chains
  - Parameter system with runtime inspection

- **2D Effects Addon** (`vivid-effects-2d`)
  - 25+ texture operators: Noise, Blur, Bloom, Feedback, Composite, etc.
  - Particle systems: Particles, PointSprites, Plexus
  - Canvas for procedural drawing
  - Color manipulation: HSV, Brightness, Quantize, Dither

- **3D Rendering Addon** (`vivid-render3d`)
  - PBR materials with metallic/roughness workflow
  - CSG operations (union, subtract, intersect)
  - GPU instancing for thousands of objects
  - glTF model loading
  - Multiple light types (point, directional, spot)

- **Video Addon** (`vivid-video`)
  - HAP codec support for high-performance video
  - Platform video decoders (AVFoundation on macOS, Media Foundation on Windows)

- **Audio Addon** (`vivid-audio`)
  - FFT analysis with configurable bins
  - Beat detection
  - Band splitting (bass, mid, high)
  - Audio-reactive parameters

- **IO Addon** (`vivid-io`)
  - Image loading (PNG, JPEG, etc. via stb_image)
  - Font atlas generation

- **Examples**
  - Getting started tutorials
  - 2D effects demonstrations
  - Audio visualization examples
  - 3D rendering showcases

### Technical Details

- Built with CMake 3.20+
- C++17 required
- WebGPU via wgpu-native
- Cross-platform: macOS, Windows, Linux

[Unreleased]: https://github.com/seethroughlab/vivid/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/seethroughlab/vivid/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/seethroughlab/vivid/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/seethroughlab/vivid/releases/tag/v0.1.0
