# Phase 4 — Reference-Translation Test Run: Findings

**Date:** 2026-04-20
**Reference:** `https://www.youtube.com/watch?v=lMhY4Kq_4k0` (HOJO Session, 2023 Public Visuals Tokyo) — projection wall with horizontal cool-palette bands, mirrored "W" letterforms, scanlines, drum-driven character.
**Brief:** Imitate-closely + drum-driven audio.
**Output graph:** `graphs/test_runs/projection_wall_av.json`

## TL;DR

The full reference-translation loop ran end-to-end on a real YouTube source. Hit the target character (horizontal cool-palette banded projection wall + drum-driven envelope-followed bloom modulation + 4-bar `bar_sync` on the DrumSequencer). Three real bugs and three usability gaps surfaced — all the kind that *only* show up under live use, not on a checklist.

## What worked (the loop is real)

1. **`fetch_reference(YouTube URL)`** returned 5 frames as planned. Two were the target performance shot, three were the unrelated talk segment — multi-frame fetch correctly surfaced both.
2. **`list_reference_graphs(pattern_filter="drum-driven-pulse")`** found `showcase_demo` immediately as the structural template for the audio + envelope-follower side.
3. **`compare_output_to_reference`** kept the iteration loop tight — capture, Read both PNGs, decide, tweak, repeat. Used it ~7 times. The "no automated score" stance felt right; me-as-judge worked.
4. **`diagnose_composition_issue(intent="drum-driven")`** correctly classified the final graph as healthy (`onset_response_rate=0.62`, latency 146 ms) despite low Pearson correlations — the per-band + onset lenses successfully avoided the Phase 1 false-negative trap.
5. **`bar_sync` param on DrumSequencer** (shipped earlier today) plugged in cleanly: set `seq.bar_sync = 3` (4 bar) and the pattern locks to phrase boundaries.

## Bugs found

### B1. `scaffold_operator` MCP wrapper sends wrong field name (HIGH severity — blocks custom-operator branch of the workflow)

The Python wrapper in both `vivid_mcp.py:2642` and `vivid_opdev_mcp.py:1026` posted `{"name", "env", "variant"}` but the runtime handler at `src/runtime/control/control_server_dispatch.cpp:731` expects `{"name", "kind", "variant"}`. Every scaffold call returned `{"error": "missing 'kind'"}`. Fixed in this session by renaming `env → kind` in the body dict — but the live MCP wrapper doesn't pick up the fix until session restart, so the test-run had to bypass scaffolding and hand-author the operator files directly.

This is exactly the kind of contract drift our "References first" loop encourages: if you don't actually run the loop, this bug stays invisible.

### B2. New-operator hot-reload doesn't include never-seen plugins

Hot-reload watches `.dylib` mtime and refreshes existing operators. Adding a brand-new plugin (`color_bands.dylib` was a fresh add) requires a full runtime restart for the operator to appear in the registry. `add_node` returned `unknown operator type 'ColorBands'` until I `stop_runtime` + `ensure_runtime`'d. Documented behavior in `docs/runtime/hot_reload.md`? Worth checking, and worth a `rescan_operators` MCP if it doesn't exist.

### B3. `save_graph` + `load_graph` snapshots in-progress experimental params

Saved the graph mid-experimentation with `mirror.mode=3` (Kaleidoscope, segments=16) for diagnostic purposes. After restart + `load_graph`, those values came back exactly — and silently produced a bright pink sunburst when I'd expected horizontal stripes. **Not a bug**, but a UX lesson: there's no "reset to defaults" or "what was the user's last saved-good state" notion. For long iteration sessions, a "save snapshot" / "save checkpoint" distinction would help.

## Usability gaps (worth folding back)

### G1. `recommend_starting_point` misfires on operator goals

Asked it about a "GPU operator that emits N color bands across the canvas" — got `clone_example` recommendation pointing to the **audio** `noise` operator, with the reason "Your goal mentions the existing operator 'noise' in audio." The recommender naively pattern-matches keywords and ignores domain. Should at least filter by `env` from goal text, or fall back to "no clear starting point — use `list_example_operators`."

### G2. `WgslFilterBase` is documented but unused by any seed operator

`get_operator_api_docs("gpu")` strongly recommends `WgslFilterBase` for "most filters" and shows a clean 20-line example. But `grep -r WgslFilterBase operators/` returns zero hits — every actual seed operator uses the heavier `OperatorBase + GpuProcessable + manual lazy_init` pattern (the noise.cpp template). When I tried to follow the docs, I had no real-world reference to crib from for things like "how does collect_ports look for a source-only filter (no inputs)?". Either land 1–2 seed operators on `WgslFilterBase` to make it real, or drop the recommendation.

### G3. No "Stripes" / "ColorBands" / "Gradient2" generator existed

Plenty of texture sources (`NoiseTexture`, `Gradient`, `MetronomeViz`) but none can emit *N discrete colored bands*. `Gradient` is a single linear ramp (and grayscale, so HSV can't tint it because HSV rotates *existing* hues). I had to write `ColorBands` to fill this. Now in the seed catalog as `operators/gpu/color_bands/color_bands.cpp` — useful well beyond this one reference.

### G4. `HSV.hue_shift` semantics aren't obvious from the param description

Spent two iterations trying to "tint a grayscale gradient cyan" via HSV before realizing hue-shift rotates *existing* hue and a grayscale source has no hue to rotate. Param description says `"hue_shift"` with no doc string. A one-line note ("rotates existing hues; use Levels or LutApply to colorize a grayscale source") would have saved the round-trip.

## What's now in the catalog

- `operators/gpu/color_bands/color_bands.cpp` — new seed operator. Source-only WGSL fragment shader, 6-slot RGB palette, horizontal/vertical orientation, scroll, soft edges. Defaults to a cool magenta/cyan/violet/white palette pre-tuned for projection-wall aesthetics. (Could also serve a future `MetronomeBands` chord/phrase visualizer.)
- `graphs/test_runs/projection_wall_av.json` — the assembled test-run graph: ColorBands → Mirror → Scanlines → Bloom + DrumSequencer (bar_sync=4) → kick/snare/hat → SmoothFr envelope followers driving bloom intensity / mirror axis / scanline intensity. Useful as a starter template for similar "wall projection" briefs.

## Recommended follow-ups (ranked)

1. **Land the MCP fix** (B1) on master — already edited in working copy, both wrappers. Without this, every future user who tries `scaffold_operator` hits the same dead-end.
2. **Add a `rescan_operators` MCP tool** (B2) — short of full restart, give the runtime a way to scan for newly-built dylibs.
3. **Make `WgslFilterBase` real** (G2) — pick 1–2 simple existing operators (Edge, Levels, Posterize?) and rewrite on top of `WgslFilterBase` so the docs have actual reference implementations.
4. **Tune `recommend_starting_point`** (G1) to at least respect `env` keyword from the goal text.
5. **One-line param descriptions on `HSV`** (G4) — docs + opdev catalog.

## Roadmap status

Pre-test-run, `09-final-infrastructure.md` ended with: "Ready to exercise on a real task. Known instruction gaps can be fixed on the fly in the MCP server instructions string — the harder infrastructure work is done."

Test run confirms: the harder infrastructure works. The remaining gaps are surface-level (wrapper field name, missing tool, doc gaps) — all fixable in a single small follow-up pass. The compelling-demos roadmap can be considered Phase-4-validated.
