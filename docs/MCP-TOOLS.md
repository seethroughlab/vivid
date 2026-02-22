# MCP Tools Reference

Complete catalog of Vivid MCP server tools. Run with `vivid mcp`.

## Project Lifecycle

| Tool | Description |
|------|-------------|
| `run_project` | Launch project in background window, connect via WebSocket |
| `stop_project` | Stop running Vivid instance |
| `create_project` | Create new project from template |
| `bundle_project` | Bundle project as standalone application |
| `list_templates` | List available project templates |
| `list_project_assets` | List assets in project's assets/ folder |

## Build & Reload

| Tool | Description |
|------|-------------|
| `validate_chain` | Compile-check chain.cpp without running |
| `get_runtime_status` | Get connection state, compile errors, runtime errors |
| `get_compile_errors` | Get structured compile errors (file, line, severity, message) |
| `wait_for_reload` | Block until hot-reload completes after editing chain.cpp |

## Introspection

| Tool | Description |
|------|-------------|
| `inspect_chain` | Per-operator metrics + output analysis (brightness, contrast, histogram, spatial, audio). Pass `per_operator_analysis: true` for per-node texture analysis. |
| `get_chain_structure` | Chain topology: operators, types, connections |
| `get_live_params` | Real-time parameter values (optionally filtered by operator) |
| `get_frame_info` | Current frame number, elapsed time, FPS |
| `get_performance_stats` | FPS, frame time, per-operator timing, texture memory |

## Parameter Control

| Tool | Description |
|------|-------------|
| `set_param` | Set parameter on running operator immediately |
| `get_pending_changes` | Get slider changes waiting to be applied to chain.cpp |
| `clear_pending_changes` | Confirm changes were applied (call after editing code) |
| `discard_pending_changes` | Revert parameters to original values |

## Capture & Compare

| Tool | Description |
|------|-------------|
| `capture_frame` | Capture current frame to PNG from running instance |
| `capture_at_frame` | Advance to frame N and capture snapshot |
| `capture_snapshot` | Render project to PNG (spawns new process, no running instance needed) |
| `capture_audio` | Capture audio to WAV with analysis (RMS, peak, spectrum) |
| `sweep_param` | Sweep parameter across values, capturing frames at each step |
| `sweep_param_audio` | Sweep parameter across values, capturing audio at each step |
| `compare_frames` | Compare two PNGs (RMSE, per-channel diff, changed pixels, FrameAnalysis diffs) |
| `compare_audio` | Compare two WAVs (RMS diff, spectral diff, correlation) |
| `export_video` | Export project to video (headless, with optional playback script) |

## Animation & Timing

| Tool | Description |
|------|-------------|
| `advance_frames` | Advance simulation by N frames |
| `reset_time` | Reset animation to frame 0 / time 0 |
| `orbit_camera` | Position camera around a target point |

## Snapshots & Presets

| Tool | Description |
|------|-------------|
| `save_snapshot` | Save current parameters to named snapshot |
| `recall_snapshot` | Apply saved snapshot (optional crossfade) |
| `list_snapshots` | List all saved snapshots for a project |
| `delete_snapshot` | Delete a snapshot |
| `save_preset` | Save parameters to preset file |
| `load_preset` | Load parameters from preset file |

## Solo & Window

| Tool | Description |
|------|-------------|
| `solo_operator` | Solo an operator to see only its output |
| `exit_solo` | Exit solo mode, return to full chain |
| `get_solo_state` | Check if solo mode is active |
| `get_window_state` | Get window configuration (fullscreen, borderless, etc.) |
| `set_window_mode` | Set fullscreen, borderless, always-on-top, cursor visibility |

## Documentation

| Tool | Description |
|------|-------------|
| `list_operators` | List all operators grouped by category |
| `get_operator` | Get operator details: parameters, types, ranges, usage example |
| `get_example` | Get complete working code examples for an operator |
| `get_recipe` | Get or list complete chain.cpp recipe examples |
| `search_docs` | Search Vivid documentation |
| `list_modules` | List installed Vivid modules |

## Visual Analysis

| Tool | Description |
|------|-------------|
| `analyze_color_harmony` | Extract 5-color palette and score harmony (complementary, analogous, triadic) |
| `analyze_symmetry` | Measure horizontal, vertical, and radial symmetry (0-1 scores) |
| `analyze_spatial_balance` | Rule-of-thirds, edge bias, and quadrant balance scoring |
| `analyze_av_reactivity` | Measure audio-visual correlation, onset response, reactivity latency, mutual information |

## Snapshot & Audio Capture Mode

**Visual snapshots**: `--snapshot` runs the chain, saves PNG(s), and exits.
**Audio capture**: `--audio-snapshot` captures audio output to a WAV file.

Visual options:
- `--snapshot <path.png>` — Output path
- `--snapshot-frame <spec>` — Frame(s) to capture (default: 5)

Audio options:
- `--audio-snapshot <path.wav>` — Output path
- `--audio-snapshot-duration <seconds>` — Duration (default: 1)

Frame specification formats:
- `5` — Single frame
- `0,5,10,15` — Specific frames (comma-separated)
- `0-11` — Range (inclusive)
- `0-20:2` — Range with step

When capturing multiple frames, filenames include frame numbers: `output.png` → `output_0000.png`, `output_0001.png`, etc.

## Multi-Sample Inspect

```bash
vivid inspect path/to/project --duration 2 --samples 5
```

- `--duration N` — Capture window in seconds (assumes 60fps)
- `--samples K` — K evenly-spaced inspections across the duration
- `--resolution WxH` — Override render resolution (e.g., `960x540`)
- Output: envelope `{"project", "duration", "sampleCount", "samples": [...]}` with `--duration`; single JSON object without
- With `--out <dir>`, saves `inspection.json`, `snapshot_NNNN.png` per sample, `waveform.png` (if audio)

## Playback Script Event Types

Export scripts (`--script events.json`) support these event types:

| Type | Fields | Description |
|------|--------|-------------|
| `param_set` | `operator`, `param`, `value` | Set parameter instantly |
| `param_ramp` | `operator`, `param`, `from`, `to`, `end_frame` | Linear ramp over frames |
| `key_press` | `key` | Inject key press (e.g. "space", "a") |
| `key_release` | `key` | Inject key release |
| `trigger` | `operator` | Fire a generic trigger |
| `midi_note` | `operator`, `note`, `velocity` | MIDI note on |
| `midi_note_off` | `operator`, `note` | MIDI note off |
| `midi_cc` | `operator`, `cc`, `value`, `channel` | MIDI CC message |
| `mouse_move` | `x`, `y` | Move mouse (normalized 0-1) |
| `mouse_click` | `x`, `y`, `button` | Click mouse (auto-releases next frame) |
| `snapshot_recall` | `value` (index), `valueTo` (duration) | Recall a saved snapshot |
