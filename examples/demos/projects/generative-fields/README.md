# Generative Fields

A bundled visual example (open via **File > Open Example**). Four fullscreen
"field" generators — **Plasma**, **Rings**, **NoiseTexture**, **Gradient** —
feed a **Switch**, so you flip 1-of-4 into the **Output**. **Tint** rides along
in `shaders/` as a fifth available field.

## Why it exists

These five generators used to ship in the core operator catalog. They are
screensaver-style shader *fields*, not the serious-tool primitives the core is
built around (real geometry, sources, compositing effects), so they were
**demoted out of core** and now live here as **project-scoped operators**: the
`.wgsl` files in this project's `shaders/` folder.

A fresh Vivid session lists none of them. Opening this example registers them
(`ShaderLibrary::set_project`, the `project` shader tier); File > New retires
them again. Nothing is machine-specific — this project references no plugins or
samples, so it is fully portable.

## Contents

- `project.json` — the graph (5 generators → Switch → Output; empty audio session)
- `shaders/{plasma,rings,noise_texture,gradient,tint}.wgsl` — the demoted field operators
