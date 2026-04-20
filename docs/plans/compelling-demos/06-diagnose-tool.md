# Phase 3 — `diagnose_composition_issue` MCP Tool

**Date:** 2026-04-20
**Scope:** Turn the composition guide's "Diagnosing a dead graph" decision tree into a callable MCP tool. When Claude sees `motion_magnitude < 0.01` or `onset_response_rate < 0.3`, the tool returns a ranked list of likely causes and concrete fixes.

## What shipped

### Tool surface

```python
diagnose_composition_issue(
    analysis_json: str = "",      # paste from analyze_output, or
    intent: str = "",              # "drum-driven" | "continuous" | "ambient" | "parametric"
    window_seconds: float = 3.0,   # used when analysis_json is omitted
) -> str
```

If `analysis_json` is empty, the tool calls `analyze_output(mode="av", window_seconds=...)` itself. Returns `{ok, intent, analysis, findings: [...]}` with the full analysis JSON plus ranked findings.

### Finding schema

```json
{
  "severity": "critical" | "warning" | "info",
  "symptom": "visual.mean_brightness=0.003 (near-black)",
  "likely_cause": "Shapes may be too small to register ...",
  "fix": "Inspect each Shape2D: confirm BOTH scale_x and scale_y are driven ...",
  "confidence": "high" | "medium" | "low"
}
```

Findings are severity-sorted (critical first). If the findings list contains any critical severity, a structure-aware hint is appended ("call `inspect_graph(detail='full')` and check...") so the caller knows to follow up with graph introspection.

### Rule set (v1 — metric-only)

Nine rules encoded from `docs/COMPOSITION-GUIDE.md` § "Diagnosing a dead graph":

| # | Trigger | Severity | Summary |
|---|---|---|---|
| 1 | `audio.rms < 0.001` + audio-expecting intent | warning | settling delay OR missing audio_out |
| 2 | `mean_brightness < 0.01` | critical | near-black output; single-axis / low to_min / broken upstream |
| 3 | `motion < 0.01` with audio present | warning | near-static; single-axis / Smooth too long / sub-Hz LFO |
| 4 | `onset_response_rate < 0.3` with ≥3 onsets + drum intent | critical | peak without envelope / low to_min / feedback swallowing hits |
| 5 | high onset rate + all correlations near 0 | info | event-driven reactivity (healthy, don't "fix") |
| 6 | negative correlation + high onset rate | info | phase lag from feedback/smoothing (expected) |
| 7 | `detected_onsets == 0` + audio present | info | continuous content; onset metric N/A |
| 8 | `reactivity_latency_ms > 300` + onset rate > 0.5 | warning | Smooth/Feedback too slow |
| 9 | no other findings + healthy ranges | info | "no issues detected" baseline |

Rule 5 and Rule 6 are particularly important — they tell Claude *not* to chase a metric that looks low. Without them, a naive reading of "correlation is −0.3" might prompt Claude to tear apart a graph that's actually working as intended.

### Intent handling

- `""` — no hint; only fires rules that don't depend on intent (2, 3, 6, 7, 8, 9). Good default for unknown graphs.
- `"drum-driven"` / `"percussive"` — enables rule 4 (critical onset-response finding)
- `"continuous"` / `"pad"` / `"ambient"` — enables rule 1 (audio silence warning)
- `"parametric"` — reserved (no intent-specific rules yet)

Invalid intents return a descriptive error with the accepted values.

## Tests

`mcp/test_vivid_mcp_perception.py` gained 7 new tests (34 total, all pass):

- `test_diagnose_near_black_output` — critical finding fires on `mean_brightness=0.003`
- `test_diagnose_low_onset_response_on_drum_intent` — critical finding fires on rate=0.2 with drum-driven intent, fix mentions SmoothFr
- `test_diagnose_healthy_graph` — no critical/warning findings for healthy metrics
- `test_diagnose_phase_lag_is_info_not_warning` — negative correlation + high onset rate → info finding with phase/lag/event-driven explanation
- `test_diagnose_invalid_intent_errors` — descriptive error for unknown intent
- `test_diagnose_invalid_json_errors` — descriptive error for bad JSON
- `test_diagnose_calls_analyze_output_when_no_json_provided` — verifies the tool invokes analyze_output itself when analysis_json is empty

## Live verification

### Current `showcase_demo.json` (Smooth-fixed, onset_response_rate=0.917)

```
[warning] visual.motion_magnitude=0.0096 (near-static)
[info]    negative correlation (-0.28) with onset_response_rate=0.92
```

Correctly flags motion as marginal (0.0096 sits just below the 0.01 threshold because feedback smooths inter-frame deltas) and correctly *identifies the negative correlation as expected phase lag* rather than a broken graph. A naive Claude reading correlations at face value would try to "fix" a working graph; the tool prevents that.

### `lanes_intro_demo.json` (pure-visual, silent audio, healthy)

```
[info] no issues detected
```

Correct baseline — silent audio with no intent hint doesn't trigger rule 1, and all visual metrics are healthy.

## Sample session

The intended use pattern is:

```
1. User: "the graph feels weak"
2. Claude: diagnose_composition_issue(intent="drum-driven")
3. Tool: [critical] onset_response_rate=0.2 → "Insert SmoothFr between drum/peak and shape/scale_x"
4. Claude: add_node SmoothFr; disconnect peak→scale; reconnect peak→smooth→scale
5. Claude: diagnose_composition_issue(intent="drum-driven")
6. Tool: [info] no issues detected
```

The loop is short, specific, and grounded in measurement at every step.

## Known limitations (deferred)

v1 is metric-only. Three extensions would make it substantially more powerful:

1. **Structure-aware inspection** — call `inspect_graph` internally; flag `peak → scale` wires that skip Smooth, Shape2D nodes with only one scale axis driven, remap wires with `to_min < 0.03`. Would point at specific node IDs instead of generic "check your graph" guidance.
2. **Intent inference** — when `intent=""`, infer from the graph topology (drum presence → "drum-driven", oscillator-only → "continuous"). Currently the caller must hint.
3. **Per-band correlation awareness** — once Phase 1 task 8 ships, the rule set should be extended to recognize "bass drives brightness, treble doesn't" patterns and suggest frequency-specific tweaks.

The metric-only v1 is the smallest useful thing — it gracefully degrades to generic advice and is enough to catch the bugs I actually encountered in the intro set.

## Files touched

- `mcp/vivid_mcp.py` — `diagnose_composition_issue` tool + rule engine (`_compute_composition_findings`)
- `mcp/test_vivid_mcp_perception.py` — 7 new tests
- `docs/plans/compelling-demos/06-diagnose-tool.md` — this doc

## Status

- Phase 1 (perception): ✅ settling, ✅ multi-axis correlation, ✅ onset response rate. 🟡 per-band correlation (task 8).
- Phase 2 (operators + polish): ✅ complete. 🟡 `OnsetDetector` graph operator pending.
- Phase 3 (composition knowledge): ✅ composition guide, ✅ `diagnose_composition_issue`. 🟡 `get_composition_patterns`, 🟡 `explain_graph_composition`, 🟡 reference corpus.

## Recommended next slice

Given the diagnostic tool closes the "what's wrong" loop, the highest-leverage remaining Phase 3 work is the inverse direction: the "what should I build" loop.

1. **`get_composition_patterns(intent)` MCP tool** — returns curated signal-flow templates for common compositional patterns (drum-driven pulse, continuous reactivity, etc.) as JSON graph snippets Claude can adapt. Pairs with the diagnostic tool — "what's wrong" + "what works" forms a complete authoring support layer.
2. **`explain_graph_composition(graph_path)` MCP tool** — reads a graph JSON and returns a structured breakdown: signal flow, reactivity pattern, notable design choices. Would let Claude learn from existing graphs rather than only critique them.
3. **Reference corpus curation** — requires user-in-the-loop labeling; defer until the programmatic tools are in place so the corpus can be scaffolded with tool-assisted annotations.

Recommend (1) — it's the highest-leverage complement to the diagnostic tool and can ship without user labeling.
