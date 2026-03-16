# Q2 Plan: Perception Analysis Tier

## Summary

Extend Vivid’s current perception surface from:

- `introspect_nodes`
- `run_diagnostics`
- `validate_checks`
- `run_checks`

into a first real analysis tier that helps an LLM compare outputs, reason about audio/visual behavior, and assess audio-visual reactivity.

This phase should stay narrow and structured. It is not a general “AI taste” system. It is a compact, deterministic analysis layer built on top of the existing runtime, capture, and control-server infrastructure.

The product target for Q2 is:

- the LLM can answer higher-level questions without only dumping structure
- at least one richer audio analysis surface exists
- at least one richer visual analysis surface exists
- at least one AV-reactivity metric exists
- at least one comparison workflow exists for two candidate outputs

## Implementation Changes

### 1. Add one new perception endpoint family instead of overloading existing ones

Keep `introspect_nodes`, `run_diagnostics`, and `run_checks` as they are. Add a new analysis-oriented endpoint family rather than overstuffing current payloads.

Use this shape:

- `analyze_output`
- `compare_outputs`

These should be exposed through the control server and therefore available to MCP automatically.

`analyze_output` should accept one captured subject:
- a live graph capture
- a frame capture
- an audio capture
- an AV capture

`compare_outputs` should accept two captured subjects of the same mode:
- frame vs frame
- audio vs audio
- AV vs AV

For Q2, do not add persistent analysis sessions or background sampling jobs. Keep analysis request/response based.

### 2. Lock the first analysis tier to four concrete capabilities

Implement exactly these first-cut analysis outputs:

- **Audio summary**
  - loudness/level summary
  - spectral character summary
  - dynamic/peak summary
- **Visual summary**
  - brightness summary
  - contrast summary
  - motion/activity summary
- **AV-reactivity metric**
  - correlation between audio energy and visual response over a short time window
- **Comparison summary**
  - structured delta between two captures or two graph states

Recommended metric set for Q2:

- audio:
  - RMS / peak summary
  - coarse spectral centroid or brightness proxy
  - spectral flatness or similar “texture” proxy
- visual:
  - mean brightness
  - contrast / variance
  - frame-to-frame change magnitude as motion/activity proxy
- AV:
  - energy-to-brightness correlation over a bounded window
- comparison:
  - brighter/darker
  - more/less contrast
  - more/less motion
  - louder/quieter
  - brighter/darker spectral character

Do not attempt harmonic analysis, symmetry scoring, color harmony scoring, LUFS compliance, or aesthetic judgment in this phase.

### 3. Define the capture contract explicitly

Q2 depends on analysis inputs being stable and deterministic enough for tooling.

Use these rules:

- `analyze_output` works on explicit captures, not implicit “whatever the app is doing right now”
- live-analysis requests must first capture the relevant subject through existing runtime/capture surfaces
- time-window analysis is bounded and short:
  - default window: about 1 second
  - acceptable range for Q2: 0.5–2.0 seconds
- `compare_outputs` requires same-mode inputs
- if a requested capture mode is unavailable, return a structured `ok:false` error rather than partial data

For Q2, prefer reusing current capture paths over inventing a separate perception recorder.

### 4. Keep responses compact, deterministic, and LLM-usable

Every new endpoint should return:

- `schema_version`
- `mode`
- `summary`
- `metrics`
- optional `notes` or `limitations`

Use deterministic field order and stable naming.

Recommended response style:

- `summary`:
  - a short structured summary object, not prose paragraphs
- `metrics`:
  - numeric values and small categorical labels
- `notes`:
  - optional caveats like `insufficient_motion`, `audio_too_quiet`, or `window_too_short`

Do not return huge raw arrays by default.
If needed, allow an `include_payload=true` style option consistent with the current perception/tooling policy.

### 5. Make comparison a first-class workflow

Q2 must leave behind one practical comparison workflow, not only single-output analysis.

Use this exact workflow model:

- capture candidate A
- capture candidate B
- call `compare_outputs`
- receive:
  - per-domain summary
  - directional deltas
  - a compact recommendation-style summary of what changed

The comparison output should answer questions like:

- which version is brighter
- which version is louder
- which version has more motion
- whether audio and visual reactivity increased or decreased

For Q2, comparison is endpoint/tooling-first. Do not build a large dedicated UI comparison workspace yet.

### 6. Keep solo and inspectability aligned with analysis

The current PRD scorecard already calls solo-mode/product inspectability only partially validated. Q2 should improve this indirectly.

Analysis requests should be able to target:

- the effective final output
- a selected node output
- a soloed node path when solo is active

Do not redesign solo mode in this phase. Just make sure the new perception endpoints can analyze what the user or LLM is actually isolating.

### 7. Document the analysis contract

Update docs so the new analysis tier becomes part of the actual product/tooling story:

- `docs/LLM-INTEGRATION.md`
- `docs/runtime/control_server.md`
- a new internal contract doc for perception analysis if needed
- optionally `docs/PRD-GAPS.md` or the future Q2 file as the implementation reference

Document one canonical Q2 workflow:

1. capture output A
2. capture output B
3. compare them
4. inspect AV-reactivity
5. choose the stronger candidate using structured analysis

## Public Interfaces / Types

Add these public control-server / runtime-facing actions:

- `analyze_output`
- `compare_outputs`

Use request fields along these lines:

- `mode`
  - `frame`
  - `audio`
  - `av`
- `source`
  - live runtime subject or captured artifact reference
- `window_seconds` for time-window analysis where relevant
- `include_payload` optional, consistent with current perception responses

Important type additions:

- one compact analysis result type per mode
- one comparison result type per mode
- one shared AV-reactivity result type

Do not add a large generic schema system in Q2. Keep the type surface intentionally small.

## Test Plan

### Runtime / analysis tests
Add or extend tests for:

- frame analysis returns deterministic brightness/contrast/motion metrics
- audio analysis returns deterministic loudness/spectral summary metrics
- AV analysis returns a stable reactivity metric over a bounded window
- analysis fails cleanly for unsupported or missing capture inputs
- comparison of two captures returns directional deltas correctly

### Control-server tests
Extend control-server coverage for:

- `analyze_output`
- `compare_outputs`
- compact response shape with `schema_version`
- `include_payload` behavior if implemented
- deterministic output ordering and error envelopes

### Integration scenarios
Add at least one end-to-end scenario for each:

1. analyze a live visual output
2. analyze a live audio output
3. compare two candidate outputs
4. compute AV-reactivity for a short live run

### Acceptance scenarios
Q2 is complete when these workflows all work:

1. The LLM can ask for a higher-level analysis of a visual result and get more than node structure.
2. The LLM can ask for a higher-level analysis of an audio result and get more than RMS/peak dumps.
3. The LLM can compare two candidate outputs and receive structured differences.
4. The LLM can measure one real AV-reactivity signal over time.
5. Docs clearly describe the new perception tier and how it differs from diagnostics/checks.

## Assumptions And Defaults

- Q2 is endpoint/tooling-first, not UI-first.
- Existing perception endpoints remain intact; Q2 adds a new analysis layer rather than redefining diagnostics/checks.
- The first analysis tier is intentionally narrow:
  - audio summary
  - visual summary
  - one AV-reactivity metric
  - one comparison workflow
- Time-window analysis is short and bounded, not continuous.
- No aesthetic scoring, no broad “creative judgment,” and no full PRD perception system in this phase.
- Solo mode is reused as-is; Q2 only makes it analyzable, not redesigned.
