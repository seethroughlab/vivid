# Audio-Visual Parity Rubric

## Purpose

This rubric provides a repeatable evaluation framework for assessing whether Vivid delivers on its core claim: that audio and visuals are true peers in a unified graph. Each workflow is scored across five categories. The results feed directly into the Parity Validation Report and the PRD Conformance Scorecard.

## Rating Scale

| Rating | Definition |
|--------|-----------|
| **Strong** | Both domains are equally capable, discoverable, and well-supported. No meaningful friction asymmetry. |
| **Adequate** | Both domains work, but one side requires more effort, has fewer options, or is harder to discover. The gap is livable but visible. |
| **Weak** | One domain is clearly underserved. A creator working in that direction would hit walls or need workarounds that the other domain does not require. |

Every non-Strong rating must include a **gap classification** (see below).

## Evaluation Categories

### 1. Ease of Creation

**What it measures:** Time-to-first-result and workflow comparability across audio-first and visual-first starting points.

**Evaluation questions:**
- How many nodes and connections are needed to get meaningful output in each domain?
- Is the wiring pattern (source → processing → output) equally intuitive?
- Do parameter defaults produce useful starting points in both domains?

**Strong example:** An Oscillator → Filter → Gain → audio_out patch and a Shape → Feedback → Bloom → video_out patch both take 4 nodes and produce immediately interesting results.

**Weak example:** Getting audio to respond to visuals requires a multi-step analysis chain that has no obvious entry point, while the reverse direction (audio → visuals) has a simple one-connection path.

### 2. Breadth of Options

**What it measures:** Exploration depth available in each domain — operator count, parameter richness, preset availability, and documentation coverage.

**Evaluation questions:**
- Are there enough operators to explore meaningfully in both domains?
- Do both domains offer comparable processing depth (effects, generators, analyzers)?
- Are factory presets and example graphs balanced across domains?
- Is documentation equally thorough for audio and GPU operators?

**Strong example:** Audio has 25 operators spanning synthesis, effects, and analysis. GPU has 20+ operators spanning generation, processing, and analysis. Both have example graphs.

**Weak example:** One domain has deep processing chains (EQ → Compressor → Reverb) while the other has only generators and a single post-process.

### 3. Cross-Domain Interaction

**What it measures:** How naturally shared data flows between domains — routing ease, signal path legibility, and whether cross-domain wiring requires special-casing.

**Evaluation questions:**
- Can control signals drive both audio and visual parameters with equal ease?
- Does audio-to-visual data flow (e.g., RMS → visual parameter) work as naturally as visual-to-audio (e.g., brightness → audio parameter)?
- Is the connection model symmetric, or does one direction require extra nodes/remapping?
- Are cross-domain signal paths legible in the graph editor?

**Strong example:** An LFO's `value` output connects directly to both `Oscillator/frequency` and `Shape/rotation` with no intermediate conversion nodes.

**Weak example:** Audio analysis outputs require manual range remapping to drive visual parameters, but visual analysis outputs drive audio parameters directly.

### 4. Inspectability

**What it measures:** The quality and completeness of live information exposed for iteration in both domains.

**Evaluation questions:**
- Can you see/hear what every node in the chain is doing?
- Are analysis and metering tools equally available? (Audio: RMS, peak, waveform, spectrum. Visual: frame capture, brightness, color, edge density.)
- Does `introspect_nodes` surface health/wiring/values for audio and GPU nodes equally?
- Can both domains be soloed for isolated inspection?

**Strong example:** Audio has Gain/rms, FFTAnalysis/spectrum, and Scopes. Visuals have TextureAnalysis (brightness, contrast, color, edge_density) and node thumbnails. Both surface live values through MCP inspection.

**Weak example:** Visual nodes show live thumbnails and rich analysis, but audio nodes only expose a single RMS value with no spectral or waveform visibility.

### 5. LLM Support

**What it measures:** Whether scaffold, mutate, inspect, and explain workflows work equally well for both domains via MCP tooling.

**Evaluation questions:**
- Can an LLM scaffold an audio-first workflow as easily as a visual-first one?
- Does `inspect_graph` return equally meaningful output for audio and GPU nodes?
- Can an LLM explain a cross-domain signal path clearly?
- Does `run_diagnostics` catch issues in both domains with equal coverage?
- Can an LLM suggest meaningful next variations in both domains?

**Strong example:** `inspect_node` on an Oscillator returns frequency, amplitude, waveform with semantic tags. `inspect_node` on a Shape returns radius, sides, rotation with semantic tags. Both are equally actionable.

**Weak example:** LLM can scaffold and explain a visual pipeline fluently, but struggles with audio because operator docs are sparser or parameter semantics are less discoverable.

## Gap Classification

Every non-Strong rating must be classified into exactly one bucket:

| Gap Type | Definition | Example |
|----------|-----------|---------|
| **Operator gap** | Missing capability — no operator exists to fill the role | No audio-reactive GPU operator that directly consumes audio buffers without an intermediate analysis step |
| **Docs/example gap** | Capability exists but is poorly demonstrated or documented | TextureAnalysis exists but no example graph shows it driving audio parameters |
| **Exploration-surface gap** | Capability exists but is harder to discover or use in one domain | Visual operators have richer factory presets; audio operators require more parameter knowledge |
| **Product bias** | System subtly favors one domain in defaults, examples, or onboarding | More intro graphs are visual-focused than audio-focused; visual output is always visible but audio requires headphones/speakers |

## How to Use This Rubric

1. Select a canonical fixture workflow (see `graphs/parity/`).
2. Walk through the workflow step by step, scoring each category.
3. For each non-Strong rating, write a one-sentence justification and assign a gap classification.
4. Record results in the Parity Validation Report.
5. After all workflows are scored, look for systemic patterns across gap classifications.
