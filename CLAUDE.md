# Vivid

WebGPU-based creative coding framework with hot-reload. Minimal core + optional libraries.

## Build Commands

```bash
cmake -B build && cmake --build build    # Full build
cmake --build build                       # Incremental build
./build/bin/vivid projects/getting-started/02-hello-noise    # Run project
doxygen Doxyfile                          # Generate API docs
```

## Framework Development

When working on Vivid's own C++ codebase (not user chain.cpp projects), use this workflow.

### Test Commands

```bash
ctest --test-dir build -L "unit|integration" --output-on-failure   # Fast feedback (~7s)
ctest --test-dir build -L "unit|integration|build|mcp|cli" --output-on-failure  # Full non-GPU suite
ctest --test-dir build --output-on-failure                          # Everything including GPU tests
ctest --test-dir build -R "Noise" --output-on-failure               # Single test by name
```

### Test Labels

| Label | What it covers | Speed |
|-------|---------------|-------|
| `unit` | Operator params, effects, audio analysis, particles, GUI layout, synth/network/serial modules | ~5s |
| `integration` | Chain assembly | ~2s |
| `build` | Version check, library existence | <1s |
| `mcp` | MCP JSON-RPC protocol, operator metadata | ~20s |
| `cli` | `vivid build/params/graph/docs/inspect/export/check` | ~60s |
| `smoke` | Example projects run without crashing (requires GPU) | ~30s |
| `visual` | Pixel comparison against reference images (requires GPU) | ~60s |
| `gui` | GUI visual regression with scripted interactions (requires GPU) | ~30s |
| `batch` | Batch compile all projects + generate operator coverage index | ~4min |
| `qualitative` | Assert visual/audio output correctness via `vivid check` (requires GPU) | ~3min |

### Inner Loop (Framework)

```
Edit C++ source
    → cmake --build build              — incremental build (~5-15s)
    → ctest -L "unit|integration"      — fast tests (~7s)
    → iterate or commit
```

### Before Pushing

Run the full non-GPU suite — this is what CI runs on all platforms:

```bash
ctest --test-dir build -L "unit|integration|build|mcp|cli" --output-on-failure
```

GPU-dependent tests (`smoke`, `visual`, `gui`) run in CI on Linux with xvfb but are `continue-on-error`. Run locally on macOS if changing rendering code.

### Batch Build Verification

Verify all example projects and test fixtures still compile:

```bash
ctest --test-dir build -L batch --output-on-failure
```

### Operator Coverage

After running batch build, check which projects use a specific operator:

```bash
cat build/operator-coverage.json | python3 -c "import sys,json; d=json.load(sys.stdin); print('\n'.join(d.get(sys.argv[1],[])))" Bloom
```

Use this when modifying an operator to find projects to test against.

### Qualitative Output Tests

Verify visual and audio output correctness across projects with `vivid-assertions.json`:

```bash
ctest --test-dir build -L qualitative --output-on-failure -j1
```

Projects with `vivid-assertions.json` are automatically discovered and tested. To add assertions to a new project, create a `vivid-assertions.json` file in the project directory (see `projects/telegram-test/vivid-assertions.json` for an example).

## CLI Options

```bash
./build/bin/vivid <project-path>                              # Run normally (stays open for hot-reload)
./build/bin/vivid <project-path> --exit-on-error              # Exit on compile error (agent/CI)
./build/bin/vivid <project-path> --snapshot output.png        # Capture single frame
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-11  # Capture 12 frames for GIF
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-30:5  # Every 5th frame
./build/bin/vivid <project-path> --audio-snapshot out.wav                   # Capture 1s audio
./build/bin/vivid <project-path> --audio-snapshot out.wav --audio-snapshot-duration 5  # 5s audio
./build/bin/vivid build <project-path>                         # Compile chain, report structured JSON errors
./build/bin/vivid check <project-path>                         # Run assertions, exit 0 (pass) or 1 (fail)
./build/bin/vivid check <project-path> --duration 2            # Run for 2s before evaluating assertions
./build/bin/vivid inspect <project-path>                       # Dump inspection JSON to stdout
./build/bin/vivid inspect <project-path> --per-operator        # Include per-operator texture analysis
./build/bin/vivid inspect <project-path> --duration 2 --samples 5  # Multi-sample: 5 snapshots over 2s
./build/bin/vivid inspect <project-path> --resolution 960x540  # Inspect at custom resolution
./build/bin/vivid params <project-path>                        # List all tweakable parameters as JSON
./build/bin/vivid graph <project-path>                         # Dump chain topology (operators, connections) as JSON
./build/bin/vivid export <project-path> -o out.mp4 --duration 10  # Export video (headless)
./build/bin/vivid export <project-path> -o out.mp4 --script events.json  # Export with scripted events
./build/bin/vivid docs search "bloom"                          # Search documentation
./build/bin/vivid docs recipe                                  # List all recipes
./build/bin/vivid docs recipe feedback-loop                    # Show a specific recipe
./build/bin/vivid docs example Noise                           # Code examples for an operator
./build/bin/vivid mcp                                         # Run MCP server for Claude Code
```

## Agent Workflow

Vivid is designed for autonomous LLM iteration using two nested loops.

### Inner Loop (seconds, no human needed)

The fast, autonomous cycle. Edit code, build, inspect structured data, validate assertions, repeat. The LLM reasons about JSON metrics — not pixels.

```
Edit chain.cpp
    → vivid build (or validate_chain MCP)   — MUST pass (exit 0) before continuing
    → inspect_chain (MCP) or vivid inspect  — read metrics JSON
    → vivid check                           — run assertions (exit 0 = pass)
    → iterate or done
```

**Important:** `vivid build` is the only CLI command that exits with a non-zero code on compile failure. Other commands (`vivid inspect`, `vivid check`, `vivid export`) hang indefinitely on errors. Always use `vivid build` as a gate before running them.

**What to look for in inspection data:**
- **Brightness** (`meanBrightness`): 0.0 = black, 1.0 = white. Most visuals should be 0.2–0.8.
- **Contrast** (`contrast`): Std dev of luminance. Near 0 = flat/washed out. 0.15–0.35 typical.
- **Spatial distribution** (`regionBrightness`): 3×3 grid. Detect if content is centered, one-sided, or uniform.
- **Histogram**: 8-bucket luminance distribution. Spikes at extremes = clipping. Even spread = good dynamic range.
- **Texture complexity** (`textureEntropy`): 0 = uniform, 1 = maximal variety. Noise/detail: >0.5. Flat fills: <0.2.
- **Edge density** (`edgeDensity`): Fraction of edge pixels. High-detail scenes: 0.1–0.3. Soft/blurred: <0.05.
- **Sharpness** (`sharpness`): Laplacian variance. Higher = sharper edges. Blurred: <0.01. Crisp: >0.05.
- **Noise level** (`noiseLevel`): Mean absolute Laplacian. High-frequency noise: >0.1. Clean: <0.02.
- **Clipping** (`clipBlackPct`, `clipWhitePct`): Fraction of pixels at pure black/white. Should be <0.05 unless intentional.
- **Dynamic range** (`headroom`, `rangeSpan`): `headroom` = room before white clip. `rangeSpan` = max−min luminance. Low rangeSpan = flat image.
- **Visual center** (`visualCenterX`, `visualCenterY`): Brightness-weighted centroid (0–1). 0.5,0.5 = centered. Detect off-center compositions.
- **Color temperature** (`colorTemperature`): 0 = cool/blue, 0.5 = neutral, 1 = warm/red.
- **Hue diversity** (`uniqueHueCount`, `hueEntropy`): `uniqueHueCount` = hue bins >5% (0–12). `hueEntropy` = 0 (monochrome) to 1 (all hues equal). `hueHistogram.N` (N=0–11) = 12-bin hue distribution in 30° steps.
- **Alpha** (`alphaOpaquePct`, `alphaTransparentPct`, `alphaPartialPct`, `alphaMean`): Detect transparency issues. Fully opaque content: `alphaOpaquePct ≈ 1.0`.
- **Audio RMS** (`rmsLevel`): 0.0 = silence, >0.9 = clipping. Music/drones: 0.1–0.5. Percussive: peaks to 0.7.
- **Spectrum bands** (6 bands: subBass, bass, lowMid, mid, highMid, high): Verify frequency content matches intent.
- **Crest factor** (`crestFactor`): Peak/RMS ratio. High = percussive/dynamic. Low = compressed/steady.

**Temporal metrics** (multi-sample inspect with `--duration`):
- **Flicker** (`temporal.flickerScore`): 0 = stable, 1 = rapid oscillation. Alternating frames: >0.8. Smooth animation: <0.2.
- **Convergence** (`temporal.isConverged`, `temporal.convergenceScore`): Detects when output has stabilized. Feedback loops should converge; `isConverged = true` after output stops changing.
- **Motion** (`temporal.motionMagnitude`): Average per-pixel change. 0 = static. Animated content: 0.01–0.2. `temporal.regionMotion.N` (N=0–8) = per-region motion (3×3 grid).
- **Frozen** (`temporal.isFrozen`): True when all recent frames are identical. Should be false for animated content.
- **Loop detection** (`temporal.isLooping`, `temporal.loopPeriodFrames`, `temporal.loopConfidence`): Detects repeating brightness patterns via autocorrelation. Useful for verifying cyclic animations.
- **Novelty** (`temporal.noveltyScore`, `temporal.noveltyTrend`): Distance from current frame to stored keyframes. 0 = repetitive, higher = exploring new visuals. `noveltyTrend` > 0.5 = increasingly novel, < 0.5 = collapsing toward repetition.

**Audio evaluation tools:**
- **Assertions** (`audio.*` paths): Validate audio properties in `vivid check`. Example paths: `audio.rmsLevel`, `audio.peakLevel`, `audio.spectrum.bass`, `audio.isSilent`, `audio.crestFactor`.
- **`capture_audio` / `compare_audio`**: Capture WAV + analysis, then compare before/after to measure changes.
- **`sweep_param_audio`**: Sweep a parameter across values, capturing audio at each step. Returns per-step RMS, spectrum, WAV files.
- **Export sidecar**: `vivid export --audio` produces `<output>.audio-analysis.json` with summary + per-second time-series.

Example assertions (`vivid-assertions.json`):
```json
{"name": "brightness-ok", "path": "output.meanBrightness", "op": "between", "value": [0.2, 0.8]}
{"path": "output.contrast", "op": ">", "value": 0.15, "after_frame": 30, "message": "Contrast stabilizes after warmup"}
{"path": "output.textureEntropy", "op": ">", "value": 0.3, "message": "Not a flat fill"}
{"path": "output.clipBlackPct", "op": "<", "value": 0.5, "message": "Not mostly black"}
{"path": "output.sharpness", "op": ">", "value": 0.01, "message": "Has visible edges"}
{"path": "output.colorTemperature", "op": "between", "value": [0.3, 0.7], "message": "Neutral tones"}
{"path": "output.hueHistogram.0", "op": ">", "value": 0.1, "message": "Has red hues"}
{"path": "output.alphaOpaquePct", "op": "==", "value": 1.0, "message": "Fully opaque output"}
{"path": "operators.bloom.textureAnalysis.meanBrightness", "op": ">", "value": 0.1}
{"path": "operators.bloom.textureAnalysis.edgeDensity", "op": ">", "value": 0.01, "message": "Bloom has detail"}
{"path": "audio.rmsLevel", "op": ">", "value": 0.01, "message": "Audio not silent"}
{"path": "audio.spectrum.bass", "op": ">", "value": 0.05, "message": "Should have bass"}
{"path": "audio.peakLevel", "op": "<", "value": 0.95, "message": "No clipping"}
{"path": "audio.spectrum.bass", "op": ">", "value": 0.4, "when_path": "operators.kick.metrics.is_playing", "when_check": "==", "when_value": 1.0}
```

Temporal assertions (require `--duration` for multi-sample inspect):
```json
{"path": "temporal.isFrozen", "op": "==", "value": 0, "message": "Animation is running"}
{"path": "temporal.flickerScore", "op": "<", "value": 0.3, "message": "No unwanted flicker"}
{"path": "temporal.isConverged", "op": "==", "value": 1, "message": "Feedback loop stabilized"}
{"path": "temporal.motionMagnitude", "op": ">", "value": 0.01, "message": "Has visible motion"}
{"path": "temporal.isLooping", "op": "==", "value": 1, "message": "Animation loops correctly"}
{"path": "temporal.noveltyScore", "op": ">", "value": 0.02, "message": "Visuals are evolving"}
```

**Assertion features:**
- **`name`** (optional): Human-readable label shown in verbose output and JSON (e.g. `"name": "feedback-alive"`)
- **`between` operator**: Range check with array value `[low, high]`, inclusive on both ends
- **`exists` / `not_exists` operators**: Check path presence without comparing values (no `value` field needed)
- **`operators.<name>.textureAnalysis.<field>`**: Assert on per-operator texture analysis (auto-enables GPU readback). Supports all FrameAnalysis fields: `meanBrightness`, `contrast`, `dominantHue`, `saturationAvg`, `dominantColor.N`, `regionBrightness.N`, `histogram.N`, `textureEntropy`, `edgeDensity`, `avgGradientMag`, `clipBlackPct`, `clipWhitePct`, `headroom`, `rangeSpan`, `sharpness`, `noiseLevel`, `visualCenterX`, `visualCenterY`, `colorTemperature`, `hueHistogram.N` (0–11), `uniqueHueCount`, `hueEntropy`, `alphaOpaquePct`, `alphaTransparentPct`, `alphaPartialPct`, `alphaMean`
- **`temporal.*`**: Assert on temporal metrics (requires `--duration` for multi-sample capture). Fields: `flickerScore`, `flickerFrequency`, `frameDelta`, `convergenceScore`, `isConverged`, `motionMagnitude`, `regionMotion.N` (0–8), `frameDiversity`, `isFrozen`, `isLooping`, `loopPeriodSeconds`, `loopPeriodFrames`, `loopConfidence`, `noveltyScore`, `noveltyTrend`, `keyframeCount`. Bool fields (`isConverged`, `isFrozen`, `isLooping`) resolve to 0.0/1.0.
- **`after_frame`** (optional): Skip assertion if current frame < value. Useful for warmup periods (e.g. feedback loops).
- **`when_path` / `when_check` / `when_value`** (optional): Conditional guard — assertion is only evaluated when the guard condition is met. Uses same dot-path format and operators. Skipped assertions show as `SKIP` and don't affect pass/fail.

### Outer Loop (minutes, human review)

When the inner loop is satisfied, export media for subjective human review:

```
vivid build path/to/project               — gate check (must pass before export)
vivid export path/to/project -o /tmp/preview.mp4 --duration 15
    → send video to user
    → user provides subjective feedback
    → back to inner loop
```

The outer loop runs infrequently — once for every 5–20 inner loop iterations.

**Do NOT export video after every change.** The inner loop (build → inspect → check) gives you enough structured data to evaluate changes autonomously. Only export when you've iterated to a point worth showing the user. Exporting is slow — unnecessary exports waste time.

## MCP Server (Claude Code Integration)

The `vivid mcp` command runs an MCP server that Claude Code can use for live integration with Vivid.

### Setup
Add to your Claude Code MCP config (`~/.claude.json`):
```json
{
  "mcpServers": {
    "vivid": {
      "command": "/path/to/vivid",
      "args": ["mcp"]
    }
  }
}
```

### Available MCP Tools

#### Project Lifecycle
| Tool | Description |
|------|-------------|
| `run_project` | Launch project in background window, connect via WebSocket |
| `stop_project` | Stop running Vivid instance |
| `create_project` | Create new project from template |
| `bundle_project` | Bundle project as standalone application |
| `list_templates` | List available project templates |
| `list_project_assets` | List assets in project's assets/ folder |

#### Build & Reload
| Tool | Description |
|------|-------------|
| `validate_chain` | Compile-check chain.cpp without running |
| `get_runtime_status` | Get connection state, compile errors, runtime errors |
| `get_compile_errors` | Get structured compile errors (file, line, severity, message) |
| `wait_for_reload` | Block until hot-reload completes after editing chain.cpp |

#### Introspection
| Tool | Description |
|------|-------------|
| `inspect_chain` | Per-operator metrics + output analysis (brightness, contrast, histogram, spatial, audio). Pass `per_operator_analysis: true` for per-node texture analysis. |
| `get_chain_structure` | Chain topology: operators, types, connections |
| `get_live_params` | Real-time parameter values (optionally filtered by operator) |
| `get_frame_info` | Current frame number, elapsed time, FPS |
| `get_performance_stats` | FPS, frame time, per-operator timing, texture memory |

#### Parameter Control
| Tool | Description |
|------|-------------|
| `set_param` | Set parameter on running operator immediately |
| `get_pending_changes` | Get slider changes waiting to be applied to chain.cpp |
| `clear_pending_changes` | Confirm changes were applied (call after editing code) |
| `discard_pending_changes` | Revert parameters to original values |

#### Capture & Compare
| Tool | Description |
|------|-------------|
| `capture_frame` | Capture current frame to PNG from running instance |
| `capture_at_frame` | Advance to frame N and capture snapshot |
| `capture_snapshot` | Render project to PNG (spawns new process, no running instance needed) |
| `capture_audio` | Capture audio to WAV with analysis (RMS, peak, spectrum) |
| `sweep_param` | Sweep parameter across values, capturing frames at each step |
| `sweep_param_audio` | Sweep parameter across values, capturing audio at each step |
| `compare_frames` | Compare two PNGs (RMSE, per-channel diff, changed pixels) |
| `compare_audio` | Compare two WAVs (RMS diff, spectral diff, correlation) |
| `export_video` | Export project to video (headless, with optional playback script) |

#### Animation & Timing
| Tool | Description |
|------|-------------|
| `advance_frames` | Advance simulation by N frames |
| `reset_time` | Reset animation to frame 0 / time 0 |
| `orbit_camera` | Position camera around a target point |

#### Snapshots & Presets
| Tool | Description |
|------|-------------|
| `save_snapshot` | Save current parameters to named snapshot |
| `recall_snapshot` | Apply saved snapshot (optional crossfade) |
| `list_snapshots` | List all saved snapshots for a project |
| `delete_snapshot` | Delete a snapshot |
| `save_preset` | Save parameters to preset file |
| `load_preset` | Load parameters from preset file |

#### Solo & Window
| Tool | Description |
|------|-------------|
| `solo_operator` | Solo an operator to see only its output |
| `exit_solo` | Exit solo mode, return to full chain |
| `get_solo_state` | Check if solo mode is active |
| `get_window_state` | Get window configuration (fullscreen, borderless, etc.) |
| `set_window_mode` | Set fullscreen, borderless, always-on-top, cursor visibility |

#### Documentation
| Tool | Description |
|------|-------------|
| `list_operators` | List all operators grouped by category |
| `get_operator` | Get operator details: parameters, types, ranges, usage example |
| `get_example` | Get complete working code examples for an operator |
| `get_recipe` | Get or list complete chain.cpp recipe examples |
| `search_docs` | Search Vivid documentation |
| `list_modules` | List installed Vivid modules |

#### Visual Analysis
| Tool | Description |
|------|-------------|
| `analyze_color_harmony` | Extract 5-color palette and score harmony (complementary, analogous, triadic) |
| `analyze_symmetry` | Measure horizontal, vertical, and radial symmetry (0-1 scores) |
| `analyze_spatial_balance` | Rule-of-thirds, edge bias, and quadrant balance scoring |

### Starting Vivid
The MCP server queries and controls a running Vivid instance. **Start Vivid before using MCP tools:**

```bash
# Minimal output window (use with external editor)
./build/bin/vivid path/to/project

# With built-in devtools (recommended for MCP workflow)
./build/bin/vivid path/to/project --show-ui
```

The `--show-ui` flag enables built-in devtools with:
- **Node Graph** — Always visible; visual chain with live thumbnails
- **Inspector** — Auto-shows when a node is selected, auto-hides on deselection
- **Performance** (`Cmd+1`) — Real-time performance metrics (toggle)
- **Status Bar** — FPS, frame time, resolution, memory, record/snapshot

Press the backtick/tilde key (`` ` ``) to toggle devtools on/off at runtime. Works with or without `--show-ui`.

If Vivid isn't running, MCP tools return `{"connected": false}` with a helpful suggestion.

### Claude-First Workflow
1. User starts Vivid with `--show-ui` or via Vivid IDE
2. Claude connects automatically via MCP (port 9876)
3. User adjusts sliders in the Inspector (preview updates immediately)
4. Claude calls `get_pending_changes` to see what changed
5. Claude edits chain.cpp with the new values
6. Claude calls `clear_pending_changes` to confirm
7. Hot-reload applies the changes
8. **IMPORTANT**: Claude calls `get_runtime_status` to verify compilation succeeded

### Checking Compile Status (Critical!)
After editing `chain.cpp`, **you MUST check if compilation succeeded** before running any other command.

**CLI Compile Check (agent/CI workflows):**
```bash
vivid build path/to/project
# Exit 0 = success, non-zero = failure with error details
# MUST pass before running vivid inspect, vivid check, or vivid export
```

**MCP Compile Check (live instance):**
```
get_runtime_status → check compileStatus.success

# Response when successful:
{"connected": true, "compileStatus": {"success": true}}

# Response when failed:
{"connected": true, "compileStatus": {"success": false, "message": "chain.cpp:42:10: error: ..."}}
```

If compilation failed, read the error message and fix the code before proceeding.

### Introspection & Validation

The inner loop relies on structured data from `inspect_chain` (MCP) or `vivid inspect` (CLI).

**FrameAnalysis** (output texture analysis):
| Field | Type | Description |
|-------|------|-------------|
| `meanBrightness` | float | Average luminance (0–1) |
| `contrast` | float | Std dev of luminance |
| `dominantColor` | [r,g,b] | Most prominent color |
| `dominantHue` | float | Hue (0–1) |
| `saturationAvg` | float | Average saturation |
| `histogram` | int[8] | 8-bucket luminance histogram |
| `regionBrightness` | float[9] | 3×3 spatial brightness grid (top-left → bottom-right) |
| `textureEntropy` | float | Normalized Shannon entropy of 64-bin histogram (0=uniform, 1=max variety) |
| `edgeDensity` | float | Fraction of edge pixels (0–1) |
| `avgGradientMag` | float | Mean gradient magnitude |
| `clipBlackPct` | float | Fraction of pixels near pure black (lum < 0.005) |
| `clipWhitePct` | float | Fraction of pixels near pure white (lum > 0.995) |
| `headroom` | float | 1.0 − maxLuminance (room before white clip) |
| `rangeSpan` | float | maxLuminance − minLuminance (dynamic range) |
| `sharpness` | float | Laplacian variance (higher = sharper) |
| `noiseLevel` | float | Mean absolute Laplacian (high-frequency content) |
| `visualCenterX` | float | Brightness-weighted centroid X (0–1, 0.5=centered) |
| `visualCenterY` | float | Brightness-weighted centroid Y (0–1, 0.5=centered) |
| `colorTemperature` | float | 0=cool/blue, 0.5=neutral, 1=warm/red |
| `hueHistogram` | float[12] | 12-bin hue distribution (30° bins, indexed 0–11) |
| `uniqueHueCount` | int | Hue bins above 5% threshold (0–12) |
| `hueEntropy` | float | Normalized hue entropy (0=monochrome, 1=all hues equal) |
| `alphaOpaquePct` | float | Fraction fully opaque pixels |
| `alphaTransparentPct` | float | Fraction fully transparent pixels |
| `alphaPartialPct` | float | Fraction partially transparent pixels |
| `alphaMean` | float | Mean alpha value (0–1) |

**TemporalAnalysis** (multi-frame metrics, requires `--duration`):
| Field | Type | Description |
|-------|------|-------------|
| `flickerScore` | float | High-frequency brightness oscillation (0=stable, 1=rapid flicker) |
| `flickerFrequency` | float | Dominant flicker frequency in Hz |
| `frameDelta` | float | Most recent per-pixel change between frames |
| `convergenceScore` | float | 0=diverging, 0.5=stable, 1=fully converged |
| `isConverged` | bool | True when output has stabilized (delta < threshold for 8+ frames) |
| `motionMagnitude` | float | Average pixel displacement (0=still) |
| `regionMotion` | float[9] | 3×3 regional motion grid (indexed 0–8) |
| `frameDiversity` | float | Variance of frame deltas (0=frozen or steady) |
| `isFrozen` | bool | True when all recent frames are identical |
| `isLooping` | bool | True when repeating brightness pattern detected |
| `loopPeriodFrames` | int | Detected loop period in frames |
| `loopPeriodSeconds` | float | Detected loop period in seconds |
| `loopConfidence` | float | Autocorrelation confidence (>0.7 = reliable) |
| `noveltyScore` | float | Distance from current frame to keyframes (0=repetitive) |
| `noveltyTrend` | float | 0–1; >0.5=increasingly novel, <0.5=collapsing |
| `keyframeCount` | int | Number of stored keyframes |

**AudioAnalysis** (per audio operator):
| Field | Type | Description |
|-------|------|-------------|
| `rmsLevel` | float | Overall RMS level (0–1) |
| `peakLevel` | float | Overall peak amplitude (0–1) |
| `rmsLeft` / `rmsRight` | float | Per-channel RMS |
| `isSilent` | bool | True if RMS < 0.001 |
| `crestFactor` | float | Peak/RMS ratio (dynamics indicator) |
| `spectrum` | float[6] | 6-band energy: subBass (<60Hz), bass (60–250), lowMid (250–500), mid (500–2k), highMid (2k–4k), high (4k+) |
| `duration` | float | Buffer duration in seconds |

**Sample inspection JSON** (abbreviated):
```json
{
  "frame": 0, "time": 0.0,
  "operators": {
    "noise": { "scale": 4.0, "speed": 0.5 },
    "feedback": { "decay": 0.95, "energy": 0.72, "pixel_change_pct": 18.3 },
    "bloom": { "threshold": 0.6, "bright_pixel_pct": 12.1 }
  },
  "output": {
    "meanBrightness": 0.48, "contrast": 0.22,
    "textureEntropy": 0.61, "edgeDensity": 0.14, "sharpness": 0.032,
    "clipBlackPct": 0.02, "clipWhitePct": 0.0, "rangeSpan": 0.91,
    "visualCenterX": 0.52, "visualCenterY": 0.48,
    "colorTemperature": 0.55, "uniqueHueCount": 4, "hueEntropy": 0.42,
    "histogram": [12, 45, 89, 120, 95, 40, 8, 3],
    "regionBrightness": [0.3, 0.4, 0.3, 0.5, 0.7, 0.5, 0.3, 0.4, 0.3]
  }
}
```

**Multi-sample envelope** (`--duration 2 --samples 5`):
```json
{
  "project": "my-project", "duration": 2, "sampleCount": 5,
  "samples": [ {"frame": 0, "time": 0.0, "output": {"...": "..."}} ],
  "temporal": {
    "flickerScore": 0.05, "isConverged": true, "motionMagnitude": 0.03,
    "isFrozen": false, "noveltyScore": 0.12, "isLooping": false
  }
}
```

**With `per_operator_analysis: true`** — each texture operator includes `textureAnalysis`:
```json
{
  "operators": {
    "noise": {
      "metrics": {"scale": 4.0, "speed": 0.5},
      "metadata": {"type": "Noise", "output_kind": "Texture"},
      "textureAnalysis": {"meanBrightness": 0.51, "contrast": 0.29, "...": "..."}
    },
    "bloom": {
      "metrics": {"threshold": 0.6},
      "metadata": {"type": "Bloom", "output_kind": "Texture"},
      "textureAnalysis": {"meanBrightness": 0.05, "contrast": 0.03, "...": "..."}
    }
  }
}
```
Useful for diagnosing where brightness/contrast drops occur in the chain.

**Comparison tools** (no running instance needed):
- `compare_frames`: RMSE, per-channel diff, changed pixel percentage. Verify visual changes had the intended effect.
- `compare_audio`: RMS diff, spectral diff, correlation. Verify audio changes.
- `sweep_param`: Capture frames across a parameter range. Find optimal values or verify smooth transitions.
- `sweep_param_audio`: Capture audio across a parameter range. Evaluate how audio changes with parameters.

**Assertions** (`vivid check`): Runs the chain and evaluates assertions from `vivid-assertions.json`. Exit code 0 = all pass, 1 = failure. Use in the inner loop to verify invariants. Supported paths: `output.*` (visual), `audio.*` (audio), `temporal.*` (multi-frame, requires `--duration`), `operators.<name>.metrics.<key>` (per-operator metrics), `operators.<name>.textureAnalysis.<field>` (per-operator texture analysis). Operators: `>`, `>=`, `<`, `<=`, `==`, `!=`, `between` (range), `exists`, `not_exists`. Assertions can have an optional `name` field for readable output. Conditional guards: `after_frame` (skip before frame N), `when_path`/`when_check`/`when_value` (skip unless guard condition met). Skipped assertions report as `SKIP` and don't affect pass/fail.

### Snapshot & Audio Capture Mode (for CI/Testing)

**Visual snapshots**: The `--snapshot` flag runs the chain, saves PNG(s), and exits.
**Audio capture**: The `--audio-snapshot` flag captures audio output to a WAV file.

Useful for:
- **Automated testing**: Verify visual/audio output hasn't regressed
- **AI evaluation**: Claude can run chains and inspect the output
- **CI pipelines**: Generate thumbnails or verify examples compile and run
- **GIF creation**: Capture multiple frames for animation

Visual options:
- `--snapshot <path.png>` - Output path for the snapshot
- `--snapshot-frame <spec>` - Frame(s) to capture (default: 5)

Audio options:
- `--audio-snapshot <path.wav>` - Output path for audio capture
- `--audio-snapshot-duration <seconds>` - Duration to capture (default: 1)

Frame specification formats:
- `5` - Single frame (backwards compatible)
- `0,5,10,15` - Specific frames (comma-separated)
- `0-11` - Range (frames 0 through 11, inclusive)
- `0-20:2` - Range with step (frames 0, 2, 4, ..., 20)

When capturing multiple frames, filenames include frame numbers: `output.png` becomes `output_0000.png`, `output_0001.png`, etc.

### Multi-Sample Inspect

`vivid inspect` supports capturing multiple inspection snapshots over time:

```bash
vivid inspect path/to/project --duration 2 --samples 5
```

- `--duration N` sets the capture window to N seconds (assumes 60fps)
- `--samples K` distributes K evenly-spaced inspections across the duration
- `--resolution WxH` overrides render resolution (e.g., `960x540`)
- Output: wrapped envelope `{"project": "name", "duration": N, "sampleCount": K, "samples": [...]}` when using `--duration`; single JSON object without `--duration` (backward compatible)
- Without `--duration`, behaves as single-frame inspect (at `--frame` or default frame 10)
- With `--out <dir>`, saves `inspection.json`, `snapshot_NNNN.png` per sample, and `waveform.png` (if audio chain)

### Playback Script Event Types

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

### Validation Workflow

Use the inner loop for autonomous iteration on visual and audio output:

1. **Start the project**: Use `run_project` to launch Vivid with MCP connection
2. **Edit chain.cpp**: Make code changes
3. **Verify compilation**: Use `vivid build` (CLI) or `get_runtime_status` / `wait_for_reload` (MCP) — fix errors before proceeding
4. **Inspect output**: Call `inspect_chain` to get structured metrics (brightness, contrast, histogram, audio levels)
5. **Validate visually**: Call `capture_frame` and review the image
6. **Validate audio**: Call `capture_audio` to capture and analyze audio output
7. **Compare**: Use `compare_frames` or `compare_audio` to measure the effect of changes
8. **Iterate**: If metrics are off or assertions fail, go back to step 2

**Key MCP tools for validation:**
- `inspect_chain` - Structured metrics for autonomous reasoning (no pixels needed)
- `capture_frame` / `capture_audio` - Capture current output
- `compare_frames` / `compare_audio` - Measure change between before/after
- `sweep_param` - Explore visual parameter space with frame captures
- `sweep_param_audio` - Explore audio parameter space with audio captures
- `solo_operator` - Isolate a single operator's output for debugging
- `set_param` - Test parameter values in real-time before committing to code

**Monitor for user adjustments**: Periodically check `get_pending_changes` — if the user adjusted sliders in the visualizer, ask "I see you changed X to Y. Would you like me to update chain.cpp with these values?"

**Export for human review** (outer loop): When satisfied with metrics, use `export_video` to produce a video for the user to review subjectively.

## User Modules

Third-party modules extend Vivid with additional operators. See `docs/MODULES.md` for full documentation.

```bash
# Install from GitHub
vivid modules install https://github.com/seethroughlab/vivid-onnx

# List installed modules
vivid modules list

# For development: link a local module
vivid modules link ~/Developer/vivid-example

# Unlink when done
vivid modules unlink vivid-example
```

Modules are stored in `~/.vivid/modules/`. Linked modules point to the original location.

## Project Structure

```
src/
  vivid-core/         Runtime engine (required)
  cli/                Command-line interface and app
modules/               Optional libraries (all ship with Vivid):
  vivid-audio/        Audio input, FFT, oscillators
  vivid-video/        Video playback (HAP, AVFoundation)
  vivid-render3d/     3D rendering with PBR, GLTF, CSG
  vivid-network/      OSC, UDP, WebSocket
  vivid-serial/       Serial port, DMX
  vivid-midi/         MIDI input/output
  vivid-imgui/        Dear ImGui integration (UI addon template)
  vivid-opencv/       OpenCV integration (stub template)
projects/           Runnable example projects (each with own assets/ folder)
docs/               RECIPES.md, CREATING-OPERATORS.md
tests/              Automated tests, fixtures, and test assets (Git LFS)
dev/                Developer tools and planning docs
~/.vivid/modules/      User-installed third-party libraries
```

All libraries in `modules/` use `module.json` for metadata.

## Key Patterns

### Operator Pattern
All operators inherit from `Operator` or `TextureOperator`:
```cpp
class MyEffect : public TextureOperator {
    void init(Context& ctx) override;     // Called once
    void process(Context& ctx) override;  // Called every frame
    void cleanup() override;              // Called on destruction
    std::string name() const override;    // Display name
};
```

### Setter Pattern
Use void setters for configuration:
```cpp
auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;
noise.speed = 0.5f;
noise.octaves = 4;
```

### Parameter System
Use `Param<T>` wrapper and implement param methods:
```cpp
Param<float> m_scale{"scale", 1.0f, 0.0f, 10.0f};  // name, default, min, max

std::vector<ParamDecl> params() override { return { m_scale.decl() }; }
bool getParam(const std::string& name, float out[4]) override;
bool setParam(const std::string& name, const float value[4]) override;
```

### Chain Entry Point
User chains use the VIVID_CHAIN macro:
```cpp
void setup(Context& ctx) { /* add operators */ }
void update(Context& ctx) { ctx.chain().process(); }
VIVID_CHAIN(setup, update)
```

For custom window configuration, use `VIVID_CHAIN_CONFIG`:
```cpp
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
```

## Code Conventions

- **C++17** required
- **WebGPU** (wgpu-native) for all GPU operations
- **WGSL** for shaders
- **Platform code**: macOS (.mm files), Windows (special DLL handling), Linux (stubs)
- **Param<T> casting**: Use explicit `static_cast<float>()` when passing to std:: functions
  ```cpp
  // Wrong: std::max(0.0f, m_inputA)  -- template deduction fails
  // Right: std::max(0.0f, static_cast<float>(m_inputA))
  ```

## Common File Locations

| Task | File |
|------|------|
| Add new 2D effect | `modules/vivid-core/include/vivid/effects/` |
| Add network operator | `modules/vivid-network/include/vivid/network/` |
| Modify chain visualizer UI | `modules/vivid-core/src/chain_visualizer.cpp` |
| Hot-reload logic | `modules/vivid-core/src/hot_reload.cpp` |
| Main runtime loop | `modules/vivid-core/src/main.cpp` |
| Operator base class | `modules/vivid-core/include/vivid/operator.h` |

## Video Audio Modes

When using `VideoPlayer` with audio, there are two modes:

### Internal Audio (recommended for playback)
```cpp
video.setInternalAudioEnabled(true);  // Default
```
- Audio handled directly by AVPlayer (macOS) / platform decoder
- **Perfect A/V sync** - audio and video are synchronized by the native player
- No audio processing or effects available
- Use this when you just want to play a video with sound

### Chain Audio (for effects/processing)
```cpp
video.setInternalAudioEnabled(false);

auto& videoAudio = chain.add<VideoAudio>("videoAudio");
videoAudio.setSource("video");  // Extract audio from VideoPlayer

auto& delay = chain.add<Delay>("delay");
delay.input("videoAudio");

auto& output = chain.add<AudioOutput>("out");
output.setInput("delay");
chain.audioOutput("out");
```
- Audio extracted and routed through Vivid's audio chain
- Enables audio effects (delay, reverb, gain, etc.)
- **May have A/V sync issues** due to processing latency
- Use this when you need audio-reactive visuals or audio effects

See `modules/vivid-video/examples/video-audio/` for a complete example with both modes.

## External Modules

Some modules have been moved to separate repositories for independent versioning and CI:

- **vivid-opencv**: https://github.com/seethroughlab/vivid-opencv
  - Computer vision operators (Contours, OpticalFlow, BlobTrack)
  - Builds OpenCV from source to avoid MSVC STL ABI issues on Windows

- **vivid-onnx**: https://github.com/seethroughlab/vivid-onnx
  - Machine learning inference (PoseDetector, FaceDetector)
  - Uses ONNX Runtime

Install external modules with: `vivid modules install <repo-url>`

## Documentation

- `docs/RECIPES.md` - Complete chain.cpp examples
- `docs/ROADMAP.md` - Architecture decisions and development history
