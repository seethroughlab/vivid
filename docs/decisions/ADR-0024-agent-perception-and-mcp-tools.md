# ADR-0024: Agent Perception and Higher-Level MCP Tools

Status: accepted (implementing incrementally — see "As built" below)

Date: 2026-07-18 (accepted 2026-07-20)

Extends [ADR-0006](ADR-0006-agent-external-mcp.md),
[ADR-0008](ADR-0008-agent-capability-surface.md),
[ADR-0010](ADR-0010-native-reboot-seed.md),
[ADR-0019](ADR-0019-nothing-fails-silently.md),
[ADR-0022](ADR-0022-session-audio-graph.md), and
[ADR-0023](ADR-0023-shared-graph-ui-substrate.md).

## As built (2026-07-20)

The layer is being delivered incrementally against the phase plan below; every new handler keeps the
MCP↔control parity test green (149↔149, `set_playing` intentionally unexposed).

- **Phase 1 (session inspection):** DONE — `inspect_session_overview`, `inspect_scene`, `inspect_track`,
  `inspect_signal_flow`, `explain_scene`, `explain_signal_flow`.
- **Phase 2 (bindings/intent):** DONE — `list_mapping_destinations`, `suggest_mappings`, `explain_mapping`,
  `inspect_bindings`, and `connect_mapping_by_intent` (resolves an audio characteristic + a destination
  param from intent words, then wires via the same bridge primitive).
- **Phase 3 (unified discovery + named params):** DONE — `list_operator_catalog`, `find_operators`,
  `find_params`, `set_audio_op_param_by_name`, `audio_graph_set_node_param_by_name`, and
  `set_param_by_intent` (resolves one param across visual+audio, clamps to range, routes to the right
  setter by descriptor target).
- **Phase 4 (audio capture + offline analysis):** DONE — `capture_audio`, `analyze_audio`,
  `analyze_audio_clip`, `analyze_audio_file`, `detect_onsets`, `summarize_mix`, over an off-thread
  `analyze_pcm` (RMS/peak/clipping/crest/3-band/centroid-proxy/flux/transients/energy-windows).
- **Phase 5 (comparison + spectrum):** DONE — `compare_audio` (two source specs → per-feature deltas +
  a plain `B vs A` verdict) and `analyze_spectrum` (octave/mel/linear per-band energy via a bandpass
  biquad filterbank + energy-weighted centroid).
- **Phase 6 (visual perception):** STARTED — `capture_frame` (GPU readback of the active output →
  a viewable PNG + blank/no-output detection), `analyze_frame` (brightness/contrast/activity/dominant
  colors/color-spread/average-hash), and `compare_frames` (two saved images or the live output → hash
  Hamming distance + metric deltas + a verdict) are DONE. The readback is `VisualGraph::read_output_pixels`
  (copyTextureToBuffer + map, main-thread, BGRA→RGBA); CPU analysis + PNG in `cli/image_analysis_tools`.
  Still TODO: `analyze_visual_motion` and `summarize_visual_output` (need a short multi-frame capture).
- **Phases 7–8 (project/package workflow, proofs/checks):** NOT YET STARTED. The proof loop (Phase 8)
  composes the now-built inspection + audio + visual analysis into pass/warn/fail checks.

## Context

The current MCP bridge is mechanically healthy. The static parity guard reports every intended
control-server handler as reachable from MCP, with only `set_playing` intentionally hidden behind
the higher-level `play` and `stop` tools.

That is necessary but not sufficient for agent-first authoring. The present surface is still mostly
an edit/control API: it can mutate tracks, clips, audio graphs, visual nodes, mappings, plugins,
projects, shaders, packages, and presets. It exposes live audio-to-visual mapping signals such as
`master.level`, `master.transient`, and low/mid/high band energy. It also has useful MIDI-level music
theory tools such as `analyze_clip`.

What it does not yet provide is the perception and intent-level tool layer that Vivid Classic proved
was essential. Classic had a runtime bridge, an operator-dev/source server, and a perception/analysis
server. Agents could capture frames/audio, analyze output, compare variations, and work from
summary-first responses instead of raw dumps. Vivid 4 keeps the external-agent architecture, but the
current MCP surface has not yet recovered that perception layer.

The most important missing capability is actual audio analysis. Current Vivid publishes
instantaneous-ish real-time mapping features. Those are good for driving visuals, but they do not let
an agent answer questions such as:

- Is this mix clipping or too quiet?
- Did the kick get punchier after the edit?
- Which bar has the strongest low-end energy?
- Does the drop actually increase spectral brightness and transient density?
- Are two scene variations meaningfully different?
- Which musical signal should drive this visual parameter?

Agent-facing MCP tools therefore need to move up one layer: from raw graph/control verbs to
compressed inspection, perception, comparison, diagnostics, and guided authoring.

## Decision

Add a phased agent-perception and higher-level MCP tool layer while keeping the existing control
tools as stable compatibility primitives.

The target surface has seven tool families:

1. **Compressed session inspection.** One-stop, summary-first tools for understanding projects,
   scenes, tracks, graphs, mappings, and signal flow without forcing agents to assemble a dozen raw
   calls.

2. **Audio perception.** Non-real-time audio capture/render analysis jobs for loudness, clipping,
   crest factor, spectral balance, transient density, onset locations, tempo, pitch/chroma where
   practical, and before/after comparisons.

3. **Visual perception.** Frame capture and image analysis for visual state, motion/change,
   brightness/color distribution, composition, blank/error detection, and visual comparisons.

4. **Audio-visual binding affordances.** First-class discovery and intent helpers for mapping
   sources, destinations, suggested bindings, and explanations of why a relationship exists.

5. **Unified operator and parameter discovery.** A domain-aware operator catalog that covers visual
   operators, native audio operators, plugins, note effects, modulators, package origin, spawn
   affordances, params, ports, semantic tags, and named parameter setting.

6. **Project, package, and asset workflow tools.** Validation, diffing, asset discovery, project file
   reload, package scaffolding, package validation, package build/install, and compiled-operator
   clone/fork workflows.

7. **Proof/evaluation tools.** Persistent quality checks that an agent can run after edits, with
   structured pass/warn/fail output and concise evidence.

Existing MCP tools should remain. New tools should compose over the same control-server state and,
where possible, over existing handlers. Raw tools are still useful for precise edits; the new layer
is for authoring by intent and verifying the result.

## Design Principles

- **Perception is product architecture, not debug plumbing.** An external agent cannot hear or see
  Vivid unless Vivid gives it structured perception tools.

- **Do real analysis off the audio thread.** Real-time mapping features stay atomic and cheap.
  Windowed analysis captures or renders buffers, then analyzes them on a non-real-time worker.

- **Return summaries by default.** Tool responses should use `detail="summary|normal|full"` or
  `include_payload=false` patterns so agents do not drown in samples, bins, pixels, or whole-session
  JSON.

- **Expose evidence.** Analysis tools should return concise measurements and enough provenance to
  trust them: source, duration, sample rate, frame count, bar range, timestamps, and warnings.

- **Prefer product concepts.** Agents should ask about scenes, tracks, clips, bindings, and visual
  outputs before raw graph topology.

- **Keep edits reversible.** New mutating tools must route through the same main-thread edit path and
  undo/redo gateway as existing MCP and UI edits.

- **Keep old tools as compatibility wrappers.** Discovery and setting can grow new unified endpoints
  without breaking the existing `list_operators`, `list_audio_operators`, `set_node_param`, and
  graph-specific tools.

## Target Tool Surface

The exact names may change during implementation, but the public shape should converge on these
families.

### Session Inspection

- `inspect_session_overview(detail="summary|normal|full")`
- `inspect_scene(scene, detail="summary|normal|full")`
- `inspect_track(track, detail="summary|normal|full")`
- `inspect_signal_flow(scope="session|scene|track")`
- `explain_scene(scene)`
- `explain_signal_flow(scope="session|scene|track")`

These tools should aggregate existing `status`, `list_tracks`, `get_graph`, `get_audio_graph`,
`get_mappings`, `get_project_status`, and health information into compact authoring summaries.

### Audio Perception

- `capture_audio(source="master", duration_beats=None, duration_seconds=None, start="now|bar|scene")`
- `analyze_audio(source="master", duration_beats=None, duration_seconds=None, features=None)`
- `analyze_audio_clip(track, scene, features=None)`
- `analyze_audio_file(path, features=None)`
- `compare_audio(a, b, features=None)`
- `detect_onsets(source_or_clip, sensitivity=0.5)`
- `analyze_spectrum(source_or_clip, bands="octave|mel|linear")`
- `summarize_mix(duration_beats=None, duration_seconds=None)`

Initial features should include RMS/loudness proxy, peak, clipping count, crest factor, low/mid/high
energy, spectral centroid, spectral flux, transient density, strongest onset times, silence ratio,
and simple energy-over-time windows. Later phases can add LUFS, stereo width, chroma/pitch class
profiles, beat confidence, and timbral descriptors.

### Visual Perception

- `capture_frame(source="active_output", path=None)`
- `analyze_frame(source="active_output", features=None)`
- `compare_frames(a, b, features=None)`
- `analyze_visual_motion(duration_seconds=None, frames=None)`
- `summarize_visual_output(duration_seconds=None)`

Initial features should include blank-frame detection, average brightness, contrast, dominant colors,
color spread, edge/activity proxy, frame hash, and basic before/after difference metrics.

### Bindings and Mapping Intent

- `list_mapping_destinations(scope="visual|audio|all")`
- `suggest_mappings(intent, scene=None, source_scope="all", dest_scope="visual")`
- `connect_mapping_by_intent(source_intent, dest_intent, amount=1.0, curve=0.0, invert=False)`
- `explain_mapping(src=None, dst=None)`
- `inspect_bindings(scene=None, detail="summary|normal|full")`

These tools should make audio-visual relationships first-class. They should use stable track ids,
semantic param metadata, current mappings, and measured audio features to suggest sensible bindings.

### Unified Discovery and Named Params

- `list_operator_catalog(domain=None, kind=None, detail="summary|normal|full")`
- `find_operators(query, domain=None, kind=None)`
- `find_params(query, scope="all")`
- `set_audio_op_param_by_name(track, index, name, value)`
- `audio_graph_set_node_param_by_name(track, node, name, value)`
- `set_param_by_intent(target, intent, value)`

The catalog should include visual operators, native audio instruments/effects, note effects,
modulators, plugin entries when available, package origin, spawn affordances, params, ports,
semantic tags, display hints, and mapping affordances.

### Project, Package, and Asset Workflow

- `validate_project(detail="summary|normal|full")`
- `diff_project(base=None)`
- `list_project_assets(kind=None)`
- `resolve_asset(path_or_name)`
- `reload_project_files()`
- `scaffold_operator_package(name, domain, kind, path=None)`
- `validate_operator_package(path)`
- `build_operator_package(path)`
- `reload_operator_package(path)`
- `clone_operator(op, new_name)`

These tools should support Vivid's text-backed, external-editor workflow without absorbing source
control, package managers, or code editing into the app.

### Proofs and Checks

- `list_quality_checks()`
- `run_quality_check(name_or_goal, scope=None)`
- `compare_variations(a, b, criteria=None)`
- `explain_tradeoffs(a, b, criteria=None)`

Checks should be explicit and evidence-backed. A check can use existing state inspection, audio
analysis, visual analysis, and health diagnostics. It should return `pass|warn|fail`, concise
evidence, and suggested next actions.

## Phased Implementation Plan

### Phase 0: Lock the Current Contract

Goal: prevent drift while the surface grows.

- Keep the MCP/control parity test passing.
- Add a generated inventory report for MCP tools grouped by domain.
- Add response-shape documentation for summary-first tools: `ok`, `summary`, `warnings`,
  `evidence`, optional `payload`, and `detail`.
- Add eval cases that assert existing raw tools still work while new aggregate tools are added.

Exit criteria:

- Parity test passes.
- Tool inventory is documented.
- No existing MCP tools are renamed or removed.

### Phase 1: Compressed Session Inspection

Goal: let an agent understand a project in one or two calls.

- Implement `inspect_session_overview`.
- Implement `inspect_scene` and `inspect_track`.
- Implement `inspect_signal_flow` and `explain_signal_flow` using existing graph/mapping state.
- Keep these tools read-only and mostly composed from existing handlers.
- Add MCP evals for "what is happening in this scene?" and "what drives the visuals?"

Exit criteria:

- Agents can summarize transport, scenes, tracks, active clips, audio graphs, visual graph, mappings,
  project path, health, and obvious warnings without calling raw dump tools.

### Phase 2: Mapping Destinations and Binding Intent

Goal: make audio-visual relationships first-class instead of stringly typed.

- Implement `list_mapping_destinations`.
- Implement `inspect_bindings`.
- Add semantic metadata to destinations where missing.
- Implement `explain_mapping` for existing mappings.
- Implement a conservative `suggest_mappings` that ranks valid source/destination pairs by
  semantic tags and current analysis features.
- Add evals for "bind kick onset to visual impact" and "explain why this visual reacts."

Exit criteria:

- Agents can discover both sides of a mapping without inventing destination strings.
- Existing `connect_mapping` remains the low-level primitive.

### Phase 3: Unified Operator Catalog and Named Params

Goal: reduce visual/audio/plugin fragmentation in discovery and param edits.

- Implement `list_operator_catalog(domain?, kind?)`.
- Preserve `list_operators` and `list_audio_operators` as compatibility views.
- Implement `find_operators` and `find_params`.
- Add named audio param setters for linear audio ops and audio graph nodes.
- Extend discovery with package/plugin origin, spawn location, semantic tags, param ranges, units,
  and mapping affordances.
- Add evals for choosing a param by intent rather than exact name.

Exit criteria:

- Agents can search for an operator or param by domain and intent.
- Audio params can be set by name in common workflows.

### Phase 4: Audio Capture and Offline Analysis Jobs

Goal: recover the most important Vivid Classic audio-perception capability.

- Add a non-real-time analysis job system owned outside the audio callback.
- Add a bounded audio ring buffer or render/capture path for master and track sources.
- Implement `capture_audio`.
- Implement `analyze_audio`, `analyze_audio_clip`, and `summarize_mix`.
- Start with cheap, robust features: RMS/loudness proxy, peak, clipping count, crest factor,
  low/mid/high energy, spectral centroid, spectral flux, silence ratio, transient density, and
  energy-over-time windows.
- Implement `detect_onsets` over captured/rendered buffers.
- Add compact evidence output and full-payload opt-in for windows/bins/onsets.
- Add tests for deterministic fixture analysis.

Exit criteria:

- An agent can measure whether a change made a passage louder, brighter, punchier, quieter,
  clipped, or more transient-dense.
- Analysis never blocks or allocates on the real-time audio thread.

### Phase 5: Audio Comparison and Arrangement-Level Analysis

Goal: let agents compare variations and reason over sections.

- Implement `compare_audio`.
- Add support for bar/scene-bounded captures where transport state allows it.
- Add `analyze_spectrum` with coarse bands first, detailed bins as opt-in.
- Add stronger onset summaries: strongest onsets, onset density by bar, and transient change.
- Add optional tempo confidence and pitch/chroma summaries for suitable material.
- Add evals for before/after mix and drop-energy comparisons.

Exit criteria:

- An agent can say how two variations differ using measured evidence rather than parameter guesses.

### Phase 6: Visual Capture and Analysis

Goal: give agents eyes on the visual output.

- Implement `capture_frame` from the active output.
- Implement `analyze_frame` with blank/error detection, brightness, contrast, dominant colors,
  color spread, edge/activity proxy, and frame hash.
- Implement `compare_frames`.
- Add `analyze_visual_motion` from a short frame sequence.
- Add evals for "verify the output is not blank" and "compare two looks."

Exit criteria:

- An agent can verify that a visual edit produced visible output and compare two frames with
  structured evidence.

### Phase 7: Project, Asset, and Package Workflow Tools

Goal: make the external-editor/project-as-folder workflow inspectable and recoverable.

- Implement `validate_project`.
- Implement `list_project_assets` and `resolve_asset`.
- Implement `reload_project_files`.
- Implement `validate_operator_package`, `build_operator_package`, and `reload_operator_package`.
- Implement `scaffold_operator_package` only after package validation can reject bad outputs.
- Implement `clone_operator` for compiled operators, mirroring shader `fork_shader` where possible.
- Add package-authoring evals and fixture packages for visual, audio effect, instrument, note effect,
  and modulator paths as they become supported.

Exit criteria:

- Agents can diagnose missing assets, broken packages, compile errors, and stale project files with
  product-level messages.

### Phase 8: Proofs, Checks, and Classic-Style Perception Loop

Goal: close the edit-perceive-compare-verify loop.

- Implement `list_quality_checks` and `run_quality_check`.
- Implement `compare_variations` and `explain_tradeoffs`.
- Define built-in checks for no audio clipping, nonblank visual output, active mappings resolve,
  project validates, no quarantined required operators, and scene energy changed as intended.
- Add persistent proof recipes for common agent tasks.
- Expand the MCP eval harness to require state inspection, audio analysis, visual analysis,
  comparison, and final explanation.

Exit criteria:

- An agent can make a creative change, perceive the result, compare it against the goal, and report a
  measured pass/warn/fail outcome.

## Alternatives Considered

- **Only add more raw control handlers.** This keeps implementation simple but leaves agents
  stitching together low-level state and guessing whether edits succeeded perceptually.

- **Put all analysis in the real-time audio engine.** This would make mapping signals richer, but it
  risks violating the audio-thread safety model and still does not solve windowed comparison or
  offline analysis.

- **Recreate Vivid Classic's separate analysis server immediately.** A separate MCP server may be
  right again, but Vivid 4 should first define the product contract and shared response shapes. The
  implementation can later split into runtime, source/dev, and perception servers without changing
  the tool semantics.

- **Use only external command-line analysis tools.** External tools are useful for implementation and
  tests, but agents need stable product-level MCP tools that understand Vivid sources, scenes,
  tracks, transport, project assets, and mappings.

- **Expose raw samples, FFT bins, and frame buffers by default.** That makes tools powerful but too
  noisy. Summary-first responses with optional payloads preserve power without overwhelming agents.

## Consequences

- **Positive:** Vivid recovers the most important Classic lesson: agents can perceive, compare, and
  verify output instead of only editing hidden state.

- **Positive:** audio-visual bindings become discoverable and explainable product objects rather than
  strings an agent has to invent.

- **Positive:** analysis measurements create better creative feedback loops: brighter, punchier,
  less clipped, more dynamic, more reactive, less blank, more visibly changed.

- **Positive:** unified operator and param discovery reduces the split between visual operators,
  native audio ops, note effects, modulators, plugins, and packages.

- **Cost:** audio capture and analysis require careful ownership. The audio thread must only publish
  cheap data or copy into bounded structures; expensive analysis must run elsewhere.

- **Cost:** summary-first perception tools need stable schemas and tests. They are product contracts,
  not debug conveniences.

- **Cost:** some tools will initially provide proxy metrics rather than studio-grade analysis. For
  example, early loudness can be RMS/peak/crest before full LUFS; early visual analysis can be
  color/brightness/activity before semantic image understanding.

- **Follow-up:** document the response schema and add MCP eval cases for every phase before treating
  the new surface as stable.

