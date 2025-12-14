# Vivid Examples

Curated examples demonstrating Vivid's capabilities. Each example is a complete, runnable project.

## Running Examples

```bash
./build/bin/vivid examples/getting-started/01-template
```

Press `Tab` to open the chain visualizer and adjust parameters in real-time.

---

## Getting Started

Start here if you're new to Vivid.

| Example | Description |
|---------|-------------|
| [01-template](getting-started/01-template) | Heavily commented starter with hot-reload tips |
| [02-hello-noise](getting-started/02-hello-noise) | Minimal working example - noise generator |

---

## Addon Examples

Each addon contains its own examples in its `examples/` directory:

| Addon | Examples | Description |
|-------|----------|-------------|
| [vivid-effects-2d](../addons/vivid-effects-2d/examples/) | 6 examples | 2D effects, particles, canvas drawing |
| [vivid-render3d](../addons/vivid-render3d/examples/) | 3 examples | 3D rendering, GLTF, instancing |
| [vivid-audio](../addons/vivid-audio/examples/) | 4 examples | Synthesis, sequencing, audio-reactive |
| [vivid-network](../addons/vivid-network/examples/) | 3 examples | OSC, UDP, web control |
| [vivid-ml](../addons/vivid-ml/examples/) | 1 example | ML inference, pose tracking |

Run addon examples the same way:
```bash
./build/bin/vivid addons/vivid-effects-2d/examples/kaleidoscope
./build/bin/vivid addons/vivid-audio/examples/drum-machine
```

---

## Showcase

Impressive multi-addon demos showing the best of what Vivid can do. These are "wow factor" examples with audio-reactive visuals, generative art, and multi-layered effects.

| Example | Description | Addons Used |
|---------|-------------|-------------|
| [flow-field](showcase/flow-field) | Generative particle art with noise-driven flow fields | effects-2d |
| [audio-visualizer](showcase/audio-visualizer) | FFT-driven particles with beat detection, bloom | audio, effects-2d |
| [depth-of-field](showcase/depth-of-field) | 3D scene with depth-based blur | render3d, effects-2d |
| [gltf-gallery](showcase/gltf-gallery) | Interactive GLTF model viewer with PBR+IBL | render3d |
| [wipeout-viz](showcase/wipeout-viz) | Racing game style visualization | audio, render3d, effects-2d |

---

## More Resources

- [LLM-REFERENCE.md](../docs/LLM-REFERENCE.md) - Compact operator reference
- [RECIPES.md](../docs/RECIPES.md) - Common patterns and techniques
- [testing-fixtures/](../testing-fixtures/) - Core testing examples
