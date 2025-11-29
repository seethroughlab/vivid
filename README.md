# Vivid

A text-first creative coding framework with hot-reload and inline previews. Write C++ operators, see output at every step—designed for LLM-assisted development.

## Quick Start

```bash
# Clone and build
git clone https://github.com/seethroughlab/vivid
cd vivid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# Run an example
./build/bin/vivid-runtime examples/hello
```

## Usage

Create a `chain.cpp` file:

```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    chain.add<Noise>("noise").scale(4.0f).speed(1.0f);
    chain.add<HSV>("color").input("noise").colorize(true).saturation(0.7f);
    chain.setOutput("color");
}

void update(Chain& chain, Context& ctx) {
    chain.get<HSV>("color").hueShift(ctx.time() * 0.1f);
}

VIVID_CHAIN(setup, update)
```

Run it:
```bash
./build/bin/vivid-runtime /path/to/your/project
```

Edit any `.cpp` or `.wgsl` file and watch it hot-reload instantly.

## Built-in Operators

| Category | Operators |
|----------|-----------|
| **Generators** | Noise, Gradient, Shape, Constant |
| **Effects** | Blur, Feedback, Mirror, Transform, Displacement, Edge, Bloom, Vignette, ChromaticAberration, Pixelate, Scanlines |
| **Color** | HSV, Brightness, Composite |
| **Media** | VideoFile, ImageFile, Webcam |
| **Modulation** | LFO, Math, Logic |
| **3D** | Mesh3D, Camera3D, GPU Instancing |

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
- C++20 compiler (Clang 14+, GCC 11+, MSVC 2022+)
- Node.js 18+ (for VS Code extension)

## Documentation

- [Installation Guide](docs/INSTALL.md) — Complete setup for all platforms
- [Chain API Guide](docs/CHAIN-API.md) — Composing operators with the fluent API
- [Operator Reference](docs/OPERATORS.md) — All built-in operators and parameters
- [Creating Operators](docs/OPERATOR-API.md) — How to write custom operators
- [Shader Conventions](docs/SHADER-CONVENTIONS.md) — WGSL shader structure
- [Philosophy](docs/PHILOSOPHY.md) — Design goals and inspirations

## Examples

See the `examples/` directory:

- `hello` — Minimal animated noise
- `feedback-kaleidoscope` — Feedback with mirror effects
- `post-processing` — Bloom and vignette effects
- `3d-instancing` — 1000 GPU-instanced cubes
- `video-playback` — HAP video with effects

## Project Status

**Core Runtime** — Complete
- WebGPU rendering, C++ hot-reload, shader hot-reload
- Chain API, async GPU readback, shared memory previews

**Operator Library** — 20+ operators implemented

**VS Code Extension** — Complete
- Inline previews, live panel, error diagnostics

**Coming Soon**
- Audio input with FFT
- MIDI/OSC input
- Video recording/export

## Contributing

```bash
# Build in debug mode
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8

# Run with an example
./build/bin/vivid-runtime examples/hello
```

Issues and PRs welcome at [github.com/seethroughlab/vivid](https://github.com/seethroughlab/vivid).

## License

[MIT](LICENSE)
