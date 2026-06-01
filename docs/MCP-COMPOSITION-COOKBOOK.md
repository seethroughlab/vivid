# MCP Composition Cookbook

How to build a piece of music by driving the Vivid graph through the MCP server,
end to end. This is the human-readable twin of the `get_authoring_guide()` MCP
tool — call that tool in-session for the same recipe plus live gotchas. The
emphasis here is the order of operations and the non-obvious failure modes that
cost time the first time through.

> An LLM author reads tool docstrings, `operator_docs`, and `get_*_guide` tools
> — not this file. This document exists for humans maintaining the surface; the
> authoritative, in-session copy lives in the tool docstrings and
> `get_authoring_guide()`. Keep them in sync.

## The recipe

1. **Start the runtime.** `ensure_runtime` (launches Vivid if needed). For a
   clean slate after a rebuild, launch empty then `load_graph` — see Pitfalls.

2. **Set the tempo first.** `set_graph_metronome(bpm, beats_per_bar)`. This is
   the *only* tempo control. `set_quantize_clock` is per-node clip/scene launch
   quantization, not BPM. Note positions and tempo-synced effects all derive
   from the graph metronome.

3. **Add instruments.** `add_node(type="Vst3Instrument")` (or a native synth),
   then `set_vst3_plugin(node, ...)` to bind the plugin. Native synths (FmSynth,
   etc.) need no binding.

4. **Load presets** (optional). `list_vst3_presets(node)` then
   `load_vst3_preset(node, id)` — but only for plugins that report
   `loadable=true` (e.g. Pigments). Plugins whose native presets are GUI-only
   (e.g. Serum `.SerumPreset`) report `loadable=false`: open the editor with
   `open_vst3_gui(node)` and pick the preset by hand, then `capture` to confirm.
   Filter out `source=="program"` filler slots. Re-strike held notes after a
   load — loading cuts sounding voices.

5. **Author notes.** Add a `MidiClip` per part. Write notes as JSON via
   `set_string_param(clip, "pattern_data", "<json>")`:
   `[{"p":<pitch 0-127>,"s":<start beat>,"d":<duration beats>,"v":<velocity 0-1>}]`.
   Set `length_bars` and `loop=true` **before** playback (see Pitfalls). Drums:
   use a `DrumSequencer` — step triggers are `kick_0..15`/`snare_0..15`/… params
   (the value is velocity 0..1, not a boolean).

6. **Wire note streams to instruments.** Connect `clip/notes_out` →
   `instrument/<note input>`. DrumSequencer offers a merged `notes_out` plus
   per-drum `kick_out`/`snare_out`/`hat_out`/`oh_out`/`clap_out`/`tom_out`.
   These are note streams (custom-ref ports), never audio.

7. **Wire audio to a Mixer and out.** `instrument/output` → `mixer/input_0`,
   `mixer/input_1`, … (the ports are `input_0..15`, not `input`). Set levels with
   `gain_0..15`, pan with `pan_0..15`. `mixer/output` → `audio_out`.

8. **Check for dropped wires.** `get_graph_errors` after a batch of `connect`s.
   `connect` also returns a `warnings` array when a port name doesn't exist.

9. **Listen / analyze, then save.** `evaluate_audio_musically` (enable a real
   backend first — the default is a stub, see below), `analyze_audio_spectrum`,
   `capture_spectrogram`. Then `save_graph(path)`.

10. **Arrange with scenes** (optional). `save_clip` snapshots a track's params
    but does **not** activate the clip, and `save_scene` captures only *active*
    clips — so the obvious `save_clip → save_scene` sequence yields empty scenes.
    Build scenes explicitly with `set_scene_assignment(scene, track, clip)`.

## Pitfalls (the time-sinks)

- **Tempo:** `set_graph_metronome`, not `set_quantize_clock`.
- **MidiClip loop:** enable `loop` *before* playback. Toggling it on after a
  one-shot has already finished won't restart it. A fresh clip's pattern is the
  literal `"[]"` and emits nothing until filled.
- **Mixer ports:** `input_0..15`, not `input`. A wrong port name returns `ok`
  but drops the wire silently — the `warnings` array and `get_graph_errors`
  catch it.
- **Serum presets:** GUI-load only (`loadable=false`). Don't trust a spectral
  diff to confirm a preset "loaded"; confounds easily. Use the GUI + capture.
- **music eval is a stub by default:** `evaluate_audio_musically` returns canned
  values until you call `configure_music_eval_backend(...)`. The detected key
  *letter* is unreliable even with a real backend.
- **After a full rebuild:** rebuilding an operator re-signs the app bundle and
  clean-shuts a running runtime; launching a graph immediately after can crash
  via a stale hot-reload dylib. Launch empty, then `load_graph`.
- **Stale MCP bridge:** the running bridge process may not expose tools that
  exist in `mcp/vivid_mcp.py` source. If an expected `mcp__vivid__*` tool is
  missing, restart the bridge (or fall back to raw HTTP `POST` to port 9876).
- **Large outputs:** `analyze_audio_spectrum` / `list_vst3_params` /
  `list_vst3_presets` can be large; request summaries / filter client-side.

## See also

- `get_authoring_guide()` — the in-session copy of this recipe.
- `docs/runtime/control_server.md` — the HTTP endpoint catalog (`connect`
  warnings, `set_graph_metronome` vs `set_quantize_clock`).
- `docs/MUSIC-EVAL.md` — evaluation backends and the stub default.
- `docs/INTERFACE.md` — session tracks / clips / scenes model.
- `operator_docs(MidiClip|DrumSequencer|Mixer|Vst3Instrument)` — exact params,
  ports, and pitfalls per operator.
