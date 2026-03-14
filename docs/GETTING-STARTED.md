# Getting Started (First 10 Minutes)

This is the canonical onboarding path for Vivid 1.0.

Goal: go from zero to a running audiovisual graph, make meaningful edits, and save a reusable state.

## 0. Prerequisites

- macOS (primary supported platform for 1.0)

## 1. Download and Launch (2-3 minutes)

Download the latest macOS release build:

- GitHub Releases: <https://github.com/seethroughlab/vivid/releases>
- Releases are promoted as rolling-alpha checkpoints (not every CI fix appears as a new tag).
- Open `Vivid.app`.
- In the app, use **File → Open Example...** and start with `av_demo`.

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
vivid install https://github.com/seethroughlab/vivid-glitch.git
```

Restart or refresh operator palette as needed, then add a glitch operator to your graph.

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
- [PERCEPTION-API-SPEC.md](internal/PERCEPTION-API-SPEC.md)

---

## Next Steps

- **Create your own operator (recommended):**
  - Operator contract + runtime architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
  - MCP workflow and scaffold tools: [LLM-INTEGRATION.md](LLM-INTEGRATION.md)
  - Semantic tagging guidance (when to tag vs not to tag): [SEMANTIC-PARAM-TAGS.md](SEMANTIC-PARAM-TAGS.md)
  - Practical scaffold/edit/reload checklist: [OPERATOR-CREATION-MCP-TEST-PLAN.md](testing/OPERATOR-CREATION-MCP-TEST-PLAN.md)
- **Install more operator libraries:** [PACKAGE-LIBRARIES.md](PACKAGE-LIBRARIES.md)
- **Author your own package repo:** [vivid-package-template](https://github.com/seethroughlab/vivid-package-template)

## Starter Graph Set (Curated)

Use this order for first-run browsing:

1. `graphs/intro/av_demo.json` — fastest “audio + visual together” baseline
2. `graphs/gpu/feedback_demo.json` — visual motion/feedback behavior
3. `graphs/intro/audio_demo.json` — audio-only baseline
4. `graphs/intro/audio_reactive_demo.json` — cross-domain response
5. `graphs/filters/wgsl_filters_demo.json` — filter-chain workflow

## Graph Browse Index

`graphs/` is physically organized and discovered recursively:

- `graphs/intro/`
- `graphs/audio/`
- `graphs/gpu/`
- `graphs/filters/`
- `graphs/io/`

Each graph file includes a top-level `meta` section (`id`, `title`, `description`, `tags`, `difficulty`, `domains`, `requires_packages`, `featured_rank`), which powers in-app discovery/search.

## Suggested Directory Convention (for new graphs)

For new additions, prefer:

- `graphs/intro/`
- `graphs/audio/`
- `graphs/gpu/`
- `graphs/filters/`
- `graphs/mfi/`
- `graphs/control/`

Keep metadata current when adding or changing graphs so search/filter remains useful.

---

## Build From Source (Developers)

If you are developing Vivid itself (not just using release builds):

- CMake 3.20+
- C++17 toolchain (Clang recommended on macOS)

```bash
git clone --recursive https://github.com/seethroughlab/vivid.git
cd vivid
cmake -B build
cmake --build build
./build/vivid graphs/intro/av_demo.json
```
