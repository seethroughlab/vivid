# Analysis Tools Guide

How AI assistants perceive your Vivid project — and how to read what they see.

## Why This Matters

Your AI assistant can't look at your screen. When it builds visuals or tunes audio, it works blind unless it uses **analysis tools** — structured instruments that measure color, brightness, rhythm, composition, and more. These tools turn pixels and waveforms into numbers the AI can reason about.

This guide explains what those tools are, what the numbers mean, and how the AI uses them to make decisions. If you've ever wondered *how does Claude know my output is too dark?* or *why did it adjust that parameter?*, this is the document for you.

### Document Map

| Section | What You'll Learn |
|---------|-------------------|
| [The Perception Loop](#the-perception-loop) | How AI iterates: capture → analyze → modify → repeat |
| [Visual Analysis](#visual-analysis) | Color harmony, symmetry, and compositional balance |
| [Audio Analysis](#audio-analysis) | Loudness, spectrum, dynamics, and pitch metrics |
| [Audio-Visual Reactivity](#audio-visual-reactivity) | Measuring how well visuals respond to sound |
| [Chain Introspection](#chain-introspection) | Inspecting operators, performance, and signal flow |
| [Capture and Compare](#capture-and-compare) | A/B testing with snapshots, sweeps, and diffs |
| [Solo Mode](#debugging-with-solo-mode) | Isolating individual operators for debugging |
| [Quick Reference](#quick-reference) | "I want to know X" → use tool Y |

---

## The Perception Loop

AI development in Vivid follows a feedback loop:

```
  ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌──────────┐
  │ Capture  │────▶│ Analyze │────▶│ Decide  │────▶│  Modify  │
  │ (frame/  │     │ (extract│     │ (is it  │     │ (edit    │
  │  audio)  │     │ metrics)│     │  good?) │     │  code or │
  └─────────┘     └─────────┘     └─────────┘     │  params) │
       ▲                                           └────┬─────┘
       └────────────────────────────────────────────────┘
```

**Concrete example:** You ask the AI to "make the visuals brighter."

1. **Capture** — The AI captures the current frame to a PNG
2. **Analyze** — It runs `inspect_chain`, sees `meanBrightness: 0.15` (very dark)
3. **Decide** — 0.15 is well below the 0.2–0.8 healthy range, confirms the problem
4. **Modify** — It adjusts brightness-related parameters (bloom threshold, HSV value, feedback decay)
5. **Capture again** — Recaptures, sees `meanBrightness: 0.45` — improvement confirmed

### Three Modes of Perception

The AI can analyze your project in different ways depending on what's running:

| Mode | How | Best For |
|------|-----|----------|
| **Headless** | `vivid inspect .` or `capture_snapshot` — spawns a process, renders, exits | Quick checks, CI pipelines, no window needed |
| **Live instance** | `run_project` then `inspect_chain`, `capture_frame`, etc. | Interactive tuning, real-time parameter adjustment |
| **File-based** | `analyze_color_harmony`, `compare_frames`, etc. on any PNG/WAV | Post-hoc analysis, comparing exports |

---

## Visual Analysis

These three tools work on **any PNG file** — no running Vivid instance needed. The AI can analyze exported frames, screenshots, or reference images.

### Color Harmony (`analyze_color_harmony`)

Extracts a 5-color palette using K-means clustering in Lab color space, then scores it against standard color harmony models.

**What it returns:**

| Field | Type | Meaning |
|-------|------|---------|
| `palette` | hex[5] | The five dominant colors extracted from the image |
| `harmonyScore` | 0–1 | How well the palette matches a known harmony model |
| `harmonyType` | string | Best-matching model: `complementary`, `analogous`, `triadic`, `split-complementary`, or `none` |
| `paletteContrast` | 0–1 | Luminance contrast between palette colors |

**Interpreting scores:**

| Score Range | Meaning |
|-------------|---------|
| 0.7–1.0 | Strong harmony — colors work well together |
| 0.4–0.7 | Moderate — acceptable but could be refined |
| 0.0–0.4 | Weak — colors may clash or feel random |

**How the AI uses it:** When building color palettes, the AI captures a frame and checks if the extracted colors form a coherent scheme. If the score is low, it might adjust HSV hue shifts, operator tint colors, or blend modes to push toward a specific harmony model (e.g., complementary for high-energy contrast, analogous for smooth gradients).

### Symmetry (`analyze_symmetry`)

Measures bilateral and rotational symmetry — useful for validating kaleidoscope effects, mandala generators, and centered compositions.

**What it returns:**

| Field | Type | Meaning |
|-------|------|---------|
| `horizontalSymmetry` | 0–1 | Left-right mirror similarity |
| `verticalSymmetry` | 0–1 | Top-bottom mirror similarity |
| `radialSymmetry` | 0–1 | 4-fold rotational symmetry |

A score of 1.0 means perfect symmetry. A score of 0.0 means no measurable symmetry.

**When it matters:** Symmetry scores are most meaningful for effects that *should* be symmetric. A kaleidoscope with `radialSymmetry: 0.3` has a problem. A particle system with `radialSymmetry: 0.3` is perfectly normal.

**How the AI uses it:** After setting up a Mirror operator in kaleidoscope mode, the AI checks `radialSymmetry`. If it's low, common fixes include centering the source shape, increasing segment count, or adjusting the mirror offset.

### Spatial Balance (`analyze_spatial_balance`)

Evaluates compositional balance using the rule of thirds, edge bias, and quadrant weight distribution.

**What it returns:**

| Field | Type | Meaning |
|-------|------|---------|
| `thirdsScore` | 0–1 | Content placement at rule-of-thirds power points (1.0 = ideal) |
| `horizontalBias` | -1 to +1 | -1 = all content left, +1 = all content right, 0 = centered |
| `verticalBias` | -1 to +1 | -1 = all content top, +1 = all content bottom, 0 = centered |
| `balanceScore` | 0–1 | Overall compositional balance (1.0 = perfectly balanced) |

**How the AI uses it:** When placing visual elements, the AI uses spatial balance to avoid lopsided compositions. A `horizontalBias` of 0.6 means content is heavily shifted right — the AI might offset particles, adjust camera position, or reposition shape operators to compensate.

---

## Audio Analysis

### Capturing Audio (`capture_audio`)

Records audio output from a running Vivid instance and returns detailed analysis metrics. The AI uses this to evaluate mix quality, verify synth behavior, and catch problems like clipping or silence.

**Key metrics grouped by category:**

#### Loudness & Dynamics

| Metric | Meaning | Healthy Range |
|--------|---------|---------------|
| `rmsLevel` | Overall loudness (0–1) | Music: 0.1–0.5 |
| `peakLevel` | Maximum amplitude | Below 1.0 to avoid clipping |
| `integratedLUFS` | Broadcast-standard loudness | -23 LUFS (broadcast), -14 LUFS (streaming) |
| `truePeakDBTP` | True peak in dBTP | < -1 dBTP for safe headroom |
| `crestFactor` | Peak-to-RMS ratio | High = percussive, Low = compressed |
| `dynamicRangeDB` | Loudness variation in dB | Higher = more dynamic |

#### Spectral Character

| Metric | Meaning | Interpretation |
|--------|---------|----------------|
| `spectrum` | 6-band energy breakdown | See band distribution below |
| `spectralCentroid` | "Brightness" in Hz | Low = warm/dark, High = bright/harsh |
| `spectralFlatness` | Tonal vs. noise content | < 0.3 tonal, > 0.7 noise-like |
| `spectralRolloff` | 85th percentile frequency | Low = bass-heavy, High = bright |
| `harmonicToNoiseRatio` | Tonal clarity in dB | Sine > 15, Noise < 5 |

The `spectrum` array contains energy in six bands:

| Index | Band | Range |
|-------|------|-------|
| 0 | Sub-bass | < 60 Hz |
| 1 | Bass | 60–250 Hz |
| 2 | Low-mid | 250–500 Hz |
| 3 | Mid | 500 Hz – 2 kHz |
| 4 | High-mid | 2–4 kHz |
| 5 | High | 4 kHz+ |

#### Rhythm & Transients

| Metric | Meaning |
|--------|---------|
| `onsetDensity` | Transients per second (how "busy" the rhythm is) |
| `onsetCount` | Total detected transients in the capture window |
| `zeroCrossingRate` | Crossings per second (correlates with pitch/noise) |

#### Stereo

| Metric | Meaning |
|--------|---------|
| `stereoCorrelation` | L/R similarity: 1 = mono, 0 = independent, -1 = out of phase |
| `stereoWidth` | Side-to-mid ratio: 0 = mono, higher = wider |

#### Pitch

| Metric | Meaning |
|--------|---------|
| `pitchHz` | Detected fundamental frequency |
| `pitchNote` | Nearest note name (e.g., "C#4") |
| `pitchCents` | Cents deviation from nearest note |
| `pitchConfidence` | Detection reliability (> 0.8 = reliable) |

> For exhaustive field definitions and types, see [INTROSPECTION-REFERENCE.md](INTROSPECTION-REFERENCE.md#audioanalysis-per-audio-operator).

### Comparing Audio (`compare_audio`)

A/B tests two WAV files without a running instance. Returns diffs for every major metric:

```
rmsDiff, peakDiff, spectrumDiff, correlation, centroidDiff,
spreadDiff, fluxDiff, flatnessDiff, widthDiff, correlationDiff_stereo,
onsetDensityDiff, lufsDiff, pitchDiff
```

**Example workflow:** The AI wants to verify that adding reverb didn't destroy the mix:

1. Capture audio before the change → `before.wav`
2. Add the reverb operator, capture again → `after.wav`
3. Run `compare_audio` on both files
4. Check: `rmsDiff` small (loudness preserved), `centroidDiff` slightly negative (darker, expected from reverb), `correlation` > 0.5 (still recognizably similar)

### Sweeping Audio Parameters (`sweep_param_audio`)

Automates "try N values and listen to each." The AI sweeps a parameter (e.g., filter cutoff from 200 Hz to 8000 Hz in 5 steps), captures audio at each step, and compares the analysis metrics to find the best value.

---

## Audio-Visual Reactivity

The `analyze_av_reactivity` tool measures how well your visuals respond to audio. It requires a running instance with audio output.

### How It Works

1. Captures synchronized audio and visual data over a duration (default: 3 seconds)
2. Computes correlation between audio loudness and visual brightness/motion
3. Detects audio onsets (transients) and checks if visuals respond to them
4. Measures latency between audio events and visual reactions
5. Calculates mutual information (catches non-linear relationships that correlation misses)

### Key Metrics

| Metric | What It Tells You | Good | Weak |
|--------|-------------------|------|------|
| `avCorrelation` | Overall audio-visual coupling | > 0.4 | < 0.2 |
| `onsetResponseRate` | Fraction of beats that trigger visible change | > 0.6 | < 0.3 |
| `reactivityLatencyMs` | Delay between sound and visual response | < 50 ms | > 100 ms |
| `avMutualInformation` | Non-linear dependency (catches what correlation misses) | > 0.2 | < 0.1 |
| `responseMagnitudeRatio` | How much stronger visuals react during beats vs. silence | > 1.5 | < 1.1 |

The `bandCorrelations` object breaks this down per frequency band (subBass, bass, lowMid, mid, highMid, high), showing which frequencies drive which visual changes.

### Example: Diagnosing Weak Reactivity

You've built an audio visualizer but the visuals feel disconnected from the music. The AI runs `analyze_av_reactivity` and sees:

```json
{
  "avCorrelation": 0.12,
  "onsetResponseRate": 0.25,
  "reactivityLatencyMs": 83,
  "bandCorrelations": {
    "bass": { "correlation": 0.08 },
    "mid": { "correlation": 0.35 }
  }
}
```

**Diagnosis:** Correlation is low (0.12), especially in bass (0.08). The visuals respond to mids but barely react to bass. Latency is high (83 ms = 5 frames).

**Fixes the AI might try:**
- Connect bass band energy to a visual parameter (scale, brightness, displacement strength)
- Reduce the analysis smoothing to lower latency
- Add a BeatDetect operator and drive visual triggers from it

After changes, re-run the analysis to confirm improvement.

> For the full field list, see [INTROSPECTION-REFERENCE.md](INTROSPECTION-REFERENCE.md#audiovisualanalysis-cross-domain-reactivity).

---

## Chain Introspection

These tools require a **running Vivid instance** (via `run_project` or `vivid .`).

### Inspecting the Chain (`inspect_chain`)

The most powerful introspection tool. Returns three layers of data:

**Layer 1: Per-operator parameters**
Every operator's current parameter values and computed metrics (like feedback energy, audio RMS, beat state).

**Layer 2: Output texture analysis**
The final chain output analyzed for brightness, contrast, color, spatial distribution, and more — the same FrameAnalysis fields listed in the [Introspection Reference](INTROSPECTION-REFERENCE.md#frameanalysis-output-texture-analysis).

**Layer 3: Per-operator texture analysis** (opt-in with `per_operator_analysis: true`)
Texture analysis at *every node* in the chain. This is how the AI traces problems to their source.

#### Debugging Example: Where Did the Brightness Go?

The final output has `meanBrightness: 0.05` — nearly black. With per-operator analysis enabled, the AI can trace brightness through the chain:

```
noise          → meanBrightness: 0.52  ✓ Healthy
feedback       → meanBrightness: 0.48  ✓ Slight drop, normal
displace       → meanBrightness: 0.45  ✓ OK
hsv            → meanBrightness: 0.06  ✗ Huge drop!
bloom          → meanBrightness: 0.05  ✗ (can't recover what's gone)
```

The HSV operator is the culprit — likely a value multiplier that's too low. The AI adjusts it and re-inspects to confirm the fix.

### Chain Structure (`get_chain_structure`)

Returns the operator topology — types, names, and connections — without parsing C++ code. Useful for understanding what a project does at a glance.

```json
{
  "operators": [
    { "name": "noise", "type": "Noise", "output": "Texture" },
    { "name": "bloom", "type": "Bloom", "input": "noise", "output": "Texture" }
  ]
}
```

### Performance Stats (`get_performance_stats`)

Real-time performance metrics:

| Metric | Meaning |
|--------|---------|
| FPS | Current frame rate |
| Frame time | Milliseconds per frame |
| Per-operator timing | How long each operator takes (identifies bottlenecks) |
| Texture memory | GPU memory used by textures |

The AI uses this to identify slow operators. If `get_performance_stats` shows a Blur operator taking 8ms in a 16ms frame budget (60 FPS), that's where to optimize — reduce kernel size, lower resolution, or replace with a cheaper effect.

### Live Parameters (`get_live_params`)

A lightweight read of current parameter values. Faster than `inspect_chain` when you just need to check a specific operator's settings:

```
get_live_params(operator: "bloom")
→ { "threshold": 0.6, "intensity": 1.2, "radius": 8.0 }
```

---

## Capture and Compare

### Capture Tools

| Tool | Needs Running Instance? | What It Does |
|------|------------------------|--------------|
| `capture_frame` | Yes | Captures the current frame from a live instance |
| `capture_snapshot` | No | Spawns a process, renders to PNG, exits |
| `capture_at_frame` | Yes | Advances to a specific frame, then captures |
| `capture_audio` | Yes | Records audio output to WAV with analysis |

**When to use which:** Use `capture_snapshot` for quick one-off checks when no instance is running. Use `capture_frame` when you're interactively tuning a live instance and want to see the current state.

### Compare Tools

**`compare_frames`** — Compares two PNG images. Returns:
- **Pixel-level:** RMSE, per-channel diff, percentage of changed pixels
- **Semantic:** Brightness diff, contrast diff, entropy diff, sharpness diff, noise diff, temperature diff, edge density diff, clipping changes, hue count diff, center displacement

The semantic diffs are especially useful. Instead of looking at pixels, the AI can ask: *"Did brightness change? Did contrast improve? Did we lose sharpness?"*

**`compare_audio`** — Compares two WAV files with the same approach: RMS diff, spectral diff, correlation, onset density diff, LUFS diff, and more.

### Parameter Sweeps

**`sweep_param`** — Visual parameter exploration. Sweeps a parameter across a range, capturing a frame at each step. Example: sweep bloom threshold from 0.1 to 0.9 in 5 steps, get 5 PNGs showing the effect of each value.

**`sweep_param_audio`** — Same idea for audio. Sweep a synth parameter, capture audio at each step, get analysis metrics for each. The AI uses this to find optimal values without manual trial-and-error.

### A/B Testing Workflow

A common pattern the AI follows when making changes:

1. **Capture baseline** — `capture_frame` → `before.png`
2. **Make change** — Edit code or `set_param`
3. **Capture result** — `capture_frame` → `after.png`
4. **Compare** — `compare_frames(before.png, after.png)`
5. **Evaluate** — Did the metrics improve? If `brightness_diff: +0.25` and the goal was "make it brighter," that's a win. If `sharpness_diff: -0.04`, check if the change accidentally blurred things.

---

## Debugging with Solo Mode

Solo mode isolates a single operator's output, bypassing everything downstream. This lets the AI (or you) see what a specific operator is actually producing.

### Tools

| Tool | What It Does |
|------|--------------|
| `solo_operator(name)` | Show only this operator's output |
| `exit_solo` | Return to normal chain output |
| `get_solo_state` | Check if solo mode is active |

### When It's Useful

- **Black output:** Solo operators one by one (starting from the source) to find where the signal dies
- **Unexpected colors:** Solo the HSV or tint operator to see its isolated contribution
- **Performance debugging:** Solo a suspected slow operator and check if FPS improves
- **Verifying input:** Solo an operator to confirm it's receiving the right input texture

### Example

The chain output is unexpectedly blue. The AI solos operators in sequence:

```
solo_operator("noise")      → Grey noise pattern (expected)
solo_operator("hsv")        → Blue-tinted noise (the HSV hue shift is too high)
exit_solo()                 → Fix the hue parameter, back to normal view
```

---

## Quick Reference

*"I want to know X — which tool do I use?"*

| Question | Tool | Key Metric(s) |
|----------|------|---------------|
| Is my output too dark/bright? | `inspect_chain` | `meanBrightness` (0.2–0.8) |
| Is contrast too low? | `inspect_chain` | `contrast` (0.15–0.35 healthy) |
| Do my colors work together? | `analyze_color_harmony` | `harmonyScore`, `harmonyType` |
| Is my composition balanced? | `analyze_spatial_balance` | `balanceScore`, `horizontalBias`, `verticalBias` |
| Is my kaleidoscope symmetric? | `analyze_symmetry` | `radialSymmetry` |
| Is the audio clipping? | `capture_audio` | `clippedSamplePct` (should be 0) |
| How loud is the output? | `capture_audio` | `integratedLUFS`, `rmsLevel` |
| What frequency band dominates? | `capture_audio` | `spectrum` array, `spectralCentroid` |
| Do visuals respond to audio? | `analyze_av_reactivity` | `avCorrelation`, `onsetResponseRate` |
| Which operator is slowing things down? | `get_performance_stats` | Per-operator timing |
| Where does brightness drop? | `inspect_chain` (per-op) | Per-operator `meanBrightness` |
| What did my change actually do? | `compare_frames` / `compare_audio` | Semantic diffs |
| What's the best value for parameter X? | `sweep_param` / `sweep_param_audio` | Compare captures across range |
| What does operator Y actually output? | `solo_operator` | Visual inspection |
| What's connected to what? | `get_chain_structure` | Operator topology |

---

## Automating Quality Checks

Once you've identified the metrics that matter for your project, you can codify them as **assertions** in `vivid-assertions.json`. This turns analysis into automated pass/fail checks:

```bash
vivid check .
```

Assertions let you define thresholds like "brightness must be between 0.2 and 0.8" or "audio RMS must be above 0.05." The check command runs your project headlessly, captures the output, and validates against your assertions.

This is useful for CI pipelines, preventing regressions, and maintaining quality standards across changes.

---

## Further Reading

- [MCP-TOOLS.md](MCP-TOOLS.md) — Complete tool catalog with all parameters
- [INTROSPECTION-REFERENCE.md](INTROSPECTION-REFERENCE.md) — Exhaustive field definitions for every metric
- [AI-WORKFLOW.md](AI-WORKFLOW.md) — Working with AI assistants (AGENTS.md, BRIEF.md, slider workflow)
