# Vivid V4 API and Structure Audit

Date: 2026-07-06

Audience: coding agents and engineers preparing cleanup or product-readiness work.

## Baseline Verdict

Vivid V4 is healthier than a normal fast-moving prototype. The main architecture is coherent:
`App`/`Window`, audio thread vs UI thread, control server queueing, operator registry/loading,
and package compilation are all real seams.

The main risk is now API clarity. Vivid has enough surface area that user and agent authoring
will be difficult unless the public contracts become smaller, current, discoverable, and tested
from the perspective of an external operator author.

Verification evidence from this audit pass:

- `ctest --test-dir app/build --output-on-failure`: `30/30` passed.
- `uv run mcp/evals/llm_mcp_eval.py --selftest`: `4/4` passed.

## P0 Issues

### 1. Canonical Docs Still Conflict About Product State

Evidence:

- [`README.md`](../README.md:9) still says the first proof target is Session View and points
  newcomers toward historical prototypes.
- [`CLAUDE.md`](../CLAUDE.md:1) and [`app/ARCHITECTURE.md`](../app/ARCHITECTURE.md:9) describe the
  current two-surface plus bridge trunk.
- [`app/README.md`](../app/README.md:27) says the app must be foreground/visible for MCP draining.
- [`app/src/main.cpp`](../app/src/main.cpp:81) says the frame/control loop is kept alive for
  background agent control.

Impact:

- Newcomers and coding agents can start from stale product truth.
- Agents may treat historical prototypes as active direction.
- The background/foreground MCP behavior is ambiguous.

Recommended fix:

- Make the root `README.md` the current product entrypoint.
- Mark old Session View prototype docs as historical evidence only.
- Verify the current foreground/background MCP behavior and update `app/README.md` accordingly.

### 2. Public Operator API Is Monolithic and Migration-Contaminated

Evidence:

- [`app/src/operator_api/types.h`](../app/src/operator_api/types.h:13) has a very long ABI-history
  comment inside the public header.
- [`app/src/operator_api/types.h`](../app/src/operator_api/types.h:168) describes value fields as
  additive until an old phase.
- [`app/src/operator_api/value_model.h`](../app/src/operator_api/value_model.h:13) says it is inert
  and not included, but [`types.h`](../app/src/operator_api/types.h:4) includes it.
- [`app/src/operator_api/value_view.h`](../app/src/operator_api/value_view.h:22) says contexts do
  not expose `values[]`, but current contexts do.

Impact:

- Operator authors cannot easily tell which API is live.
- Coding agents may preserve or extend stale migration concepts.
- Public headers read like internal archaeology rather than a stable SDK.

Recommended fix:

- Split public authoring docs into current contract vs migration history.
- Rewrite public header comments to describe only the live API.
- Move ABI changelog/history into a separate document.
- Add an explicit “current operator ABI v11” reference page.

### 3. MCP and Control Contracts Are Manually Duplicated and Incomplete

Evidence:

- Native handlers exist for audio-operator workflows in
  [`app/src/cli/control_server.cpp`](../app/src/cli/control_server.cpp:692): `add_audio_effect`,
  `remove_audio_effect`, `set_track_audio_instrument`, `set_audio_op_param`, `list_audio_ops`,
  `record`, `note_on`, `note_off`, `metronome`, `set_clip_loop`, and `slice_to_midi`.
- Those handlers are not exposed as MCP tools in [`mcp/vivid_mcp.py`](../mcp/vivid_mcp.py:214).
- Existing MCP tools are handwritten wrappers around `_post(...)`, with no generated parity check.

Impact:

- Agents cannot access important native product features.
- The public agent surface can drift silently from the live control server.
- Agent-first authoring is blocked for native audio-operator workflows.

Recommended fix:

- Define a single control/MCP tool manifest or schema source.
- Generate or validate MCP wrappers from that schema.
- Add a parity test that fails when intended public handlers are missing from MCP.
- Expose native audio-operator tools with discovery, add/remove, instrument assignment, and named
  parameter setting.

### 4. External Operator Packaging Is Too Narrow for the Product Promise

Evidence:

- [`app/src/packages/package_manifest.h`](../app/src/packages/package_manifest.h:12) documents a
  manifest shape with only `name`, `source`, and `gpu`.
- The available package example is a GPU visual operator.
- There is no equivalent package example for audio effects, instruments, custom inspectors, or
  custom editor windows.

Impact:

- Users and coding agents can copy a visual package, but not the broader extension types Vivid
  promises.
- Package metadata cannot express enough about operator kind, UI surface, or authoring intent.

Recommended fix:

- Add package examples for:
  - `gpu_visual`
  - `audio_effect`
  - `instrument`
  - `custom_inspector`
  - `custom_editor`
- Extend the manifest enough to describe domain/capability without overbuilding package management.
- Add a package-authoring smoke test for each supported extension type.

## P1 Issues

### 5. Native Audio Operator Runtime Is a Good Start but Too Narrow as an SDK Contract

Evidence:

- [`app/src/audio/audio_op_runtime.cpp`](../app/src/audio/audio_op_runtime.cpp:49) classifies sources
  by “no audio input.”
- [`app/src/audio/audio_op_runtime.cpp`](../app/src/audio/audio_op_runtime.cpp:157) hardcodes output
  port 0 as stereo.
- [`app/src/audio/audio_op_runtime.cpp`](../app/src/audio/audio_op_runtime.cpp:165) feeds only one
  stereo input for effects.

Impact:

- Current built-ins are covered, but serious user-authored audio operators may hit undocumented limits.
- Agents cannot infer which audio port shapes are supported.

Recommended fix:

- Formalize supported v1 audio operator shapes.
- Validate audio descriptors against those shapes at load time.
- Document unsupported shapes clearly.
- Add tests for rejected invalid audio descriptors.

### 6. Operator Discovery Is Visual-First, Not Unified

Evidence:

- [`mcp/vivid_mcp.py`](../mcp/vivid_mcp.py:80) documents `list_operators()` as the catalog of visual
  operators that can be spawned.
- Native audio operators exist, but agents cannot discover them through the same semantic catalog.

Impact:

- Agents have a partial view of the extension surface.
- Audio and visual authoring APIs feel like separate worlds even though they share the operator API.

Recommended fix:

- Add `list_operator_catalog(domain?)` or extend `list_operators`.
- Include domain, kind, `process_*` capabilities, params, ports, UI capabilities, semantic metadata,
  and package origin.
- Prefer named params for native audio-op setting, matching visual node params.

### 7. Input Orchestration Remains a Hotspot

Evidence:

- [`app/src/app/input.cpp`](../app/src/app/input.cpp:7) imports app, session, mapping, plugin, graph,
  editor, frame, operator clone, transport, plugin windows, and visual graph concerns.
- [`app/src/app/input.cpp`](../app/src/app/input.cpp:53) begins a key callback that mixes operator
  chooser routing, musical typing, editor routing, transport, recording, graph actions, visual source
  toggles, and scene launch.

Impact:

- UI behavior changes are hard for agents to localize.
- Regression risk is high because unrelated input features share the same callback flow.

Recommended fix:

- Split input routing into focused controllers:
  - transport keys
  - musical typing
  - clip grid and clip pool
  - plugin browser/drop
  - graph input
  - editor routing
- Keep GLFW callback installation in `input.cpp`; move behavior into named helpers/modules.

### 8. Control Server Is Becoming an API Registry Blob

Evidence:

- [`app/src/cli/control_server.cpp`](../app/src/cli/control_server.cpp:86) owns request queueing,
  dispatch, validation, JSON construction, and feature behavior.
- The file is over 900 lines and growing with each new control surface.

Impact:

- API behavior is harder to audit.
- Handler additions are likely to duplicate validation and response-shape logic.
- The MCP parity problem gets worse as the file grows.

Recommended fix:

- Keep queueing/threading in `control_server`.
- Move handler families into modules such as session, visuals, mappings, audio operators, project,
  and packages.
- Add typed request/response helpers or schema-backed JSON helpers.

### 9. Newcomer Docs Are Contributor Maps, Not an SDK

Evidence:

- Directory `CLAUDE.md` files explain where code lives and what invariants matter.
- There is no complete “operator authoring guide” that walks from choosing an operator kind to
  packaging, testing, loading, and exposing it to agents.

Impact:

- A human or coding agent can navigate internals but still struggle to author a correct extension.
- API expectations are scattered across headers, examples, tests, and comments.

Recommended fix:

- Add `docs/operator-authoring/`.
- Include quickstarts for visual, audio effect, instrument, custom inspector, and custom editor.
- Include RT-safety rules, descriptor metadata rules, packaging steps, MCP discovery expectations,
  and test commands.

## P2 Issues

### 10. Tests Are Strong Internally but Do Not Lock the External Authoring Contract

Evidence:

- The suite covers runtime pieces well.
- [`mcp/evals/cases.py`](../mcp/evals/cases.py:24) covers only four simple MCP tasks:
  operator discovery, Plasma-to-Output creation, a visual param set, and bridge explanation.

Impact:

- Internal regressions are caught, but external SDK regressions can still pass.
- Agent workflows for audio operators, packaging, inspectors/editors, and project-local code are not
  pressure-tested.

Recommended fix:

- Add SDK contract tests that compile/load sample packages.
- Add MCP evals for:
  - discovering native audio operators
  - creating a native audio instrument/effect chain
  - setting named audio-op params
  - installing a project-local package
  - explaining audio-to-visual mappings with semantic metadata
  - using a custom editor/inspector introspection surface

### 11. Semantic Metadata Exists but Does Not Drive Enough Agent Behavior

Evidence:

- Semantic vocabulary and tests exist.
- MCP discovery mostly exposes param names/descriptions rather than semantic intent, units, display
  hints, mapping affordances, or valid destinations.

Impact:

- Agents still need to infer too much from names.
- Mapping suggestions and operator selection are less reliable than they could be.

Recommended fix:

- Surface semantic metadata in operator discovery.
- Add mapping-affordance metadata to discovery responses.
- Add eval cases that require the agent to choose params by semantic intent rather than exact name.

### 12. Historical Phase Labels Leak Into Active Source

Evidence:

- Active comments still use labels like `P2`, `P4`, `M6`, `AO-1`, and `AG-1`.
- Some are useful breadcrumbs, but public headers and active API comments should describe current
  behavior rather than old phase history.

Impact:

- Agents may treat old phase labels as current roadmap truth.
- API comments are harder to trust.

Recommended fix:

- Keep phase history in ADRs and roadmaps.
- Rewrite active API comments around current behavior and stability.
- Leave phase labels only where they refer to a still-current planned migration.

## Recommended Handoff Order

1. Fix docs truth and operator API comment contradictions first.
2. Create a unified API/tool manifest and MCP parity test.
3. Add operator-authoring docs plus minimal audio/visual/editor package examples.
4. Broaden MCP discovery around operator domains and semantic metadata.
5. Split `input.cpp` and control handler families after the public contracts are clearer.

