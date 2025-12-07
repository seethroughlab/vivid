# Philosophy

Vivid combines the inspect-anywhere philosophy of TouchDesigner, the extensibility of openFrameworks, the inline output of Jupyter notebooks, and the immediacy of Strudel—but with plain C++ that LLMs can read and write.

## Inspirations

**TouchDesigner** pioneered the idea that every node should show its output. You can see the data flowing through the graph, which makes creative exploration intuitive. But the node graph is a black box to text tools and LLMs.

**openFrameworks** proved that a C++ creative coding toolkit can be both powerful and approachable. Its addon ecosystem and "batteries included" philosophy make complex things accessible without hiding the underlying tech. Vivid aims for that same extensibility.

**Jupyter Notebooks** put output directly below code. Each cell shows its result, making iteration fast and debugging visual. But notebooks are awkward for structured programs and don't hot-reload.

**Strudel.cc** takes this further—the code *is* the live performance. Type a pattern, hear it immediately. The text and the output are unified, not separate windows you switch between.

## Why Build This?

**TouchDesigner is powerful but opaque.** The node graph is a binary blob that language models can't read or write. Diffing changes is painful. Version control is an afterthought. Collaboration means sharing screenshots and hoping.

**Text-based tools lack visibility.** Processing, openFrameworks, and Cinder are excellent for creative coding, but you only see the final output. Debugging means adding print statements or mentally simulating the pipeline.

**Vivid combines the best of both:** the inspectability of a node graph with the portability and LLM-friendliness of plain text.

## Core Principles

### Don't Reinvent the Wheel

Before building something from scratch, research existing solutions. If a well-maintained library, engine feature, or standard approach exists, use it. Custom code should only be written when:

- No suitable solution exists
- Existing solutions don't fit our architecture
- The integration cost exceeds the implementation cost

This applies to everything: rendering techniques, UI components, algorithms, file formats. When a new feature seems complex, first ask: "Who has solved this before?"

### Keep the Core Minimal

The runtime should do as little as possible: window management, timing, input, hot-reload, and addon detection. Everything else—rendering, effects, 3D, media—belongs in addons.

**Why this matters:**
- A small core is easier to understand and maintain
- Addons can evolve independently
- Users only pay for what they use
- LLMs can reason about ~600 lines more easily than ~6000

### Text Is the Source of Truth

Your project is C++ files, shader files, and a simple YAML config. No binary formats, no proprietary containers. Everything diffs cleanly, merges sanely, and fits in a Git repository.

### See Every Step

When you define an operator chain, each step shows its output. Textures render as thumbnails. Numeric values display inline. You never have to guess what's happening inside the pipeline.

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

## Lessons Learned

These insights emerged from building and iterating on Vivid:

### Rendering Engines Have Opinions

Full-featured rendering engines (Diligent, bgfx, etc.) provide powerful abstractions, but they also impose constraints. When an engine's PBR renderer and GLTF loader use incompatible internal structures, you end up fighting the engine instead of building your app.

**The solution:** Use a low-level graphics API (WebGPU) that gives you control without imposing a rendering paradigm. Let addons provide higher-level abstractions.

### One Abstraction Layer is Enough

Layering abstractions (your API → engine API → native API) adds complexity and debugging difficulty. Each layer has its own conventions, error handling, and performance characteristics.

**The solution:** Write directly against a portable low-level API. WebGPU's `webgpu.h` via Dawn (Google's production-tested C++ implementation) provides a clean abstraction over Vulkan, Metal, and D3D12.

### Compatibility is the User's Problem

Trying to make every addon compatible with every other addon leads to lowest-common-denominator design. A 3D renderer, a 2D effects library, and an audio analyzer have fundamentally different needs.

**The solution:** Define clear addon interfaces, but don't force compatibility. If a user wants to combine two addons, they understand they're taking on that integration work.

### Hot-Reload is Non-Negotiable

The ability to edit code and see changes instantly is what makes creative coding feel like creative coding. Any architecture decision that compromises hot-reload should be reconsidered.

**The solution:** Design the entire system around hot-reload from day one. State preservation, error recovery, and graceful degradation all serve this goal.

### LLMs Need Small Context

A language model reasoning about a 10,000-line runtime will miss things. A model reasoning about a 600-line core can understand every interaction.

**The solution:** Keep the core tiny. Push complexity to addons that can be understood in isolation.
