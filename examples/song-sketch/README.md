# song-sketch — a folder project demo

A complete Vivid PoC project authored MCP-natively, demonstrating the **project-as-folder**
model: everything the scene needs lives in this directory and reloads together.

```
song-sketch/
├── project.json        # the session (tracks × clips, the visuals chain, the bridge mappings)
├── vivid-package.json  # a project-local operator package manifest
├── aurora_field.cpp    # a custom C++ visual operator (AuroraField), compiled on load
├── aurora.glsl         # a data-driven CustomShader source
└── README.md
```

## What it contains
- **Song** (authored via the music-theory tools): 96 BPM in **A minor** — a `i–VI–III–VII`
  chord progression (Am–F–C–G), a root-motion bass an octave down, and a kick/snare groove
  with a Euclidean `E(5,8)` hat pattern. All quantized to the key.
- **Visuals**: the default `…→Feedback→Blur→Output` chain driven by **AuroraField** (the
  project-local C++ operator) plus a **CustomShader** node rendering `aurora.glsl`.
- **Bridge**: audio characteristics wired to the visual params (`master.transient→warp`,
  `master.level→glow`, `master.low→hue`, …) so the visuals react to the music.

## Run it
Launch the app, then over MCP (or the control server): `load_project("<abs>/examples/song-sketch")`.
On load, `AuroraField` is compiled from source into this folder and registered **before** the
graph restores (so its node resolves), the CustomShader picks up `aurora.glsl`, and the song +
mappings come back. Press play (spacebar / the ▶ button / `set_playing`).

> `AuroraField.dylib` is a build artifact (git-ignored) — it is recompiled from
> `aurora_field.cpp` on every load, so the project stays portable.
