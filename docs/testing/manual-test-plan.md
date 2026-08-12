# Vivid — Manual Test Plan

A human walkthrough covering **every demo, saved project, per-operator example, tutorial, and
showcase**, plus the core app flows and export paths. Purpose: a release-gate audit (pre-v0.1.2) that
the automated `ctest` + showcase harness can't cover — actually *seeing and hearing* the output.

## How to use this plan

- Work top to bottom; each `- [ ]` is one check. Mark **PASS** / **WARN** / **FAIL** and jot a note.
- **PASS** = looks + sounds as described, no errors. **WARN** = works but off (ugly, quiet, sluggish,
  cosmetic glitch). **FAIL** = crash, silent audio, black/blank output, or an error toast.
- Keep a **Diagnostics panel** open (View menu) and the **log view** visible — the app now surfaces
  operator build failures (`get_health.errored_ops`, red node badge, toast); a black op is a FAIL to
  note, not a mystery.
- Conventions:
  - **Run a demo:** launch the app, then `uv run examples/demos/<name>.py` (talks to the running app
    over the loopback control server; it builds the song + visuals and saves a portable project).
  - **Open a project:** `File > Open` (or `File > Open Example`), pick the project folder.
  - **Launch by direct binary** for a clean run: `build/vivid.app/Contents/MacOS/vivid`
    (`open -a` can run a stale copy). `VIVID_DISCARD_RECOVERY=1` skips the recovery modal.

---

## 0. Environment & launch smoke

- [ ] App launches from the signed release DMG (Gatekeeper: opens without a security block).
- [ ] App launches from `build/vivid.app/Contents/MacOS/vivid` (dev build).
- [ ] Audio device is selected + audible (`View > Audio Output` lists devices; picking one keeps sound).
- [ ] Diagnostics panel shows a green health dot on a fresh session (no `missing_ops` / `errored_ops`).
- [ ] Control server is reachable (`curl -s -X POST 127.0.0.1:9876/status -d '{}'` returns `ok:true`).

## 1. Core app flows (do these once, on a fresh or a demo session)

- [ ] **Transport:** play/stop; BPM change takes effect; the metronome-locked visuals follow tempo.
- [ ] **Session view:** launch a scene/clip; the grid highlights the active cell; per-track level meters move.
- [ ] **Visual graph:** select a node, edit a param (slider) → the output changes live; rewire an edge
      (drag) → output updates; `set_active_output` / clicking a node changes what the viewer shows.
- [ ] **Save / load round-trip:** `File > Save`, quit, relaunch, `File > Open` → audio **and** visuals
      come back identical (drums audible, mappings intact, custom ops recompiled).
- [ ] **Undo/redo:** a param edit + a node add/delete undo and redo cleanly.
- [ ] **MIDI clip editor:** open a clip, add/move/velocity-edit a note, quantize — playback reflects it.
- [ ] **Plugin (VST3/CLAP) hosting:** a track with Surge XT opens its editor; a preset loads; sound changes.

## 2. Demos (`examples/demos/*.py`) — run + watch + hear + round-trip

For **each** demo: run the script, then verify all four:
**(a) builds with no error/traceback · (b) audio plays — all sections if multi-scene · (c) visuals
render and visibly REACT to the audio · (d) the saved project re-opens identical.**

| Demo | What it should show / prove | ✓ |
|------|------------------------------|---|
| `pulse` | 4-on-floor techno; Session View + visual graph together (Showcase #2). | [ ] |
| `neon` | Driving 16th arp + octave bass on curated free plugins; neon look (Showcase #5). | [ ] |
| `mirror` | The bridge runs **both** ways: audio↔visual (Showcase #3). Confirm the return path moves audio. | [ ] |
| `grid` | NONOTAK monochrome geometry; 3 movements cut every 4 bars; breakbeat builds intro→main→peak. | [ ] |
| `spectrum` | Separate visible nodes (AudioSpectrum→lanes) — a per-band 3D equaliser wall. | [ ] |
| `blob` | Notes-as-signal reactive blob (ADR-0041); note events, not authored clips, drive it. | [ ] |
| `bloom` | Melodic parts from note-as-signal generators (generative, not clips). | [ ] |
| `chop` | A real audio break chopped into a playable drum-rack; the chop is audible + reactive. | [ ] |
| `constellation` | Polyphonic note→visual: a Notes node drives points from the track's live notes. | [ ] |
| `crystal` | Shape3D sphere deformed by an animated noise field; morphing crystal. | [ ] |
| `fracture` | A 5-section arrangement as launchable SCENES (intro/verse/chorus/…) — perform the sections. | [ ] |
| `lattice` | Fully 3D-native: a Grid3D of spheres (Shape3D→Instancer3D←InstanceGrid). | [ ] |
| `prism` | WHICH note is playing drives the visual (pitch-addressed, not just energy). | [ ] |
| `signal` | An external **video clip** pulled into the graph and beat-cut. | [ ] |
| `storm` | Particles3D advects ~60k GPU particles through a curl-noise field; flowing ember cloud. | [ ] |

> Perf note: for the multi-section songs (`grid`, `fracture`), launch each scene in turn and confirm
> the build/drop actually changes. `storm` (~60k particles) is the frame-rate stress case — watch fps.

## 3. Saved projects (`examples/demos/projects/*`) — open + play

The 15 demo projects above double as saved fixtures; additionally open the **project-only** ones (no
builder script) and confirm they load + play with no `missing_ops`:

- [ ] `drift` — opens, plays, visuals render.
- [ ] `generative-fields` — opens, plays, visuals render.
- [ ] `geometry` — opens (carries a `vivid-package.json` custom op → compiles on load), renders.
- [ ] `surge-lead` — opens (custom op package), audible Surge lead.
- [ ] `File > Open Example` menu lists the demos + a **Demos/Operators** submenu (per-op examples).

## 4. Per-operator examples (`examples/operators/<Op>/`, 38 of them)

These are one-node teaching projects under **File > Open Example → Demos/Operators**. Spot-check a
representative spread (open, confirm it renders the op's effect, no black frame / error badge):

- [ ] A generator: `CosinePalette`, `Solids`, `NoiseField`, `Lines`.
- [ ] A 3D op: `Shape3D`, `Instancer3D`, `Particles3D`, `Render3D`, `SDF3D`.
- [ ] A transform/effect: `Bloom`, `Blur`, `Feedback`, `Kaleidoscope`, `Displace`, `CRT`.
- [ ] A reactive/audio op: `AudioSpectrum`, `Clock`, `Switch3D`, `StepBars`.
- [ ] An I/O op: `Video`, `Webcam`, `Image`, `MeshLoad`, `Model`.
- [ ] (If time) the full 38 — each opens and renders its op with no `errored_ops`.

## 5. Tutorials (`examples/tutorials/*`) — follow end-to-end

Two parts. The **new-user learning path** — do each as a first-timer would; this doubles as onboarding
QA. Then the **advanced follow-ups**. For every tutorial: follow the numbered steps, confirm each
**✓ You should see/hear** checkpoint actually holds against the shipping UI, and run the
**Try it with MCP** commands. Note any step whose wording no longer matches the interface.

### Learning path (GUI-first)

- [ ] **01 · Meet Vivid** — open `pulse`, play, tour the two surfaces + mappings + diagnostics; the tour
      matches what's on screen. MCP aside (`status` / `get_mappings` / `list_operators`) returns data.
- [ ] **02 · Your first sound** — New project → add an instrument track (Surge XT) → write a 4-bar clip →
      launch + play; you hear the phrase. The MCP aside builds the same phrase.
- [ ] **03 · Your first visual** — add a generator → wire to Output → tweak params → stack an effect; the
      picture updates live, no black frame (or Diagnostics flags it).
- [ ] **04 · Make it react** — map `master.low` → a visual param; play → it pulses with the kick; tune
      amount/attack/release; a note-gate source snaps harder. `get_mappings` shows the mapping.
- [ ] **05 · Perform it** — build intro/main/peak scenes, launch-quantize = 1 bar, launch them live → the
      arrangement builds on the bar.
- [ ] **06 · Make it yours** — add CustomShader + a `.glsl`, edit it live (hot-reload), break it (error
      surfaces, last-good renders), fix it, add a param + map audio to it. `get_operator_authoring_guide`
      returns the guide; `get_health` shows a build failure instead of a silent black frame.
- [ ] **07 · Save & share** — save the project, reopen (round-trips: drums audible, mappings intact,
      custom ops recompile), export WAV + realtime video + deterministic video.

### Advanced follow-ups

- [ ] **`mcp-native-first-project`** (Golden Path A, shader-first, needs Surge XT): follow the steps;
      you end with an audible, visible first project matching the tutorial.
- [ ] **`live-shader-edit`**: edit a project-local `.glsl` while the project runs → the output
      hot-reloads; a broken shader shows the error (last-good keeps rendering) and recovers on fix.
- [ ] **`project-cpp-operator`**: scaffold/build/reload a C++ operator; it appears in `list_operators`
      and renders. (This now also exercises #337 — a bad build surfaces instead of rendering black.)
- [ ] **`free-plugin-starter-list.md`**: the listed free plugins install + load as described.

## 6. Showcase (ADR-0037 gate) — automated + eyeball

- [ ] Run the harness with the app up: `uv run examples/demos/showcase/runner.py` (attach mode) →
      every one of the 5 showcases (`first-project`, `pulse-song`, `mirror-bridge`, `shader-edit`,
      `neon-song`) reports **PASS**; `reports/index.json` is written; exit code 0.
- [ ] Eyeball the 5 captured hero PNGs — each is a real, non-blank, on-brand frame.
- [ ] The **live site** reference (`vivid.seethroughlab.com/reference/`) shows the current op count
      (73 after the reference-regen PR merges).

## 7. Export

- [ ] **Audio:** `File > Export Audio…` → a valid WAV; a hot mix reports `clipped:true` + a warning.
- [ ] **Video (realtime):** `File > Export Video…` → an `.mp4` opens in QuickTime, AV in sync.
- [ ] **Video (deterministic):** `File > Export Video (Deterministic)…` → an `.mp4`; `ffprobe` shows
      frame-count = seconds×fps, even dims, an AAC track; the visuals react to the bounced audio.

## 8. MCP surface (spot-check the agent path)

- [ ] `list_operators` / `find_operators` return the live catalog with per-param semantic hints.
- [ ] `find_operators` on a nonsense query returns an `authoring_suggestion` (the new posture nudge).
- [ ] `get_operator_authoring_guide` returns the when/how-to-author guide.
- [ ] `get_health` reports `errored_ops` (0 on a clean session; >0 if you load a deliberately-broken op).

## 9. Regression / known-instability watch-list

These are documented flaky/edge behaviors — reproduce-and-note, don't be surprised:

- [ ] **VST graph startup race:** a non-deterministic SIGSEGV can occur on launch of a plugin-heavy
      project → relaunch. Note frequency (should be rare).
- [ ] **Live topology-edit on a large running graph** can SIGSEGV — prefer building via JSON + a single
      `load_graph`. Confirm the demo scripts (which do this) don't crash.
- [ ] **Canvas resize shrink:** shrinking the Output resolution mid-session can clip the grid to the
      top-left until relaunch (cosmetic).
- [ ] **Autosave-recovery modal** appears after an unclean quit — it should offer recover/discard and
      not block a normal launch.
- [ ] **Sliced-drum reload** (regression for #336): open a saved demo with chopped drums (`grid`,
      `chop`) → the drums are **audible** (not a silent drone). This was the persistence bug just fixed.

## 10. Sign-off

- [ ] All demos: PASS (or WARNs triaged, no FAILs).
- [ ] All tutorials: PASS.
- [ ] Showcase harness: green.
- [ ] Export paths: PASS.
- [ ] No unexplained `errored_ops` / crashes.
- [ ] **Verdict:** ready to cut v0.1.2 · needs fixes (list below).

_Tester: ________  Date: ________  Build/commit: _________
