# WavetableOsc editor — future features

Companion to `../wavetable-osc.md` (the v1 plan). Everything in this folder is deliberately **deferred from v1**. The v1 editor ships with three regions — family/member grid, waveform preview canvas, unison scatter — plus a side panel with amplitude / warp / cursor readout. Each feature below is a substantial addition on top of that.

Each file captures: what the feature does, why it's deferred, engine/editor cost, interactions with other deferred items, and a rough scope estimate.

## Recommended ordering

If we come back for another round, ship in roughly this order:

| # | Feature | Why first | Estimated effort |
|---|---|---|---|
| 1 | [frame-stack-visualization](frame-stack-visualization.md) | Biggest wow-factor for least effort — gives users a real mental model of the wavetable across position | ~3h |
| 2 | [audition](audition.md) | Unlocks iterative authoring (otherwise you need a running graph to hear edits) | ~2h + platform extension |
| 3 | [warp-preview-overlays](warp-preview-overlays.md) | Makes the 8 warp modes legible | ~3h |
| 4 | [file-drop-import](file-drop-import.md) | Daily-ergonomics win, reusable across operators | ~2h + platform extension |
| 5 | [live-monitoring](live-monitoring.md) | Mod-input preview + output scope + MPE — once the editor context exposes live lane state, all three land together | ~4h + platform extension |
| 6 | [expanded-browser](expanded-browser.md) | Per-member preview glyphs in a family tree | ~4h |
| 7 | [spectrum-view](spectrum-view.md) | Frequency-domain toggle on the preview | ~3h |
| 8 | [per-voice-unison](per-voice-unison.md) | Break unison symmetry | ~6h, operator surface change |
| 9 | [morph-recording](morph-recording.md) | Keyframe automation for position/warp | ~6h, operator surface change |
| 10 | [detail-panels](detail-panels.md) | Phase/drift/interaction clusters get their own sub-panels | ~3h each, build on demand |

## Polish items (not worth their own files)

- **Scatter voice tooltips** — hover a dot in the unison scatter to see exact cents / pan / phase offset. Pure UI, ~30 lines.
- **Keyboard shortcuts cheatsheet overlay** — `?` opens a full-window legend. Once several editors have non-trivial shortcuts, centralize this.
- **Browser favorites** — pin favourite family/member slots to the top of the grid. Needs persistent state per node instance.
- **"Randomize" button** — roll warp_mode + warp_amount + position + drift in a musical distribution. Popular in Serum/Vital.
- **Preset recall shortcuts** — arrow keys in the grid cycle through family/member; would play nicely with A/B slots if those arrive.

## What's NOT in scope anywhere

Cthulhu's chord-memorizer half is a different operator entirely — not a WavetableOsc feature. Similarly:
- Granular mode (time-stretch, position scan) — that's a Granular operator, not an addition here.
- Physical-modelling (Karplus-Strong, waveguide) — different operator class.
- FM matrix (6-op DX7 style) — the Interaction mode's FM is a two-op approximation; full FM is its own territory.

## Architectural notes

- **Platform extensions** (audition, file-drop, live-monitoring) all touch `VividEditorContext` or `VividEditorHostAPI`. Whatever lands there should be designed for reuse by *any* editor, not just WavetableOsc. Don't add `vt_audition(...)`; add a generic `ctx.host.play_note(...)` that sampler, granular, and every future synth editor can call.
- **Operator-surface changes** (per-voice unison, morph recording) need a backward-compat plan. Follow DrumSequencer's A/B pattern: new params append past existing indices; old graphs load with defaults that replicate today's behavior.
- **Visualization cost**: Frame-stack rendering, spectrum view, and live scopes all sample the wavetable or FFT audio. Budget per-frame CPU carefully — the editor already redraws at ~60fps during drags.
- **Test seams**: New helpers go into `wavetable_osc_editor_shared.{h,cpp}` (already established). Per-feature tests follow the v1 pattern: pure-logic helpers in one test suite, end-to-end keyboard/mouse flows in another.
