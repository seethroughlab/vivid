# Vivid

A text-first creative coding framework inspired by TouchDesigner, designed for LLM-assisted development.

## What Is This?

Vivid is a C++ framework for real-time visual programming. Unlike node-based tools where the graph is the source of truth, Vivid keeps everything in plain C++ files. You write code, you see output—at every step in the chain.

The framework hot-reloads your C++ operators as you edit them. A VS Code extension shows live previews inline with your code, so you can see exactly what each stage of your pipeline produces without switching windows or mentally tracing connections.

## Inspirations

**TouchDesigner** pioneered the idea that every node should show its output. You can see the data flowing through the graph, which makes creative exploration intuitive. But the node graph is a black box to text tools and LLMs.

**openFrameworks** proved that a C++ creative coding toolkit can be both powerful and approachable. Its addon ecosystem and "batteries included" philosophy make complex things accessible without hiding the underlying tech. Vivid aims for that same extensibility.

**Jupyter Notebooks** put output directly below code. Each cell shows its result, making iteration fast and debugging visual. But notebooks are awkward for structured programs and don't hot-reload.

**Strudel.cc** takes this further—the code *is* the live performance. Type a pattern, hear it immediately. The text and the output are unified, not separate windows you switch between.

Vivid combines these ideas: the inspect-anywhere philosophy of TouchDesigner, the extensibility of openFrameworks, the inline output of notebooks, and the immediacy of Strudel—but with plain C++ that LLMs can read and write.

## Why Build This?

**TouchDesigner is powerful but opaque.** The node graph is a binary blob that language models can't read or write. Diffing changes is painful. Version control is an afterthought. Collaboration means sharing screenshots and hoping.

**Text-based tools lack visibility.** Processing, openFrameworks, and Cinder are excellent for creative coding, but you only see the final output. Debugging means adding print statements or mentally simulating the pipeline.

**Vivid combines the best of both:** the inspectability of a node graph with the portability and LLM-friendliness of plain text.

## Core Principles

### Text Is the Source of Truth

Your project is C++ files, shader files, and a simple YAML config. No binary formats, no proprietary containers. Everything diffs cleanly, merges sanely, and fits in a Git repository.

### See Every Step

When you define an operator chain, each step shows its output. Textures render as thumbnails. Numeric values display inline. You never have to guess what's happening inside the pipeline.

```cpp
// chain.cpp - Each operator shows its output in VS Code
#include <vivid/vivid.h>

class NoiseOp : public vivid::Operator {
    vivid::Texture output_;
public:
    void init(vivid::Context& ctx) override {
        output_ = ctx.createTexture(ctx.width(), ctx.height());
    }
    void process(vivid::Context& ctx) override {
        ctx.runShader("shaders/noise.wgsl", {}, output_);  // [preview]
        ctx.setOutput("out", output_);
    }
    vivid::OutputKind outputKind() const override {
        return vivid::OutputKind::Texture;
    }
};
VIVID_OPERATOR(NoiseOp)
```

The VS Code extension shows `[img]` decorations with hover previews for each operator's output.

### Hot Reload Everything

Edit a `.cpp` file, save, and see the change immediately. No restart, no lost state. The runtime recompiles only what changed, swaps the shared library, and preserves operator state across the reload.

Shaders hot-reload too. Edit a `.wgsl` file and watch the output update in real time.

### LLM-Native Workflow

Because everything is plain text with clear structure, language models can:

- Read and understand your entire project
- Generate new operators or modify existing ones
- Suggest optimizations or debug issues
- Refactor pipelines without breaking connections

The framework is designed so that an LLM can be a genuine collaborator, not just a code snippet generator.

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│  VS Code + Vivid Extension                               │
│  - Code editing                                          │
│  - Inline preview decorations                            │
│  - Side panel with full node outputs                     │
│  - Parameter tweaking via hover widgets                  │
└────────────────────────┬─────────────────────────────────┘
                         │ WebSocket
                         ▼
┌──────────────────────────────────────────────────────────┐
│  Vivid Runtime                                           │
│  - File watcher (source + shaders)                       │
│  - Incremental C++ compilation                           │
│  - Shared library hot-reload                             │
│  - Operator graph execution                              │
│  - Preview capture and streaming                         │
│  - Output window rendering                               │
└──────────────────────────────────────────────────────────┘
```

## Operator Types

Inspired by TouchDesigner's operator families:

- **TOPs (Texture Operators)** — Image/texture processing: noise, blur, feedback, composite, shader
- **CHOPs (Channel Operators)** — Numeric streams: LFO, math, MIDI input, audio analysis
- **SOPs (Surface Operators)** — Geometry: shapes, meshes, instancing, deformations
- **MATs (Materials)** — Shading: PBR materials, custom shaders, texture mapping

Each operator type has appropriate preview rendering: textures show thumbnails, channels show values or sparklines, geometry shows wireframe previews.

## Built-in Operators

### Texture Operators (TOPs)

| Operator | Description |
|----------|-------------|
| **Noise** | Simplex noise with FBM. Scale, speed, octaves, lacunarity, persistence. |
| **Gradient** | Linear, radial, angular, diamond gradients with configurable colors. |
| **Shape** | SDF shapes: circle, rectangle, triangle, line, ring, star. Fill/stroke modes. |
| **Blur** | Separable Gaussian blur with configurable radius. |
| **Feedback** | Frame feedback with decay, zoom, rotate, translate. |
| **Composite** | Blend modes: over, add, multiply, screen, difference. |
| **Brightness** | Brightness and contrast adjustment. |
| **HSVAdjust** | Hue shift, saturation, and value adjustment. |
| **Transform** | Translate, scale, rotate with configurable pivot. |
| **Displacement** | UV displacement using a texture. |
| **Edge** | Sobel edge detection with multiple output modes. |

### Channel Operators (CHOPs)

| Operator | Description |
|----------|-------------|
| **LFO** | Low-frequency oscillator. Sine, saw, square, triangle waveforms. |

## Benefits

### For Creative Coders

- **Faster iteration** — Hot reload means no waiting for builds or restarts
- **Better debugging** — See the output of every step, not just the end
- **Portable projects** — Plain text files work everywhere, forever
- **Real version control** — Meaningful diffs, branches, and merges

### For Teams

- **Code review works** — Review visual changes through code changes
- **No license servers** — Open source, run it anywhere
- **Onboarding is reading** — New team members can understand projects by reading them

### For LLM-Assisted Development

- **Full project context** — Models can read your entire pipeline
- **Structured output** — Models can generate valid operators directly
- **Iterative refinement** — Ask for changes, see them applied, refine further
- **Documentation built-in** — Code comments and structure serve as documentation

## Getting Started

### Prerequisites

- CMake 3.16+
- C++20 compiler (Clang 14+, GCC 11+, MSVC 2022+)
- Node.js 18+ (for VS Code extension)

### Build the Runtime

```bash
# Clone and build
git clone https://github.com/your-org/vivid
cd vivid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# Verify it works
./build/bin/vivid-runtime examples/hello
```

You should see a window with animated noise.

### Install the VS Code Extension

```bash
# Build the extension
cd extension
npm install
npm run compile

# Build the native module for shared memory previews
cd native
npm install
npm run build
cd ../..
```

Then in VS Code:
1. Open the `extension` folder
2. Press `F5` to launch Extension Development Host
3. Open your Vivid project folder

### Create Your First Project

```bash
mkdir my-project && cd my-project
```

Create `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(my_operators)

# Find Vivid headers (set VIVID_ROOT to your vivid directory)
set(VIVID_ROOT "${CMAKE_SOURCE_DIR}/.." CACHE PATH "Path to Vivid")
include_directories(${VIVID_ROOT}/build/include)

add_library(operators SHARED chain.cpp)
set_target_properties(operators PROPERTIES PREFIX "lib")
```

Create `chain.cpp`:
```cpp
#include <vivid/vivid.h>

class MyNoise : public vivid::Operator {
public:
    void init(vivid::Context& ctx) override {
        output_ = ctx.createTexture(ctx.width(), ctx.height());
    }

    void process(vivid::Context& ctx) override {
        ctx.runShader("shaders/noise.wgsl", {}, output_);
        ctx.setOutput("out", output_);
    }

    vivid::OutputKind outputKind() const override {
        return vivid::OutputKind::Texture;
    }

private:
    vivid::Texture output_;
};

VIVID_OPERATOR(MyNoise)
```

Run it:
```bash
cd /path/to/vivid
./build/bin/vivid-runtime /path/to/my-project
```

## Example: Feedback Loop

Here's a complete example showing animated noise with feedback:

```cpp
// chain.cpp
#include <vivid/vivid.h>

class AnimatedNoise : public vivid::Operator {
    vivid::Texture output_;
public:
    void init(vivid::Context& ctx) override {
        output_ = ctx.createTexture(ctx.width(), ctx.height());
    }

    void process(vivid::Context& ctx) override {
        float time = ctx.time();
        ctx.runShader("shaders/noise.wgsl", {
            {"u_time", time},
            {"u_scale", 4.0f}
        }, output_);
        ctx.setOutput("out", output_);
    }

    vivid::OutputKind outputKind() const override {
        return vivid::OutputKind::Texture;
    }
};
VIVID_OPERATOR(AnimatedNoise)
```

Edit any parameter, save, and watch the preview update instantly in VS Code.

## Status

**Core Runtime** — Complete
- WebGPU-based rendering with fullscreen output
- Hot-reload of C++ operators (save and see changes instantly)
- Hot-reload of WGSL shaders
- Async GPU readback for preview capture
- Shared memory preview transfer to VS Code (zero-copy)

**Operator Library** — 12 operators implemented
- Noise, Gradient, Shape, Blur, Feedback, Composite
- Brightness, HSVAdjust, Transform, Displacement, Edge, LFO

**VS Code Extension** — Complete
- Inline `[img]` decorations with hover previews
- Live preview panel with 30fps updates
- Compile error diagnostics inline in editor
- Auto-connect to running runtime

**Coming Soon**
- Image/video file loading
- Audio input with FFT analysis
- MIDI/OSC input
- 3D geometry and particle systems
- Video recording/export

## Documentation

- **[Installation Guide](docs/INSTALL.md)** — Complete setup instructions for all platforms
- **[Operator API Guide](docs/OPERATOR-API.md)** — How to create custom operators
- **[Shader Conventions](docs/SHADER-CONVENTIONS.md)** — WGSL shader structure and uniforms
- **[Operator Reference](docs/OPERATORS.md)** — Built-in operator parameters (auto-generated)

## Implementation Plan

The implementation is documented across four files designed for use with Claude Code or similar LLM-assisted development:

- **[PLAN-01-overview.md](PLAN-01-overview.md)** — Project structure, CMake setup, dependencies
- **[PLAN-02-runtime.md](PLAN-02-runtime.md)** — Core runtime, WebGPU renderer, hot-reload system
- **[PLAN-03-operators.md](PLAN-03-operators.md)** — Operator API, built-in operators, WGSL shaders
- **[PLAN-04-extension.md](PLAN-04-extension.md)** — VS Code extension, WebSocket protocol, inline decorations

## License

MIT
