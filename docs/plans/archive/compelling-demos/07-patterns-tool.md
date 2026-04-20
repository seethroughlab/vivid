# Phase 3 — `get_composition_patterns` MCP Tool

**Date:** 2026-04-20
**Scope:** The inverse of `diagnose_composition_issue`. That tool tells Claude *what's wrong*; this one tells Claude *what a working graph looks like*. Together they close the authoring loop.

## What shipped

### Tool surface

```python
get_composition_patterns(intent: str = "") -> str
```

Pure-static tool — no runtime calls. Returns curated pattern templates. Filters by intent:

| Intent | Pattern |
|---|---|
| `"drum-driven"` / `"percussive"` | Drum-driven pulse |
| `"continuous"` / `"pad"` / `"ambient"` | Continuous reactivity |
| `"parametric"` / `"sync"` | Parametric sync (shared source) |
| `"spectral"` / `"spectral-color"` / `"hue"` | Spectral color |
| `""` (no filter) | All four |

### Pattern schema

Each pattern includes:

```json
{
  "id": "drum-driven-pulse",
  "name": "Drum-driven pulse",
  "intents": ["drum-driven", "percussive"],
  "one_line": "Each drum's peak feeds an envelope follower, which drives a shape's scale on both axes.",
  "when_to_use": "...",
  "signal_flow": "drum/peak → SmoothFr → Shape2D/scale_x + scale_y",
  "key_operators": [
    {"type": "DrumKick", "role": "audio source"},
    {"type": "SmoothFr", "role": "envelope follower",
     "recommended_params": {"rise_time": 0.005, "fall_time": 0.4},
     "factory_preset": "Envelope follower (snappy)"},
    {"type": "Shape2D", "role": "visual responder"}
  ],
  "example_connections": [
    {"from": "drum/peak", "to": "smooth/input", "bridge": "peak"},
    {"from": "smooth/value", "to": "shape/scale_x",
     "from_min": 0.0, "from_max": 0.8, "to_min": 0.05, "to_max": 0.35, "clamp": true},
    {"from": "smooth/value", "to": "shape/scale_y",
     "from_min": 0.0, "from_max": 0.8, "to_min": 0.05, "to_max": 0.35, "clamp": true}
  ],
  "watch_out_for": [
    "Drive BOTH scale_x AND scale_y — single-axis distorts into a line",
    "Keep to_min >= 0.03 so shapes have baseline presence",
    "..."
  ],
  "expected_metrics": {
    "onset_response_rate": "> 0.7 for a full drum kit (0.85+ is great)",
    "reactivity_latency_ms": "< 300",
    "mean_brightness": "0.03–0.2"
  },
  "exemplars": ["graphs/intro/showcase_demo.json"]
}
```

`recommended_params` pulls from the actual Smooth factory presets where applicable, and `exemplars` point at the working saved graphs so Claude can `load_graph` any of them and observe the pattern in action.

### The four patterns

**Drum-driven pulse** (A) — the pattern Phase 2 arrived at for `showcase_demo`. Audio peak → Smooth → shape scale on both axes. Watch-outs encode the specific anti-patterns Phase 0–2 surfaced (single-axis driving, low to_min, feedback too aggressive).

**Continuous reactivity** (B) — sustained audio → visual parameter via `Gain.rms`. Exemplar is `audio_reactive_demo`. Flags the specific Phase 1 finding that correlation can be hiding on the motion axis rather than brightness.

**Parametric sync (shared source)** (C) — LFO/metronome forks to both audio and visual. Exemplars: `av_demo`, `av_metronome_demo`. Explicit note: reactivity metrics don't apply here (the coupling isn't *measurable* through the analyzer — it's the shared source, not a response).

**Spectral color** (D) — FFT → hue. No exemplar yet (the current `FFTAnalysis` operator requires manual bin reduction). Includes a `roadmap_note` pointing at the planned `SpectralFeatures` operator that would simplify this pattern.

## Tests

`mcp/test_vivid_mcp_perception.py` gained 6 new tests (40 total, all pass):

- `test_get_composition_patterns_all` — all 4 patterns present with no filter
- `test_get_composition_patterns_drum_driven` — intent narrows to single pattern
- `test_get_composition_patterns_percussive_matches_drum_driven` — synonym mapping
- `test_get_composition_patterns_continuous` — all 3 synonyms ("continuous", "pad", "ambient") map to same pattern
- `test_get_composition_patterns_schema_fields_present` — every pattern has the required 11 fields, and list-type fields really are lists
- `test_get_composition_patterns_invalid_intent_errors` — descriptive error for unknown intent

## Loop verification

Pair this with `diagnose_composition_issue` for the complete authoring cycle:

```
1. User: "build me a drum-driven AV patch"

2. Claude: get_composition_patterns(intent="drum-driven")
   → Returns drum-driven-pulse pattern with exemplar showcase_demo.json

3. Claude: either load_graph("graphs/intro/showcase_demo.json") and study,
   or build fresh using add_node + connect guided by key_operators +
   example_connections

4. Claude: diagnose_composition_issue(intent="drum-driven")
   → Confirms the result: onset_response_rate > 0.7, no critical findings

5. If findings fire, fix using the concrete `fix` guidance, re-diagnose,
   repeat until clean.
```

The loop is short, grounded in measurement at every step, and each tool's output structurally maps onto the next tool's input (findings say what to fix; patterns say how to fix it).

## Discoverability wiring

Cross-references added so users find the tool:

- `docs/COMPOSITION-GUIDE.md` § Common patterns — sidebar calls out `get_composition_patterns(intent)` as the programmatic form of the same content
- `mcp/vivid_mcp.py` docstring — explicitly tells Claude to pair with `diagnose_composition_issue`

## Files touched

- `mcp/vivid_mcp.py` — new `get_composition_patterns` tool + `_COMPOSITION_PATTERNS` data + `_filter_patterns_by_intent` helper
- `mcp/test_vivid_mcp_perception.py` — 6 new tests
- `docs/COMPOSITION-GUIDE.md` — sidebar cross-reference
- `docs/plans/compelling-demos/07-patterns-tool.md` — this doc

## Status

- Phase 1 (perception): ✅ settling, ✅ multi-axis correlation, ✅ onset response. 🟡 per-band correlation.
- Phase 2 (operators + polish): ✅ complete. 🟡 `OnsetDetector` graph operator pending.
- Phase 3 (composition knowledge):
  - ✅ composition guide
  - ✅ `diagnose_composition_issue` tool
  - ✅ `get_composition_patterns` tool
  - 🟡 `explain_graph_composition` tool
  - 🟡 reference corpus (~20 annotated graphs)

## Known limitations (future work)

- **No structural validation** — the tool returns templates but doesn't verify that the operators it recommends are actually registered in the running runtime. In practice this hasn't been a problem (all referenced operators ship with the app), but installing a package with overlapping names could introduce ambiguity.
- **Intent inference absent** — caller must specify the intent. Pairs with the same limitation in `diagnose_composition_issue`. A future `auto-infer` slice could look at the current graph's topology to suggest the most relevant pattern.
- **No pattern composition** — the tool returns individual patterns, not combinations. Many compelling AV graphs layer patterns (drum-driven + spectral color, continuous + parametric sync). A future `compose_patterns([intents...])` tool could return templates that combine multiple patterns cleanly.

## Recommended next slice

Given the authoring loop is now closed (diagnose + patterns), the highest-leverage remaining work is different in character — it's about **scale**:

1. **`explain_graph_composition(graph_path)`** — reverse of `get_composition_patterns`. Reads any graph JSON and returns the pattern(s) it exhibits plus a structured breakdown. Lets Claude learn from existing graphs rather than only critique them. Useful for onboarding into an unfamiliar graph, and for the upcoming reference corpus.

2. **Reference corpus curation** — requires user-in-the-loop labeling. Gold-standard anchor for Claude's judgment. Deferred until `explain_graph_composition` ships so the corpus can be scaffolded with tool-assisted annotations.

3. **Phase 1 task 8 — per-band correlation** — extends `analyze_output` metrics to bass/mid/treble. Adds metric breadth but doesn't close a new loop. Lower leverage than (1) now that the composition loop works.

4. **Phase 2 `OnsetDetector` graph operator** — graph-side trigger primitive. Modest enhancement; current Smooth + peak ports already cover the common cases.

Lean toward (1) `explain_graph_composition` — it symmetrizes the pattern tooling (write + read) and directly supports (2) reference corpus, which is the biggest remaining Phase 3 gap.
