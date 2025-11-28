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
auto noise = NODE(Noise())       // [preview: perlin texture]
    .scale(4.0)
    .speed(0.5);

auto lfo = NODE(LFO(2.0));       // ∿ 0.73

auto modulated = NODE(Brightness(noise))
    .amount(lfo);                // [preview: pulsing texture]

Output(modulated);
```

### Hot Reload Everything

Edit a `.cpp` file, save, and see the change immediately. No restart, no lost state. The runtime recompiles only what changed, swaps the shared library, and preserves operator state across the reload.

Shaders hot-reload too. Edit a `.frag` file and watch the output update in real time.

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

```bash
# Clone and build
git clone https://github.com/your-org/vivid
cd vivid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Start the runtime with an example project
./build/vivid-runtime examples/hello

# Open the project folder in VS Code with the Vivid extension installed
code examples/hello
```

## Example: Audio-Reactive Visuals

```cpp
#include <vivid/vivid.h>

void setup(Context& ctx) {
    // Audio input analysis
    auto audio = NODE(AudioIn())
        .device("default")
        .fftSize(1024);
    
    auto bass = NODE(FFTBand(audio))
        .lowFreq(20)
        .highFreq(200);
    
    // Generative texture
    auto noise = NODE(Noise())
        .scale(Mix(2.0, 8.0, bass))  // Bass controls noise scale
        .speed(0.3);
    
    // Feedback with zoom
    auto fb = NODE(Feedback(noise))
        .decay(0.92)
        .zoom(1.0 + bass * 0.02);    // Bass creates zoom pulse
    
    // Color grading
    auto final = NODE(HSVAdjust(fb))
        .hueShift(ctx.time() * 0.1)
        .saturation(1.2);
    
    Output(final);
}
```

Each `NODE()` macro registers the operator with source location information. The VS Code extension receives live previews and displays them inline, so you see the audio reactivity at every stage of the chain.

## Status

This project is in early development. The current focus is:

1. Core runtime with hot-reload C++ operators
2. Basic operator library (noise, math, feedback, composite)
3. VS Code extension with inline previews
4. Shader hot-reload

Future plans include geometry operators, audio/MIDI integration, and a library of community-contributed operators.

## Implementation Plan

The implementation is documented across four files designed for use with Claude Code or similar LLM-assisted development:

- **[PLAN-01-overview.md](PLAN-01-overview.md)** — Project structure, CMake setup, dependencies
- **[PLAN-02-runtime.md](PLAN-02-runtime.md)** — Core runtime, WebGPU renderer, hot-reload system
- **[PLAN-03-operators.md](PLAN-03-operators.md)** — Operator API, built-in operators, WGSL shaders
- **[PLAN-04-extension.md](PLAN-04-extension.md)** — VS Code extension, WebSocket protocol, inline decorations

## License

MIT
