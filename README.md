# Vivid

A text-first creative coding framework with hot-reload and inline previews. Write C++ chains, see output at every step—designed for LLM-assisted development. Built on Diligent Engine for cross-platform Vulkan/Metal/DirectX rendering.

## Quick Start

```bash
# Clone with submodules
git clone --recursive https://github.com/seethroughlab/vivid
cd vivid

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# Run an example (macOS)
./build/runtime/vivid.app/Contents/MacOS/vivid examples/hello-noise
```

## Usage

Create a `chain.cpp` file:

```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Context& ctx) {
    std::cout << "Chain loaded!" << std::endl;
}

void update(Context& ctx) {
    // Access time, input, render operators
    float t = ctx.time();
}

VIVID_CHAIN(setup, update)
```

Run it:
```bash
# macOS
./build/runtime/vivid.app/Contents/MacOS/vivid /path/to/your/project
```

Edit your `chain.cpp` and watch it hot-reload instantly.

## Built-in Operators

| Category | Operators |
|----------|-----------|
| **Generators** | Noise, Gradient, SolidColor |
| **Effects** | Blur, Feedback, Mirror, Transform, Displacement, EdgeDetect, ChromaticAberration, Pixelate |
| **Color** | HSV, BrightnessContrast, Composite |
| **3D** | GLTFViewer, Render3D (PBR, IBL, multi-light) |
| **Utility** | Passthrough, Output |

## VS Code Extension

The extension shows live previews inline with your code:

```bash
cd extension
npm install && npm run compile
cd native && npm install && npm run build
```

Press F5 in VS Code to launch the extension development host.

## Requirements

- CMake 3.16+
- C++17 compiler (Clang 14+, GCC 11+, MSVC 2022+)
- Vulkan SDK (macOS: MoltenVK via Homebrew)
- Node.js 18+ (for VS Code extension)

## Documentation

- [Chain API Guide](docs/CHAIN-API.md) — Composing operators with the fluent API
- [Operator Reference](docs/OPERATORS.md) — All built-in operators and parameters
- [Creating Operators](docs/OPERATOR-API.md) — How to write custom operators
- [Philosophy](docs/PHILOSOPHY.md) — Design goals and inspirations
- [Roadmap](ROADMAP.md) — Development roadmap and architecture

## Examples

See the `examples/` directory:

- `hello-noise` — Minimal hot-reload demo
- `mesh-test` — 3D mesh rendering with PBR
- `lighting-test` — Multi-light system demo
- `gltf-test` — GLTF model loading with IBL
- `pbr-test` — PBR materials and environment maps
- `video-playback` — Video file playback

## Project Status

**Core Runtime** — Complete
- Diligent Engine rendering (Vulkan/Metal/DirectX)
- C++ hot-reload via dynamic library loading
- Cross-platform window/input via GLFW

**3D Rendering** — In Progress
- PBR materials with metallic/roughness workflow
- IBL (Image-Based Lighting) with HDR environment maps
- GLTF model loading
- Multi-light system (directional, point, spot)
- Shadow mapping (in development)

**Operator Library** — 18 operators implemented

**VS Code Extension** — In Development

**Coming Soon**
- Audio input with FFT
- MIDI/OSC input
- Video recording/export

## Contributing

```bash
# Build in debug mode
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8

# Run with an example (macOS)
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
./build/runtime/vivid.app/Contents/MacOS/vivid examples/hello-noise
```

Issues and PRs welcome at [github.com/seethroughlab/vivid](https://github.com/seethroughlab/vivid).

## License

[MIT](LICENSE)
