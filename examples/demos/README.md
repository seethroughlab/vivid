# Vivid demos — four songs, authored over MCP

Four complete audio-visual scenes, each **built entirely by a Python script that drives
Vivid's control server** (the same backend the MCP bridge speaks). The script *is* the
point: it shows how MCP-friendly Vivid is — a whole song, a reactive visual graph, and
the audio→visual bridge, authored in ~60 readable lines.

| demo | vibe | sound | visual (custom + built-in) |
|------|------|-------|----------------------------|
| **pulse** | techno / acid, 128 BPM | Surge acid bass + stabs, 4-on-floor | `pulse_tunnel.glsl` (acid tunnel) → Feedback → Blur |
| **drift** | ambient / cinematic, 70 BPM | Surge pad + Surge reverb, sparkle arp | `drift_flow.glsl` (nebula) → Feedback → Tint |
| **neon** | synthwave, 100 BPM | Surge arp + shimmer, dark bass, backbeat | `neon_grid.glsl` (retro grid) → Feedback → Blur |
| **grid** | glitch / IDM, 90 BPM | bitcrushed Surge lead, Euclidean beat | `grid_glitch.glsl` (datamosh) → Feedback → Blur |

Every demo mixes **built-in visual ops** (Feedback / Blur / Tint / Output) with a **custom
op** (a bespoke CustomShader `.glsl` you can edit live), and wires audio characteristics to
the visual params through the bridge, so the picture moves with the music.

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
- **Repatch the synth** — `set_track_clap_instrument(track, ".../Surge XT.clap")` then set
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
