# Getting Started (First 10 Minutes)

This is the canonical onboarding path for Vivid 1.0.

Goal: go from zero to a running audiovisual graph, make meaningful edits, and save a reusable state.

## 0. Prerequisites

- macOS (primary supported platform for 1.0)

## 1. Download and Launch (2-3 minutes)

Download the latest macOS release build:

- GitHub Releases: <https://github.com/jeffcrouse/vivid/releases>
- Open `Vivid.app`.
- In the app, open a starter graph from the bundled `graphs/` set (start with `av_demo.json`).

## 2. Make Your First Edit (2 minutes)

In the graph UI:

1. Select a GPU node (for example `shape`/`composite` in `av_demo`).
2. Drag one visible parameter (size, hue, blend amount, etc.).
3. Confirm immediate visual response.

Then select an audio-related parameter and confirm audible response.

Expected result: both audio and visuals react immediately without restart.

## 3. Save a Variation (1 minute)

Use variation controls to save a state snapshot (for example `Intro`).
This gives you a repeatable baseline before further changes.

Expected result: recalling the variation restores the same parameter state.

## 4. Try a Package Operator (2-3 minutes)

Install one package library (from the app package manager UI or CLI):

```bash
vivid install https://github.com/seethroughlab/vivid-wavetable.git
```

Restart or refresh operator palette as needed, then add `WavetableSynth` to your graph.

Package library reference:
- [PACKAGE-LIBRARIES.md](PACKAGE-LIBRARIES.md)

## 5. Use MCP Perception Loop (optional, 1-2 minutes)

If you run with MCP tooling, use:

- `introspect_nodes`
- `run_diagnostics`
- `run_checks`

to inspect current state and validate constraints while editing.

Reference:
- [LLM-INTEGRATION.md](LLM-INTEGRATION.md)
- [PERCEPTION-API-SPEC.md](PERCEPTION-API-SPEC.md)

---

## Next Steps

- **Create your own operator (recommended):**
  - Operator contract + runtime architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
  - MCP workflow and scaffold tools: [LLM-INTEGRATION.md](LLM-INTEGRATION.md)
  - Practical scaffold/edit/reload checklist: [OPERATOR-CREATION-MCP-TEST-PLAN.md](OPERATOR-CREATION-MCP-TEST-PLAN.md)
- **Install more operator libraries:** [PACKAGE-LIBRARIES.md](PACKAGE-LIBRARIES.md)
- **Author your own package repo:** [vivid-package-template](https://github.com/seethroughlab/vivid-package-template)

## Starter Graph Set (Curated)

Use this order for first-run browsing:

1. `graphs/av_demo.json` — fastest “audio + visual together” baseline
2. `graphs/feedback_demo.json` — visual motion/feedback behavior
3. `graphs/audio_demo.json` — audio-only baseline
4. `graphs/audio_reactive_demo.json` — cross-domain response
5. `graphs/wgsl_filters_demo.json` — filter-chain workflow

## Graph Browse Index

Current `graphs/` remains flat; use this taxonomy for browsing:

- **Intro / Core**
  - `demo.json`, `av_demo.json`, `audio_demo.json`, `audio_reactive_demo.json`
- **GPU Composition / Motion**
  - `feedback_demo.json`, `composite_demo.json`, `bloom_demo.json`, `texture_analysis_demo.json`, `webcam_timemachine_demo.json`
- **WGSL Filter Demos**
  - `wgsl_filters_demo.json`, `hsv_demo.json`, `mirror_demo.json`, `edge_demo.json`, `displace_demo.json`, `transform_demo.json`, `gradient_demo.json`, `scanlines_demo.json`, `crt_effect_demo.json`, `dither_demo.json`, `ramp_demo.json`, `switch_demo.json`
- **Movie File In (MFI)**
  - `movie_file_in_demo.json`, `mfi_blur_demo.json`, `mfi_composite_demo.json`, `mfi_displace_demo.json`, `mfi_halftone_demo.json`, `mfi_posterize_demo.json`, `mfi_av_sync_demo.json`, `mfi_kitchen_sink_demo.json`
- **Control / MIDI**
  - `midi_demo.json`, `fft_bars_demo.json`

Curated browse entrypoints in-repo:

- [`graphs/README.md`](../graphs/README.md)
- [`graphs/intro/README.md`](../graphs/intro/README.md)
- [`graphs/performance/README.md`](../graphs/performance/README.md)
- [`graphs/demo/README.md`](../graphs/demo/README.md)
- [`graphs/package-examples/README.md`](../graphs/package-examples/README.md)

## Suggested Directory Convention (for new graphs)

For new additions, prefer:

- `graphs/intro/`
- `graphs/audio/`
- `graphs/gpu/`
- `graphs/filters/`
- `graphs/mfi/`
- `graphs/control/`

Do not move existing core demo files in-place during active development unless coordinated with tests/docs in the same change.

---

## Build From Source (Developers)

If you are developing Vivid itself (not just using release builds):

- CMake 3.20+
- C++17 toolchain (Clang recommended on macOS)

```bash
git clone --recursive https://github.com/jeffcrouse/vivid.git
cd vivid
cmake -B build
cmake --build build
./build/vivid graphs/av_demo.json
```
