# Vivid demos — four songs, authored over MCP

Four complete audio-visual scenes, each **built entirely by a Python script that drives
Vivid's control server** (the same backend the MCP bridge speaks). The script *is* the
point: it shows how MCP-friendly Vivid is — a whole song, a reactive visual graph, and
the audio→visual bridge, authored in ~60 readable lines.

| demo | vibe | sound (real Surge patches) | visual (custom + built-in + primitives) |
|------|------|---------------------------|------------------------------------------|
| **pulse** | techno / acid, 128 BPM | *Acid Bassline* + *Sync Pluck*, 4-on-floor | `pulse_tunnel.glsl` → **Kaleidoscope** → Feedback → Blur |
| **drift** | ambient / cinematic, 70 BPM | *Verb Pad* + Surge reverb, *Bell Keys* | `drift_flow.glsl` → Tint → **Gradient × Composite** (vignette) |
| **neon** | synthwave, 100 BPM | *Sync Pluck* arp + *Square Bass*, backbeat | `neon_grid.glsl` + a **Shape** sun → Feedback → Blur |
| **grid** | glitch / IDM, 90 BPM | *Digi* lead (bitcrushed) + *FM Bass* | `grid_glitch.glsl` → **Transform** (tile) → Feedback |

Each part's timbre is a **real Surge factory patch** chosen through the generic preset flow
(`list_presets` → pick by name → `load_preset`). Every demo mixes **built-in ops** (Feedback /
Blur / Tint), a bespoke **CustomShader** `.glsl` you can edit live, and the **primitive ops**
(Shape / Gradient / Composite / Transform / Kaleidoscope), all wired to audio characteristics
through the bridge so the picture moves with the music.

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
- **Swap the visual** — point the CustomShader at a different file:
  `set_node_asset(<cs_node>, "grid_glitch.glsl")`, or edit the `.glsl` in `shaders/` and it
  reloads.
- **Re-map the bridge** — `connect_mapping("master.high", "node:<cs>.warp")` to drive a
  different visual param from a different band.
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
shaders/*.glsl    # the bespoke CustomShader sources (copied into each saved project)
projects/         # saved loadable projects (git-ignored; regenerate by running a builder)
```
