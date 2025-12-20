# Vivid Roadmap

A **minimal-core, addon-first creative coding framework** built on WebGPU. Plain C++ that language models can read and write.

See [PHILOSOPHY.md](PHILOSOPHY.md) for design goals and principles.

---

## Previous Attempts: V1 and V2

Understanding why V1 and V2 failed is essential context for V3's design decisions.

### Vivid V1 (2023-2024)

- ~47,000 lines of C++, wgpu-native backend
- 29 operators, fluent API, hot-reload, VS Code extension

**Why it failed:**
1. **Scope creep** - Started adding 3D, skeletal animation, physics—none finished
2. **Skeletal animation was fundamentally broken** - Months wasted on bones/skinning that never worked
3. **Platform fragmentation** - macOS worked, Windows had driver issues, Linux untested

### Vivid V2 (2024)

- ~9,500 lines, Diligent Engine backend (Vulkan/D3D12/Metal abstraction)
- 18 operators, PBR rendering, GLTF loader

**Why it failed:**
1. **Shadow mapping never worked** - Weeks debugging cascades that were either all-shadow or all-lit
2. **Diligent was over-engineered** - Abstraction layer added complexity without benefit
3. **Dependency hell** - Transitive dependencies complicated builds

### Lessons Applied to V3

1. **Stay minimal** - Core is ~600 lines. Resist adding features until basics are solid.
2. **No skeletal animation** - 3D models are static meshes only.
3. **Simple shadows only** - Point/spot shadow maps, no cascades.
4. **WebGPU via wgpu-native** - V1's approach was right, we just added too much on top.
5. **Operators are addons, not plugins** - No registration macros. Explicit `chain.add<T>()`.
6. **Test on all platforms from day one** - CI runs on macOS, Windows, Linux.
7. **State preservation is non-negotiable** - Hot-reload without state preservation is useless.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ User Code (chain.cpp)                                    │
│                                                          │
│  void setup(Context& ctx) { ... }                        │
│  void update(Context& ctx) { ... }                       │
│  VIVID_CHAIN(setup, update)                              │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ Addons (vivid-effects, vivid-render3d, vivid-audio)      │
│                                                          │
│  All written against webgpu.h                            │
│  Operators provide fluent API for chain composition      │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ Vivid Core (~600 lines)                                  │
│                                                          │
│  - GLFW window + input                                   │
│  - WebGPU device/surface (via webgpu.h)                 │
│  - Texture blit (display chain output)                   │
│  - Hot-reload (file watch, compile, dlopen)              │
│  - Context (time, input, device access)                  │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ wgpu-native (Mozilla's WebGPU in Rust)                   │
│                                                          │
│  Metal (macOS), Vulkan (Linux), D3D12 (Windows)          │
└─────────────────────────────────────────────────────────┘
```

**Why this approach:** Rendering engines have opinions. When an engine's PBR renderer and GLTF loader use incompatible structures, you fight the engine. WebGPU gives control without imposing a paradigm—addons provide abstractions, core has no rendering opinions.

---

## Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| wgpu-native | WebGPU implementation | Mozilla's Rust-based, cross-platform |
| GLFW | Window/input | Cross-platform |
| GLM | Math | Vectors, matrices, column-major |
| Dear ImGui | Debug UI | Chain visualizer, parameter tweaking |
| stb_image | Image loading | Header-only, in core |

**Addon dependencies:**
- vivid-video: FFmpeg, AVFoundation, Vidvox HAP
- vivid-audio: miniaudio, KissFFT
- vivid-render3d: cgltf, mikktspace
- vivid-network: oscpack (OSC), WebSocket

---

## Project Structure

```
my-project/
├── chain.cpp       # Required: Your visual program
├── CLAUDE.md       # Recommended: AI context for this project
├── shaders/        # Optional: Custom WGSL shaders
└── assets/         # Optional: textures/, models/, hdris/
```

---

## Common Pitfalls

| Problem | Symptom | Solution |
|---------|---------|----------|
| Wrong vertex count | Nothing renders | Use 3 for fullscreen triangle |
| Texture format mismatch | Black/garbled output | Match surface format (BGRA8Unorm) |
| Missing uniform update | Stale shader values | Update buffer before draw |
| Wrong coordinate space | Upside down | Flip Y in shader or vertex |

---

## Known Issues

### Metal Memory Leak

wgpu-native's Metal backend leaks ~1 MB/10s from render pass creation. This is a driver-level issue in Apple's AGX compiler, not fixable at the application level.

**Upstream issue:** https://github.com/gfx-rs/wgpu/issues/8768

See [/_MEMORY_LEAK.md](../_MEMORY_LEAK.md) for investigation details.

---

## Future Directions

Potential areas for expansion (not committed):

- **Web export** - Compile to WebAssembly + WebGPU for browser deployment
- **ML integration** - ONNX Runtime for pose detection, style transfer
- **Addon registry** - Package management for community addons
- **Runtime distribution** - Bundle projects into standalone apps
