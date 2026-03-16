# Audio-Visual Parity Validation Report

**Date:** 2026-03-15
**Rubric:** `docs/internal/PARITY-RUBRIC.md`
**Fixtures:** `graphs/parity/`

## Executive Summary

Vivid's audio-visual parity claim is **Adequate overall, with specific directional asymmetries**. The architecture genuinely treats audio and visuals as peers — the three-domain model with Control as hub is real and works. Cross-domain wiring via control signals is symmetric by design, and the MCP tooling layer is fully domain-agnostic.

The parity gaps that exist are concentrated in two areas:

1. **Audio→visual analysis is thinner than visual→audio analysis.** TextureAnalysis exposes 6 distinct control-float outputs (brightness, contrast, R, G, B, edge_density). Audio analysis exposes Gain/rms as a single control float; FFTAnalysis produces spectrum spreads which are powerful but harder to route into scalar-driven parameters. This is a meaningful operator gap.

2. **Example and onboarding bias leans visual.** The default `demo.json` (featured_rank 1) is visual-only. Of 4 intro graphs, only 1 is audio-only, 1 is audio-reactive, and 2 are visual or AV. The visual domain has more immediate "wow factor" visibility due to node thumbnails and always-on visual output, while audio requires speakers/headphones to evaluate.

Neither gap undermines the architectural claim. Both are addressable with targeted work.

## Per-Workflow Rubric Results

### Workflow 1: Audio-First (`parity_audio_first.json`)

| Category | Rating | Justification | Gap Classification |
|----------|--------|---------------|-------------------|
| Ease of creation | **Strong** | 4 nodes for a complete synth patch (Osc → Filter → Gain → audio_out); adding visual response requires ~5 nodes but uses the same wiring pattern. | — |
| Breadth of options | **Strong** | 25 audio operators provide deep synthesis and effects chains; GPU operators provide ample visual response surfaces. | — |
| Cross-domain interaction | **Strong** | AudioAnalysis provides 5 control-float outputs (rms, peak, spectral_centroid, spectral_flux, zero_crossing_rate) that drive visual parameters directly via connection remapping — symmetric with TextureAnalysis's visual→audio bridge. | — |
| Inspectability | **Strong** | AudioAnalysis provides multi-metric inspection (rms, peak, spectral_centroid, spectral_flux, zero_crossing_rate) comparable to TextureAnalysis's visual metrics. | — |
| LLM support | **Strong** | MCP tools scaffold, inspect, and diagnose both sides equally. `inspect_node` on audio and GPU nodes returns equally structured param/port data. | — |

**Workflow summary:** The audio side is genuinely strong — deep operator set, good defaults, natural wiring. The visual response works but the audio→visual bridge is narrower than it could be. An `AudioAnalysis` operator that exposes multiple control-float outputs (RMS, peak, spectral centroid, spectral flux) would close this gap.

---

### Workflow 2: Visual-First (`parity_visual_first.json`)

| Category | Rating | Justification | Gap Classification |
|----------|--------|---------------|-------------------|
| Ease of creation | **Strong** | 4 nodes for a complete feedback loop (Shape → Feedback → Bloom → video_out); adding audio response requires TextureAnalysis + 4 audio nodes — comparable effort. | — |
| Breadth of options | **Strong** | GPU operators provide generators, effects, and analysis. Audio operators offer rich synthesis and processing for the response layer. | — |
| Cross-domain interaction | **Strong** | TextureAnalysis outputs 6 control floats that drive audio parameters directly via connection remapping. This direction is the strongest cross-domain path in the system. | — |
| Inspectability | **Strong** | Visual side has thumbnails + TextureAnalysis metrics. Audio side now has AudioAnalysis providing equivalent multi-metric inspection. | — |
| LLM support | **Strong** | MCP tools handle both sides symmetrically. An LLM can read TextureAnalysis outputs, understand the remap ranges, and suggest parameter adjustments. | — |

**Workflow summary:** This is the strongest parity direction. TextureAnalysis is exactly the kind of bridge operator that makes cross-domain interaction feel natural. The visual→audio path is a model for what audio→visual should also provide.

---

### Workflow 3: Cross-Domain-First (`parity_cross_domain.json`)

| Category | Rating | Justification | Gap Classification |
|----------|--------|---------------|-------------------|
| Ease of creation | **Strong** | Clock + LFO + Envelope create a control hub that wires identically to both domains. Connection remapping handles range differences cleanly. | — |
| Breadth of options | **Strong** | Control domain has 29 operators including general modulators (LFO, Clock, Envelope, Math, Random, Smooth, Sequencer, SampleHold, Quantizer) plus I/O-specific operators (MIDI, OSC, Mouse, Keyboard). The core modulation toolkit now covers the essential control primitives. | — |
| Cross-domain interaction | **Strong** | This is the design's strongest point. LFO/value connects to both Oscillator/frequency and Shape/rotation with identical syntax. Connection remapping handles the range difference. The control hub is genuinely domain-agnostic. | — |
| Inspectability | **Adequate** | Control values are visible through MCP inspection. Visual side has thumbnails. Audio side relies on listening + Gain/rms. No unified "control signal monitor" that shows all outgoing values from a hub node at once. | Exploration-surface gap |
| LLM support | **Strong** | The control-hub pattern is very LLM-friendly. An LLM can reason about the signal flow, suggest new routing, and generate variations by adjusting remap ranges. | — |

**Workflow summary:** The control-as-hub architecture is the strongest evidence of genuine parity thinking. The same LFO drives both domains with zero special-casing. Minor gaps in control-domain breadth (fewer pure modulation operators than a hardware modular) and inspectability (no "probe" view for control signal routing) are livable.

---

## Aggregate Rubric Matrix

| Category | Audio-First | Visual-First | Cross-Domain | Overall |
|----------|-------------|-------------|--------------|---------|
| Ease of creation | Strong | Strong | Strong | **Strong** |
| Breadth of options | Strong | Strong | Strong | **Strong** |
| Cross-domain interaction | Strong | Strong | Strong | **Strong** |
| Inspectability | Strong | Strong | Adequate | **Strong** |
| LLM support | Strong | Strong | Strong | **Strong** |

**Overall parity judgment: Strong. Primary operator and discoverability gaps (O1, O2, D1, D2, P1) have been closed. Remaining gaps (E1, E2, P2) are low-severity and deferred.**

## Classified Gap List

### Operator Gaps

| # | Gap | Direction affected | Severity |
|---|-----|-------------------|----------|
| O1 | ~~No multi-metric audio analysis operator outputting control floats.~~ **CLOSED.** `AudioAnalysis` operator now provides 5 control-float outputs (rms, peak, spectral_centroid, spectral_flux, zero_crossing_rate), mirroring TextureAnalysis's pattern. | Audio → Visual | ~~High~~ Closed |
| O2 | ~~FFTAnalysis outputs are spreads, not control floats.~~ **CLOSED.** AudioAnalysis provides the scalar reduction path. FFTAnalysis remains useful for spectral visualization; AudioAnalysis handles the scalar routing use case. | Audio → Visual | ~~Medium~~ Closed |

### Docs/Example Gaps

| # | Gap | Direction affected | Severity |
|---|-----|-------------------|----------|
| D1 | ~~No example graph demonstrates TextureAnalysis driving audio.~~ **CLOSED.** `texture_analysis_demo.json` now wires TextureAnalysis brightness → Oscillator frequency and edge_density → Filter cutoff. | Visual → Audio | ~~Medium~~ Closed |
| D2 | ~~`texture_analysis_demo.json` is visual-only.~~ **CLOSED.** Graph now includes Oscillator, Filter, Gain, and audio_out driven by TextureAnalysis outputs. | Visual → Audio | ~~Low~~ Closed |

### Exploration-Surface Gaps

| # | Gap | Direction affected | Severity |
|---|-----|-------------------|----------|
| E1 | ~~Control domain modulation toolkit is narrower than synthesis/effects toolkit.~~ **CLOSED.** Sequencer (moved from vivid-sequencers to core), SampleHold (new), and Quantizer (new) close the modulation toolkit gap. Smooth already serves as a slew limiter (separate rise/fall times). Probability modulation is covered by Sequencer's per-step probability + Random + Gate. | Both | ~~Low~~ Closed |
| E2 | **No control signal probe/monitor view.** When a control node fans out to multiple targets, there's no way to see all downstream values simultaneously without inspecting each target. | Both | Low |

### Product Bias

| # | Gap | Direction affected | Severity |
|---|-----|-------------------|----------|
| P1 | ~~Default demo graph (`demo.json`, featured_rank 1) is visual-only.~~ **CLOSED.** `demo.json` moved to featured_rank 4. Intro ordering is now: av_demo (0) → audio_demo (2) → audio_reactive_demo (3) → demo (4). | Audio underserved | ~~Medium~~ Closed |
| P2 | **Visual output is always visible (node thumbnails); audio requires external playback.** This is inherent to the medium but means visual nodes "show" their work while audio nodes are silent in the graph editor. Partially mitigated by Scopes operator and Gain/rms. | Audio underserved | Low (inherent) |

## LLM Parity Findings

### MCP Tool Symmetry Assessment

| Capability | Audio domain | Visual domain | Symmetric? |
|-----------|-------------|---------------|------------|
| `inspect_graph` — structured node/port output | Params + port values returned | Params + port values returned | **Yes** |
| `introspect_nodes` — health/wiring summary | Domain, error state, connections reported | Domain, error state, connections reported | **Yes** |
| `add_node` + `connect` + `set_param` — scaffolding | Works identically for audio operators | Works identically for GPU operators | **Yes** |
| `run_diagnostics` — issue detection | Detects disconnected audio sinks, type mismatches | Detects disconnected GPU sinks, type mismatches | **Yes** |
| `inspect_node` — live values | Returns frequency, amplitude, gain etc. | Returns radius, decay, threshold etc. | **Yes** |
| Analysis feedback for iteration | AudioAnalysis produces 5 control-float metrics (rms, peak, spectral_centroid, spectral_flux, zero_crossing_rate) | TextureAnalysis produces 6 control-float metrics | **Yes — symmetric** |

### LLM Workflow Assessment

- **Scaffold:** An LLM can scaffold all three fixture workflows from scratch using `add_node`, `connect`, `set_param`, and `set_connection_remap`. No domain-specific barriers. **Symmetric.**
- **Inspect:** `inspect_graph` and `inspect_node` return equally structured data for both domains. **Symmetric.**
- **Explain:** An LLM can trace signal paths in both directions. Connection remapping is explicit and readable. **Symmetric.**
- **Suggest variations:** An LLM can suggest parameter changes, alternative operators, and new routing in both domains. The richer TextureAnalysis output gives slightly more to work with when suggesting audio→visual variations. **Mostly symmetric.**
- **Diagnose:** `run_diagnostics` covers both domains equally. **Symmetric.**

**LLM parity judgment:** The MCP layer is genuinely domain-agnostic. The only LLM-visible asymmetry mirrors the runtime asymmetry: audio analysis produces less structured feedback than visual analysis, giving an LLM less to work with when iterating on audio→visual coupling.

## Recommendations

### High Priority (close the primary parity asymmetry)

1. **Create an `AudioAnalysis` operator** (control domain) that takes audio input and outputs control-float metrics: `rms`, `peak`, `spectral_centroid`, `spectral_flux`, `zero_crossing_rate`. This is the single highest-impact change for parity. It mirrors TextureAnalysis's role and closes gaps O1 and O2.

### Medium Priority (address discoverability)

2. **Add a cross-domain example to `texture_analysis_demo.json`** (or create a companion graph) showing TextureAnalysis → audio modulation. Closes D1 and D2.

3. **Change `demo.json` to an AV graph** (or make `av_demo.json` featured_rank 1). Closes P1. The existing `av_demo.json` is already a good candidate — it uses Clock + LFO driving both domains.

### Low Priority (nice-to-have)

4. Expand control-domain modulation operators (sample-and-hold, slew limiter, step sequencer). Closes E1 but is not blocking parity.

5. Add a control-signal probe/monitor mode to introspection. Closes E2.

## Appendix: Evidence Sources

- Operator registry: `src/runtime/operator_registry.cpp` (25 audio, 26 control, 20+ GPU)
- Port definitions: `operators/` source files per operator
- Graph schema: `src/runtime/graph.h`
- MCP tools: `mcp/vivid_mcp.py` (94 tools)
- Existing demo graphs: `graphs/` (16 audio, 16 GPU, 18 filter, 5 intro)
- Parity fixture graphs: `graphs/parity/` (3 fixtures)
- PRD Conformance Scorecard: `docs/internal/PRD-CONFORMANCE-SCORECARD.md`
