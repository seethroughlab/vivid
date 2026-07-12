# Vivid demos — four songs, authored over MCP

Four complete audio-visual scenes, each **built entirely by a Python script that drives
Vivid's control server** (the same backend the MCP bridge speaks). The script *is* the
point: it shows how MCP-friendly Vivid is — a whole song, a reactive visual graph, and
the audio→visual bridge, authored in ~60 readable lines.

| demo | vibe | sound (real Surge patches) | visual (REAL vertex geometry) |
|------|------|---------------------------|-------------------------------|
| **pulse** | techno / acid, 128 BPM | *Acid Bassline* + *Sync Pluck*, 4-on-floor | hard-geometric: **ShapeGrid** hexagons + a solid **Mesh** octahedron (add) |
| **drift** | ambient / cinematic, 70 BPM | *Verb Pad* + Surge reverb, *Bell Keys* | Swiss/typographic: a **VectorText** "DRIFT" title over a radial **Gradient** |
| **neon** | synthwave, 100 BPM | *Sync Pluck* arp + *Square Bass*, backbeat | retro-vector: cyan **Lines** rings + a magenta wireframe **Mesh** → Feedback glow |
| **grid** | glitch / IDM, 90 BPM | *Digi* lead (bitcrushed) + *FM Bass* | technical/wireframe: a teal **Lines** grid + a wireframe **Mesh** icosahedron |

Each part's timbre is a **real Surge factory patch** chosen through the generic preset flow
(`list_presets` → pick by name → `load_preset`). Every demo's picture is built from **real
vertex geometry** — vertex-buffer ops (**ShapeGrid** / **Lines** / **VectorText** / **Mesh**),
composited and (sparingly) run through **Feedback**/**Blur** for glow — not fullscreen shader
fields. Every geometry param is wired to an audio characteristic through the bridge, so the
picture moves with the music: solids punch on the kick, grids jitter on the hats, rings roll
with the bass.

## Requirements
- The app running (`app/build/vivid.app/Contents/MacOS/vivid`) — it serves the control
  server on `127.0.0.1:9876`.
- **[Surge XT](https://surge-synthesizer.github.io/)** installed as a CLAP plugin (free /
  open-source) — the melodic parts use it (Vivid now hosts CLAP). EZdrummer 3 plays the drums.

## Run one
```sh
uv run examples/demos/pulse.py     # or drift.py / neon.py / grid.py
```
Each script clears the session, authors the song + visual graph + bridge, starts playback,
and saves a loadable project under `projects/<name>/`. Load a saved one anytime with the
MCP tool `load_project("<abs>/examples/demos/projects/pulse")`.

## Then edit it live (the MCP-friendly part)
With a demo playing, try these over MCP (or the control server) — changes are instant:

- **Reharmonize** — `set_progression(track, scene, ["ii","V","I"], key="C")`
- **Reshape the geometry** — `set_node_param(<mesh>, "shape", 1.0)` swaps the solid
  (cube/tetra/octa/icosa), `"wireframe"` toggles solid↔wireframe; on a `Lines` node `"mode"`
  switches grid/radial/rings. Structure is baked, so change it directly (don't map audio to it).
- **Re-map the bridge** — `connect_mapping("master.high", "node:<mesh>.spin")` to drive a
  different geometry param from a different band.
- **Browse presets** — `list_presets(track, filter="pad")` returns matching Surge patches by
  name; `load_preset(track, id)` loads one. Pick by the sound you want ("warm", "bass",
  "lead", "bell"); the agent guides the choice. (Generic — no per-plugin code.)
- **Repatch the synth** — or `set_track_clap_instrument(track, ".../Surge XT.clap")` and set
  params by index (filter cutoff = 319, amp release = 334), or open Surge's own UI.
- **Add space** — `add_track_clap_effect(track, ".../Surge XT Effects.clap")` and set the
  FX Type param.

## Files
```
vivid_demo.py     # the shared MCP client + music/visual/Surge helpers
pulse.py …        # one builder per demo (author + save)
text/drift.txt    # the vector-type title string for drift (copied into its saved project)
projects/         # saved loadable projects (git-ignored; regenerate by running a builder)
```
