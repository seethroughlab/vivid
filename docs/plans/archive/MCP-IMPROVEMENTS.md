# Source-Driven MCP Operator Docs Plan

## Summary
Replace the generated-JSON ingestion approach with a **source-driven operator docs pipeline** inside Vivid’s runtime/control-server layer. The MCP server should read operator documentation directly from repo source files, merge that with runtime descriptor truth, and return the combined result through `operator_docs`, `package_operator_docs`, and a richer `list_types`.

This plan specifically fixes the wrapper/shared-core case such as `ClockAu` / `ClockFr` using docs from [clock_core.h](/Users/jeff/Developer/vivid/operators/control/clock/clock_core.h), and `EnvelopeAu` / `EnvelopeFr` using docs from [envelope.h](/Users/jeff/Developer/vivid/operators/control/envelope/envelope.h), without relying on lossy or mis-keyed generated site artifacts.

## Implementation Changes
### 1. Source-resolved operator doc discovery
- Add a runtime source-doc resolver in `src/runtime`, for example `operator_source_docs.h/.cpp`.
- Given a runtime operator name, resolve docs from source using this exact order:
  1. Find the registered operator loader/descriptor by runtime name.
  2. Find the source file that contains `VIVID_REGISTER(<RuntimeName>)`.
  3. If the registered struct itself has the doc block, use it.
  4. If the registered struct is a thin wrapper around a shared base or included core/header, follow that base definition and use the nearest doc block attached to the shared implementation struct.
- The resolver must explicitly support wrapper variants like:
  - `ClockAu` / `ClockFr` -> `ClockCore`
  - `EnvelopeAu` / `EnvelopeFr` -> `Envelope`
  - same pattern for other shared core/header operators
- Do not parse comments at process startup for the whole repo. Resolve lazily on docs requests and cache results in memory by operator name.

### 2. Direct source parsing, not site JSON
- Reuse the parsing logic and tag vocabulary from `scripts/extract_operator_docs.py`, but move the parsing capability into runtime C++ instead of reading `site/operators/*.json`.
- Parse these tags directly from source comments:
  - `@brief`
  - body text
  - `@tip`
  - `@see`
  - `@param`
  - `@input`
  - `@output`
  - `@recipe`
  - `@pitfall`
  - `@family`
  - `@best_used_with`
  - `@common_companions`
- Keep the runtime parser tolerant:
  - missing doc block -> `has_docs=false`
  - missing tags -> empty fields
  - no operator should fail to load or introspect because docs are absent
- The docs extractor script can remain for the website, but MCP/runtime must not depend on its output.

### 3. Richer port metadata in descriptors
- Extend `VividPortDescriptor` in [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h) with:
  - `semantic_shape`
  - `semantic_intent`
  - `description`
- Add port helper APIs in [operator.h](/Users/jeff/Developer/vivid/src/operator_api/operator.h):
  - `semantic_tag`
  - `semantic_shape`
  - `semantic_intent`
  - `description`
- Update control-server serialization to emit these fields for ports in:
  - `list_types`
  - `operator_docs`
  - `package_operator_docs`
- Preserve backward compatibility:
  - unset fields stay absent/null
  - existing operators compile unchanged

### 4. Control-server docs endpoints
- Add a new `operator_docs` method:
  - input: `name`, optional `package`
  - output: one merged doc payload for that operator
- Upgrade `package_operator_docs` to use the same source-doc resolver for each package operator.
- Keep `list_types` lightweight, but add:
  - `brief` when available
  - `has_docs`
  - `lane_behavior_help`
- Runtime descriptor metadata remains authoritative for:
  - operator name
  - param types/defaults/min/max/choices
  - port types/transports/channels
  - lane behavior
- Source docs provide the human guidance:
  - brief/body/tips
  - param/input/output narratives
  - recipes/pitfalls/relationship hints

### 5. Source authoring upgrades for high-value operators
- Add or improve source doc blocks and `@input` / `@output` coverage for the first high-value operator set:
  - `EnvelopeAu` / `EnvelopeFr` via shared `Envelope`
  - `Filter`
  - `Gain`
  - `ClockAu` / `ClockFr` via shared `ClockCore`
  - `ChordProgressionAu`
- For each of those, ensure the source docs explicitly explain:
  - what the operator does
  - what the important ports expect
  - how lane behavior affects graph construction
- Seed required recipes and pitfalls in source doc blocks:
  - `voices/gates -> EnvelopeAu/gate -> VoiceMixer/amp_env_audio`
  - `EnvelopeAu/value -> Filter/cutoff_mod`
  - `PolyVoiceAllocator/frequencies -> Filter/frequencies`
  - `ClockAu/beat_phase -> ChordProgressionAu/beat_phase`
  - pitfall: `beat_phase` is global retriggering, not per-note ADSR
- Keep source docs as the single human-authored truth for MCP-visible guidance.

### 6. MCP surface
- Update `mcp/vivid_mcp.py` so MCP exposes:
  - richer `list_types`
  - richer `package_operator_docs`
  - new `operator_docs`
- Tool descriptions should explicitly say docs are sourced from operator source comments plus runtime metadata.
- Do not expose raw source files through MCP in this pass; expose parsed/merged docs only.

## Public APIs / Interfaces
- `VividPortDescriptor` gains:
  - `semantic_shape`
  - `semantic_intent`
  - `description`
- New control-server method:
  - `operator_docs`
- Expanded docs payload fields:
  - `brief`
  - `body`
  - `tips`
  - `params[].doc`
  - `inputs[].doc`
  - `outputs[].doc`
  - `recipes`
  - `pitfalls`
  - `operator_family`
  - `best_used_with`
  - `common_companions`
  - `lane_behavior_help`
  - `has_docs`
  - `source_path`
- `list_types` stays compact and does not become the full narrative endpoint.

## Test Plan
- Add source-parser tests in C++ for the runtime doc resolver:
  - parses `@brief`, body, `@param`, `@input`, `@output`, `@tip`
  - preserves multiline continuation text
  - tolerates missing doc blocks
- Add targeted wrapper-resolution tests:
  - `ClockAu` resolves docs from `ClockCore`
  - `ClockFr` resolves docs from `ClockCore`
  - `EnvelopeAu` resolves docs from `Envelope`
  - `EnvelopeFr` resolves docs from `Envelope`
- Extend [test_control_server.cpp](/Users/jeff/Developer/vivid/tests/test_control_server.cpp) to verify:
  - `list_types` returns `brief` and `lane_behavior_help`
  - ports expose `semantic_tag`, `semantic_shape`, `semantic_intent`, `description` when authored
  - `operator_docs("EnvelopeAu")` includes `gate` vs `beat_phase` guidance
  - `operator_docs("Filter")` includes `cutoff_mod` and `frequencies`
  - `package_operator_docs` returns the same merged structure for package operators
  - operators without docs return `has_docs=false` but still serialize cleanly
- Add an end-to-end motivating assertion:
  - querying `EnvelopeAu` and `Filter` through docs endpoints returns enough information to infer the correct per-note envelope wiring without source-file access

## Assumptions and Defaults
- Source comments, not generated site JSON, are the MCP/runtime documentation authority.
- The runtime may assume access to the local Vivid source tree while running in development and MCP contexts.
- The first pass prioritizes core audio/poly operators and shared wrapper/core patterns; broader operator coverage follows the same mechanism later.
- The docs extractor script remains for website generation, but it is no longer part of the MCP/control-server runtime path.
