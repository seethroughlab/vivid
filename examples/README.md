# Vivid Examples

Curated examples demonstrating Vivid's capabilities. Each example is a complete, runnable project.

## Running Examples

```bash
./build/bin/vivid examples/getting-started/01-template
```

Press `Tab` to open the chain visualizer and adjust parameters in real-time.

---

## Learning Path

### 1. Getting Started

Start here if you're new to Vivid.

| Example | Description |
|---------|-------------|
| [01-template](getting-started/01-template) | Heavily commented starter with hot-reload tips |
| [02-hello-noise](getting-started/02-hello-noise) | Minimal working example - noise generator |

### 2. 2D Effects

Learn texture operators and visual effects.

| Example | Description |
|---------|-------------|
| [chain-basics](2d-effects/chain-basics) | Core concepts: inputs, compositing, animation |
| [feedback](2d-effects/feedback) | State preservation across frames |
| [kaleidoscope](2d-effects/kaleidoscope) | Chaining multiple effects together |
| [retro-crt](2d-effects/retro-crt) | Retro pipeline: dither, scanlines, CRT |
| [particles](2d-effects/particles) | 2D particle system with emitters |
| [canvas-drawing](2d-effects/canvas-drawing) | Imperative 2D drawing API |

### 3. Audio

Synthesis, sequencing, and audio-reactive visuals.

| Example | Description |
|---------|-------------|
| [drum-machine](audio/drum-machine) | Drum synthesis, patterns, Euclidean rhythms |
| [audio-reactive](audio/audio-reactive) | Audio analysis driving visual effects |

### 4. 3D Rendering

PBR rendering, models, and instancing.

| Example | Description |
|---------|-------------|
| [3d-basics](3d-rendering/3d-basics) | Scene, camera, primitives, CSG, lighting |
| [gltf-loader](3d-rendering/gltf-loader) | Loading GLTF/GLB models with IBL |
| [instancing](3d-rendering/instancing) | GPU instancing for thousands of objects |

---

## Showcase

Impressive demos showing the best of what Vivid can do. These are "wow factor" examples with audio-reactive visuals, generative art, and multi-layered effects.

| Example | Description |
|---------|-------------|
| [flow-field](showcase/flow-field) | Generative particle art with noise-driven flow fields and ethereal trails |
| [audio-visualizer](showcase/audio-visualizer) | FFT-driven particles with beat detection, bloom, and chromatic aberration |

---

## More Resources

- [LLM-REFERENCE.md](../docs/LLM-REFERENCE.md) - Compact operator reference
- [RECIPES.md](../docs/RECIPES.md) - Common patterns and techniques
- [testing-fixtures/](../testing-fixtures/) - Additional examples used for testing
