# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2025-01-XX

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

[Unreleased]: https://github.com/seethroughlab/vivid/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/seethroughlab/vivid/releases/tag/v0.1.0
