# VIVID

**A real-time creative coding platform where audio and visuals are equal peers in the same graph.**

Vivid is a node-based environment for building audiovisual performances, installations, and interactive art. Audio operators (synthesizers, drum machines, effects) and GPU operators (shapes, filters, compositing) share the same graph, driven by the same data, with the same level of expressive control. Everything is a JSON file. Everything hot-reloads.

Built in C++ with WebGPU (Dawn). Designed from the ground up for LLM-assisted creative exploration.

![Vivid — WGSL filter chain processing video through 10+ real-time GPU filters](docs/images/wgsl-filters-demo.png)

## Screenshots

| | |
|---|---|
| ![Shape Instance Demo](docs/images/shape-instance-demo.png) | ![Feedback Demo](docs/images/feedback-demo.png) |
| **Instanced shapes driven by audio envelopes** — A chord progression triggers a wavetable synth and drum kit; per-voice ADSR envelopes drive GPU shape instancing. | **Feedback with temporal accumulation** — A star shape modulated by LFOs feeds into a feedback operator with decay, zoom, and rotation, creating recursive visual trails. |
| ![FFT Bars Demo](docs/images/fft-bars-demo.png) | ![WGSL Filters Demo](docs/images/wgsl-filters-demo.png) |
| **Audio-reactive FFT visualization** — An oscillator's output is analyzed in real time; the frequency spectrum drives an instanced bar graph on the GPU. | **Video filter chain** — A movie file piped through HSV, mirror, displace, posterize, chromatic aberration, scanlines, CRT effect, and more — all modulated by LFOs. |

## Features

- **Audio-visual parity** — Audio and GPU operators live in the same graph, connected by the same wires
- **Three execution domains** — GPU (textures), Audio (sample-rate buffers), and Control (per-frame values) evaluated in dependency order
- **50+ built-in operators** — Synthesizers, drum machines, audio effects, shape generators, image filters, compositors, and control sources
- **Spreads** — Implicit vectorization: a single wire carries N values, enabling polyphony and instancing without manual fan-out
- **19 data-driven WGSL filters** — Self-describing GPU shaders loaded at runtime from `.wgsl` files with embedded metadata
- **Hot-reload everything** — Edit a graph JSON, a WGSL filter, or recompile an operator dylib — changes apply instantly without restart
- **JSON graph as single source of truth** — Every graph is a plain JSON file: nodes, connections, parameters, layout
- **Live thumbnails on every node** — GPU nodes show texture previews; audio nodes show waveforms; control nodes show value readouts
- **MCP server** — 22-tool Model Context Protocol server for LLM integration (Claude Code, etc.)
- **MIDI input with CC learn** — Hardware controller support with automatic CC mapping

## Built-in Operators

### GPU
Shape, Noise, Bars, Composite, Bloom, Feedback, Instance, Movie File In

### GPU Filters (WGSL)
HSV, Levels, Blur, Gaussian Blur, Edge, Mirror, Pixelate, Posterize, Gradient, Chromatic Aberration, Scanlines, CRT Effect, Transform, Displace, Channel Mixer, Vignette, Dither, Halftone, Tile, Ramp, Solid Color, Switch

### Audio
**Synthesis:** Oscillator, Wavetable Synth, Gain
**Drums:** Kick, Snare, Hi-Hat, Clap, Tom, Cymbal
**Effects:** Reverb, Delay, Bitcrush, Distortion, Stutter, Tape Stop, Beat Repeat, Reverse, Scratch, Stretch, Freq Shift, Glitch
**Spread:** Spread ADSR, Spread LFO
**Input:** Movie File Audio In

### Control
Clock, LFO, Math, Envelope, MIDI Input, FFT Analysis, Note Pattern, Chord Progression, Arpeggiator, Drum Sequencer, Sequencer, Logic, Gate, Random, Smooth

### Sinks
`audio_out` (stereo output), `video_out` (GPU display with fit/fill/stretch)

## Getting Started

### Build

```bash
git clone --recursive https://github.com/jeffcrouse/vivid.git
cd vivid
cmake -B build
cmake --build build
```

### Run

```bash
# Open a demo graph
./build/vivid graphs/feedback_demo.json

# Headless screenshot
./build/vivid graphs/wgsl_filters_demo.json --screenshot output.png --screenshot-delay 15
```

### Requirements

- C++17 compiler (Clang or GCC)
- CMake 3.20+
- macOS, Linux, or Windows (macOS is the primary development platform)

Dependencies are vendored or fetched automatically: WebGPU (Dawn), GLFW, miniaudio, RtMidi, yyjson, stb_image_write, stb_truetype, IXWebSocket, CLI11.

## Architecture

Vivid evaluates a directed acyclic graph across three domains every frame:

| Domain | Rate | Data type | Examples |
|--------|------|-----------|---------|
| **Control** | Per-frame (~60 Hz) | Scalar values, spreads | Clock, LFO, Envelope, MIDI |
| **GPU** | Per-frame | Textures (800x600) | Shape, Filters, Composite |
| **Audio** | Per-sample (48 kHz) | Float buffers (256 frames) | Oscillator, Drums, Effects |

Control outputs can drive both GPU and Audio parameters. GPU and Audio operators expose analysis outputs (peak, RMS, spectrum) back to the control domain. Spreads propagate automatically — connect a 16-voice chord to a synth and you get 16-voice polyphony; connect those envelopes to a shape and you get 16 instances.

## Documentation

- **[Product Requirements](docs/PRD.md)** — Vision, core principles, system architecture
- **[Architecture](docs/ARCHITECTURE.md)** — Language choice, build system, operator contract, directory structure
- **[Interface Design](docs/INTERFACE.md)** — UI architecture, visual style, node graph rendering
- **[LLM Integration](docs/LLM-INTEGRATION.md)** — MCP server, the four LLM roles

## Status

Active development. Core engine, node graph UI, 50+ operators, WGSL filter framework, and MCP server are complete.

## License

Vivid is source-available under the [Vivid Source Available License](LICENSE). You can use it freely for personal projects, artistic work, and education. Operator Packages you create in separate repositories are yours to license however you choose. See [LICENSING.md](LICENSING.md) for full details.

Contributions to this repository are subject to the [Contributor License Agreement](CLA.md).

---

*Jeff Crouse / [See-Through Lab](https://see-through.studio)*
