# Vivid demos — audio-visual scenes, authored over MCP

Complete audio-visual scenes, each **built entirely by a Python script that drives
Vivid's control server** (the same backend the MCP bridge speaks). The script *is* the
point: it shows how MCP-friendly Vivid is — a whole song, a reactive visual graph, and
the bridge, authored in ~60 readable lines.

## The four songs (genre range)

| demo | vibe | sound (real Surge patches) | visual (REAL vertex geometry) |
|------|------|---------------------------|-------------------------------|
| **pulse** | techno / acid, 128 BPM | *Acid Bassline* + *Sync Pluck*, 4-on-floor | hard-geometric: **ShapeGrid** hexagons + a solid **Mesh** octahedron (add) |
| **drift** | ambient / cinematic, 70 BPM | *Verb Pad* + Surge reverb, *Bell Keys* | Swiss/typographic: a **VectorText** "DRIFT" title over a radial **Gradient** |
| **neon** | synthwave, 100 BPM | *Sync Pluck* arp + *Square Bass*, backbeat | retro-vector: cyan **Lines** rings + a magenta wireframe **Mesh** → Feedback glow |
| **grid** | glitch / IDM, 90 BPM | *Digi* lead (bitcrushed) + *FM Bass* | technical/wireframe: a teal **Lines** grid + a wireframe **Mesh** icosahedron |

## The seven mechanism showcases (breadth of the engine)

Where the songs show genre range over one pattern (audio→visual reactivity), these seven each
headline a *distinct Vivid mechanism* that the songs don't exercise. Most lean on bundled `media/` +
native audio; **bloom** needs Surge for its voice, **chop** needs Surge only for its sub (its drums
are the plugin-free native Sampler), and **prism** needs Surge for its melodic voice.

| demo | headlines | sound | visual |
|------|-----------|-------|--------|
| **fracture** | the **glitch pack** over a real **drum machine** | **Cassette Drums** (808 kit) → BeatRepeat → Stutter → Reverse (beat-synced) + Surge acid bass + stabs | a chromatic-split hex **ShapeGrid** → **Transform** → **Displace** → **Feedback** |
| **mirror** | the **bidirectional bridge** — the picture drives the sound back | Cassette Drums + Surge bass + a Surge **pad through an SVFilter**; `viz.feedback → cutoff`, `viz.blur → resonance` (the return leg) | wireframe **Mesh** + **ShapeGrid** → Feedback → Blur |
| **bloom** | **note-as-signal** — the music writes itself, *no clip authored* | three scene-cell generators: **RandMelody** lead + **Euclid** bass (Surge) + a **Euclid** kick (Cassette Drums); an **LFO** breathes the lead cutoff | **Lines** + wireframe **Mesh** + a **note-bloom** Shape that flashes per note |
| **signal** | **external pixels** — a real **video clip** into the reactive chain | Cassette Drums + Surge sub bass drive the treatment | **Video** → **Displace** → **Feedback** |
| **chop** | **sample-slicing** — a real break chopped into a **drum-rack Sampler** + re-sequenced | the bundled `break90.wav` sliced (`slice_to_midi`, 1/16 grid) into a native **Sampler**, re-chopped per section, under a Surge sub — **drums are plugin-free** | a square **ShapeGrid** + solid **Mesh** → **Feedback**, punched per slice hit |
| **prism** | **notes drive the picture directly** — pitch becomes colour (audio analysis can't know *which* note) | a Surge pluck plays an ascending A-minor pentatonic run + a kick | a solid **Mesh** whose hue tracks `track.note` (blue→red by pitch), size tracks `.velocity`, spin flashes on `.gate`; the **ShapeGrid** tracks `master.transient` (loudness) for contrast |
| **constellation** | **polyphonic notes → geometry** — a **Notes** node drives a generic **Instancer** | a Surge pad plays A-minor chords + an arp | a **Notes** source (track's live notes) → **Instancer** draws one instance per held note (pitch → x + colour, velocity → size); chords bloom, the arp trails → **Feedback** |

> **Bidirectional / return leg (mirror):** the `viz.warp/glow/feedback/blur` sources feed a visual
> param's value *back* to an audio param (`connect_mapping` to a `gnode:`/`aparam:`/`param:` dest).
> Drive that visual param from audio first, or a static param sends back a constant.

> **Video (signal):** `media/loop.mp4` is a placeholder (a cellular-automaton "data" clip) — swap in
> your own footage (media root `examples/demos/media`). A Video source currently flips the composited
> output, so signal ships without a type call-sign.

Each part's timbre is a **real Surge factory patch** chosen through the generic preset flow
(`list_presets` → pick by name → `load_preset`). Every demo's picture is built from **real
vertex geometry** — vertex-buffer ops (**ShapeGrid** / **Lines** / **VectorText** / **Mesh**),
composited and (sparingly) run through **Feedback**/**Blur** for glow — not fullscreen shader
fields. Every geometry param is wired to an audio characteristic through the bridge, so the
picture moves with the music: solids punch on the kick, grids jitter on the hats, rings roll
with the bass.

## Requirements
- The app running — either a signed release (`/Applications/Vivid.app`, launched normally) or a dev
  build (`app/build/vivid.app/Contents/MacOS/vivid`). Either serves the control server on
  `127.0.0.1:9876` (set `VIVID_PORT` to change it).
- **[Surge XT](https://surge-synthesizer.github.io/)** — free CLAP synth, the melodic voices
  (bass / lead / pad / stabs) across every demo.
- **[BPB Cassette Drums](https://bedroomproducersblog.com/free-vst-plugins/drums/)** — free VST3
  drum machine (606/808/909/MFB), the drums in all four showcases. Install both `Cassette Drums.vst3`
  and its `Cassette Drums.instruments` folder **together** into `~/Library/Audio/Plug-Ins/VST3/`.
  (The four *songs* additionally use EZdrummer 3 for drums.)

## Run one
```sh
uv run examples/demos/pulse.py       # the songs: pulse / drift / neon / grid
uv run examples/demos/fracture.py    # the showcases: fracture / mirror / bloom / signal / chop
```
Each script clears the session, authors the scene + visual graph + bridge, starts playback,
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
vivid_demo.py     # the shared MCP client + music/visual/Surge helpers (+ the mechanism helpers:
                  #   import_audio, add_glitch, warp, note-graph, map_to_audio, video/webcam)
pulse.py …        # song builders: pulse / drift / neon / grid
fracture.py …     # showcase builders: fracture / mirror / bloom / signal
text/drift.txt    # the vector-type title string for drift (copied into its saved project)
media/            # bundled assets for the showcases: break90.wav + loop.mp4 (placeholders — swap
                  #   for a real break / footage) + the {mirror,signal,bloom}.txt call-signs
projects/         # saved loadable projects (git-ignored; regenerate by running a builder)
```
