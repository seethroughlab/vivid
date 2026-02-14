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

### Audio-Visual Parity

**Keep creators in a unified audio-visual headspace by making both domains equally expressive through the same code-first workflow.**

Vivid treats audio and visuals as equal peers. A drum machine should be as easy to create as a particle system. An LFO should modulate both a filter cutoff and a bloom intensity. Sequencer triggers should fire visual events just as naturally as audio events.

This philosophy extends to how operators are built:

| Visual | Audio | Purpose |
|--------|-------|---------|
| CRTEffect | TapeEffect | Vintage character |
| FilmGrain | Crackle | Surface texture |
| Noise | Oscillator | Generation |
| Feedback | Delay | Temporal recursion |
| Bloom | Reverb | Diffusion/space |
| Particles | Granular | Discrete elements |

**Why native operators matter:** External plugins create distance between creator and creation. When you use a commercial synth plugin, you can't read its source to understand how it works. With native operators, everything is learnable—you can trace the DSP code, modify parameters, and truly understand your instrument.

The goal is co-creation: encouraging "artist making audio-visual work" rather than "musician adding visuals" or "visual artist adding audio."

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

When you define an operator chain, each step shows its output. Textures render as thumbnails with health-colored borders — green for active signal, yellow for near-silent, red for errors. Audio operators show inline level meters. Sparklines on each node pulse when the output is changing and flatline when it stalls.

This visibility serves two audiences: the performer sees chain health at a glance during a show, and the developer (or LLM agent) gets the same data via MCP inspection as structured JSON.

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

## Built-in Tools: For Performance, Not Development

Vivid's built-in tools serve the performer and the live debugger, not the developer. External tools — Claude Code, Cursor, VS Code, terminal editors — handle code editing, file management, and version control. Vivid handles what those tools cannot: real-time visual and audio feedback during a live session.

### Two Audiences, Two Interfaces

**The Performer** uses Vivid's built-in UI during a show or creative session:

- **Node Graph** — Visual chain with live thumbnails and health indicators
- **Inspector** — Parameter sliders for the selected node (auto-shows on selection)
- **Status Bar** — FPS, frame time, resolution, memory, record/snapshot controls
- **Performance Panel** (`Cmd+1`) — Detailed real-time metrics
- **Console** — Read-only log overlay for compile status and runtime warnings

**The Developer** (human or LLM agent) uses external tools:

- **External editors** — VS Code, Cursor, Neovim, or any text editor
- **MCP server** — Claude Code connects to the running Vivid instance for parameter queries, frame capture, and inspection
- **CLI commands** — `vivid check`, `vivid inspect`, `--snapshot` for CI and validation
- **Hot-reload** — Edit any `.cpp` or `.wgsl` file and see changes immediately, regardless of which tool made the edit

### What Was Removed

Vivid originally included a code editor, integrated terminal, and file browser — a self-contained IDE. These were removed because:

- **External tools do it better.** Competing with VS Code and terminal emulators on editing and file management is a losing proposition.
- **LLM workflows don't need them.** In an agent-driven workflow, nobody is hand-editing code inside Vivid. The agent writes to disk, hot-reload picks it up.
- **Hot-reload is tool-agnostic.** The file watcher doesn't care what wrote the file. This decoupling means any external tool works without integration effort.

### Shared Architecture

The same underlying systems serve both audiences through different interfaces:

| System | Performer (Built-in UI) | Developer (External Tools) |
|--------|------------------------|---------------------------|
| Parameters | Inspector sliders | MCP `set_param` / `get_live_params` |
| Inspection | Health-colored borders, sparklines | MCP `get_runtime_status` / `vivid inspect` |
| Recording | Status bar record button | `--snapshot` flag / MCP `capture_frame` |
| Chain state | Node graph with live thumbnails | MCP `get_runtime_status` / `vivid inspect` |

Building for one audience directly benefits the other.

### Decision Framework

When considering whether a new feature belongs inside Vivid's UI or outside it, ask:

1. **Does it require real-time GPU/audio output?** If yes, it belongs inside Vivid. External tools can't render live thumbnails or play audio from the chain.
2. **Does it duplicate what external tools already do well?** If yes, keep it outside. Editors, terminals, file browsers, and version control are solved problems.
3. **Would a performer use it on stage?** If yes, it belongs inside Vivid with large, high-contrast UI elements designed for dark rooms.

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

### For Live Performance

- **Real-time visual feedback** — Node graph with live thumbnails and health indicators shows chain state at a glance
- **Performance monitoring** — FPS, frame time, and memory usage always visible; detailed metrics one keypress away
- **Parameter control** — Inspector sliders for immediate tweaking, with MIDI mapping for physical controllers
- **Recording and snapshots** — Capture output to PNG or video without interrupting the session

### For LLM-Assisted Development

- **Full project context** — Models can read your entire pipeline
- **Structured output** — Models can generate valid operators directly
- **Iterative refinement** — Ask for changes, see them applied, refine further
- **Documentation built-in** — Code comments and structure serve as documentation

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│  Vivid Runtime                                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Core: file watcher, compilation, hot-reload,      │  │
│  │  operator graph execution, output rendering        │  │
│  └──────────────┬──────────────────────┬──────────────┘  │
│                 │                      │                  │
│  ┌──────────────▼───────────┐  ┌──────▼───────────────┐  │
│  │  Built-in Devtools       │  │  MCP Server           │  │
│  │  (--show-ui / ` toggle)  │  │  (WebSocket :9876)    │  │
│  │                          │  │                       │  │
│  │  - Node Graph            │  │  - Parameter queries  │  │
│  │  - Inspector             │  │  - Frame capture      │  │
│  │  - Status Bar            │  │  - Chain inspection   │  │
│  │  - Performance (Cmd+1)   │  │  - Documentation      │  │
│  │  - Console               │  │                       │  │
│  └──────────────────────────┘  └───────────┬───────────┘  │
└──────────────────────────────────────────────────────────┘
                                             │
                          ┌──────────────────▼──────────────┐
                          │  External Tools                  │
                          │  Claude Code, Cursor, VS Code,   │
                          │  CLI (vivid check / inspect)     │
                          └─────────────────────────────────┘
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

### Development Tools vs Performance Tools

Vivid originally included a built-in code editor, integrated terminal, and file browser — a self-contained IDE inside the application. The assumption was that the developer would work *inside* Vivid.

That assumption broke when LLM-assisted coding became the primary development workflow. Claude Code, Cursor, and similar tools already have world-class editors, terminals, and file management. Building a second, worse version of all that inside Vivid meant competing with teams of hundreds on a problem that was already solved and getting better fast.

**The solution:** Split tooling by audience. The performer gets real-time UI that external tools can't replicate — node graph with live thumbnails, parameter sliders, health indicators, recording controls. The developer gets external tools for editing plus an MCP server that bridges those tools to the running Vivid instance. The same underlying systems (parameters, inspection, chain state) serve both audiences through different interfaces.
