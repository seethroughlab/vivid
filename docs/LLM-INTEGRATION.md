# LLM Integration

## 4. The LLM Execution Bridge

### 4.1 The Representation Problem

Vivid's canonical representation must simultaneously serve three masters: human readability (understand the flow, find the parameter to change), LLM readability and writability (parse, understand, generate valid modifications without hallucinating), and execution efficiency (compile to real-time frame rates with GPU-scale element counts).

### 4.2 The Two-Layer Architecture

**The routing layer** (what connects to what, parameter values) is interpreted and hot. Changes propagate within the same frame. It's a lightweight data structure: a graph of nodes with typed ports, connection lists, and parameter values. Serializable to JSON. An LLM reads and writes it trivially. This is what the user directly manipulates during experimentation.

**The computation layer** (per-element logic, GPU kernels, audio DSP) is compiled and fast. When the user or LLM modifies per-element behavior, this triggers a scoped compilation step. Only the changed kernel recompiles. The routing layer keeps running while the kernel compiles; the new kernel swaps in atomically when ready.

This is what Max/MSP's Gen~ does for audio, what TouchDesigner's GLSL TOPs do for visuals, and what Faust does for audio DSP. The graph is interpreted and instant; the inner computations are compiled and fast.

### 4.3 The Orchestration Layer

**The routing graph is JSON, and JSON is the complete description.** All automation, timing, and logic are handled by visible Control operators in the graph (LFO, Clock, Sequencer, Pattern, Envelope, Math, Logic, Gate, Smooth). There is no separate scripting or DSL layer. The answer to "how does this work?" is always "look at the graph."

This is the most LLM-friendly design: the LLM reads JSON, writes JSON. No code generation for the orchestration layer. Parameter transitions, pattern-driven modulation, and conditional logic are all visible as nodes rather than hidden in scripts.

**External integration via WebSocket:** Vivid's internal model is always JSON graph + control operators. A WebSocket API accepts graph mutations from external processes — a Python script, a JS orchestrator, a Max patch, or any other tool can generate JSON and feed it to Vivid. This provides scripting-level power for users who want it, without introducing a language dependency into the core.

**Build toward visual graph equivalence** on top of the same JSON representation. Every graph has a canonical visual representation and a canonical JSON representation. They are isomorphic. The LLM generates JSON (its strength); the visual graph exists for direct manipulation. The escape hatch to raw WGSL/C++ exists for power users writing operator internals.

### 4.4 The Four LLM Roles

**Operator layer — LLM as author (when needed).** The LLM first composes from existing operators (seed + installed packages). When no existing operator or combination achieves the goal, the LLM writes a self-contained C++ operator, hot-reload compiles it in under a second, and the user sees the result immediately. Authored operators should be designed for reuse — generic names, clear parameters, broad applicability — so they become lasting additions to the project, not throwaway scaffolding. The operator contract is designed to make this generation reliable.

**Routing layer — LLM as architect.** "Build me a patch with 3 audio analysis bands driving 3 visual layers with independent particle systems." The LLM generates graph structure as JSON that the user then explores. This is the scaffolding role — often combined with operator authoring when the scaffold requires new operators that don't yet exist.

**Experimentation layer — LLM as variation generator.** "Generate 8 different connection matrix configurations." "Fill this session column with particle behavior variations." The user evaluates and selects. The LLM produces breadth; the user provides taste.

**Reflective layer — LLM as critic and analyst.** "What's happening harmonically in the audio right now?" "The visual rhythm isn't syncing with the beat — what's wrong?" The LLM observes the current state and helps the user understand and refine.

### 4.5 LLM Integration Architecture

The LLM connects to Vivid through two complementary paths, both built on a shared Runtime API.

**The Runtime API** is an internal interface exposing all LLM-relevant operations: inspect graph structure, read and write parameters, capture frames and audio, run analysis tools, evaluate checks, scaffold operators, and modify graph topology. This is the single source of truth for what the LLM can do. Both integration paths below call into the same API.

**Path 1: MCP server.** Vivid runs an MCP (Model Context Protocol) server that exposes the Runtime API as MCP tools. Claude Code, Cursor, or any MCP-capable LLM connects to it externally. This is the primary LLM integration path — it provides the complete tool surface (inspect, add, connect, set_param, scaffold_operator, introspection/diagnostics/checks) with streaming, multi-turn context, and tool use UIs that external clients already provide. It also enables non-interactive use cases: CI pipelines running checks, scripts generating patch variations, installation monitors watching for drift.

Core/package update MCP tool surface (current):
- `check_core_updates(force_refresh=false)` — checks Vivid core app update availability from appcast metadata.
- `check_package_updates(core_version, include_all_installed=false)` — checks installed package update/compatibility status.

Perception MCP tool surface (current):
- `introspect_nodes(include_payload=false)` — compact node-count/domain/error summary; optional full payload.
- `run_diagnostics(include_payload=false)` — compact severity summary + top hint IDs; optional full findings.
- `validate_checks(checks, include_payload=false)` — compact validity/error-count summary; optional full validation details.
- `run_checks(checks, include_payload=false)` — compact pass/fail summary (`all_passed`, `all_critical_passed`); optional full per-check results.

Perception MCP response policy:
- Default output is compact and deterministic for stable tool loops.
- `include_payload=true` adds the full underlying runtime result object.
- Runtime-side failures are normalized into explicit `{ ok:false, error:{...} }` envelopes.

**Path 2: Built-in chat (deferred).** Originally planned as a collapsible chat panel inside Vivid's interface calling the Anthropic API directly. Deferred because the MCP server already provides full LLM integration through external clients, and building a built-in chat would mean significant complexity to produce a worse version of what Claude Code and Cursor already offer. May be revisited if in-app chat proves essential for creative workflows where context-switching to an external client is too slow.

**Future path: WebSocket API** (Phase 3) exposes the same Runtime API over WebSocket for non-LLM external processes — Python scripts, Max/MSP, show control systems. The MCP server and WebSocket API may share transport infrastructure but serve different audiences.

---

## 9. LLM Perception System

The LLM cannot see the screen. When it generates a graph, adjusts parameters, or scaffolds operators, it works blind unless it has structured instruments that turn pixels and waveforms into numbers it can reason about. The perception system is what closes the loop between LLM generation and creative quality.

### 9.1 The Perception Loop

LLM-assisted development in Vivid follows a feedback cycle: capture the current output (frame, audio buffer, or both), extract structured metrics, evaluate whether the result matches intent, modify the graph or parameters, and capture again to verify. This loop is the runtime equivalent of a human watching the screen while turning knobs. Without it, the LLM's role collapses from "collaborator" to "one-shot generator."

### 9.2 Three Perception Layers

#### Layer 1: Introspection

The LLM's eyes. Structured readout of what the graph is actually producing at every point in the processing chain.

- **Per-node output analysis:** for every GPU node, extract texture metrics (brightness, contrast, entropy, edge density, color temperature, clipping). For every Audio node, extract signal metrics (RMS, spectrum, crest factor, onset density, LUFS). For Control nodes, current values and recent history.
- **Chain tracing:** when the final output has a problem (too dark, clipping, frozen), the LLM inspects metrics at each node in the upstream chain to find where the problem originates. If brightness is healthy at node 3 and gone at node 4, node 4 is the culprit.
- **Solo mode:** isolate any node's output, bypassing everything downstream. In a graph (not a chain), this means rendering the selected node and its upstream dependencies only.
- **Performance metrics:** per-node timing, GPU memory, audio thread load. Identifies bottlenecks.

#### Layer 2: Analysis

The LLM's judgment. Higher-level evaluation that goes beyond raw metrics to assess perceptual and aesthetic quality.

- **Visual analysis:** color harmony scoring (complementary, analogous, triadic), bilateral and rotational symmetry measurement, spatial balance (rule of thirds, center of mass, quadrant distribution).
- **Audio analysis:** loudness standards compliance (EBU R128), spectral character (brightness, flatness, rolloff), dynamic range, pitch detection, stereo imaging.
- **Audio-visual reactivity:** this is core to Vivid's thesis. Measures how well visuals respond to audio: correlation between audio energy and visual brightness/motion, onset response rate (what fraction of beats produce visual change), reactivity latency (how many milliseconds between an audio event and the visual response), per-band correlation (does bass drive one thing and treble drive another).
- **Comparison tools:** A/B frame comparison (semantic diffs: brightness change, contrast change, sharpness change), A/B audio comparison (spectral diff, loudness diff), parameter sweeps (capture output across a parameter range to find optimal values).

#### Layer 3: Checks

The LLM's memory of intent. Codified quality gates that persist across sessions and can be checked automatically.

Checks are JSON declarations that bind a metric path or diagnostic condition to a comparison: "output brightness must be between 0.2 and 0.8," "audio RMS must be above 0.01," "critical diagnostics must be zero." They serve multiple purposes:

- **CI/CD:** run the graph headlessly and validate that all checks pass. Prevents regressions when operators are modified.
- **Intent preservation:** when the user says "make it brighter," the LLM can add a check that brightness stays above a threshold. Future changes that violate this check are flagged.
- **Installation monitoring:** for long-running installations, checks detect drift (frozen output, silence, loss of audio reactivity) and alert or trigger recovery.
- **Conditional checks:** guards allow checks to apply only when relevant. "Bass energy should be high, but only when the kick operator is active."

### 9.3 Temporal and Cross-Domain Metrics

Single-frame analysis is insufficient for a real-time system. The perception system must also measure temporal behavior (is the animation frozen? is there unwanted flicker? has a feedback loop converged or diverged? is the output looping?) and cross-domain relationships (does visual motion correlate with audio energy? how much latency exists between an audio onset and the visual response?).

These temporal and cross-domain metrics require multi-sample capture: the system records output over a time window (typically 1–3 seconds) and computes statistics across the sample set. This is more expensive than single-frame analysis and is triggered on demand rather than running continuously.

### 9.4 Design Principle

The perception system is not a debugger bolted onto the side. It is a core architectural component — the mechanism through which the LLM iterates on creative output. Every operator should expose metrics. Every domain bridge should be measurable. Checks should remain explicit and machine-readable. When the perception system works well, the LLM becomes a genuine collaborator: it can see what it built, evaluate whether it's good, and fix what's wrong.
