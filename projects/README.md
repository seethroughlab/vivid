# Vivid Projects

Curated projects demonstrating Vivid's capabilities. Each project is complete and runnable.

## Running Projects

```bash
./build/bin/vivid projects/getting-started/01-template
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

## Addon Projects

Each addon contains its own projects in its `examples/` directory:

| Addon | Projects | Description |
|-------|----------|-------------|
| [vivid-effects-2d](../src/addons/vivid-effects-2d/examples/) | 6 projects | 2D effects, particles, canvas drawing |
| [vivid-render3d](../src/addons/vivid-render3d/examples/) | 3 projects | 3D rendering, GLTF, instancing |
| [vivid-audio](../src/addons/vivid-audio/examples/) | 4 projects | Synthesis, sequencing, audio-reactive |
| [vivid-network](../src/addons/vivid-network/examples/) | 3 projects | OSC, UDP, web control |

Run addon projects the same way:
```bash
./build/bin/vivid src/addons/vivid-effects-2d/examples/kaleidoscope
./build/bin/vivid src/addons/vivid-audio/examples/drum-machine
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
- [tests/fixtures/](../tests/fixtures/) - Core testing examples
