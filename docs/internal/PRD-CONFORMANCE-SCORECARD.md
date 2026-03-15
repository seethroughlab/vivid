# PRD Conformance Scorecard

## Executive Summary

This scorecard evaluates current Vivid against `docs/PRD.md` using a hybrid evidence pass: companion docs, code/runtime surfaces, test evidence, demo graphs, and the completed audit/hardening record.

Overall judgment:

- **Core architecture and runtime thesis:** mostly strong
- **LLM/runtime/package/tooling contract:** strong but still incomplete at the perception layer
- **Experimentation-interface vision:** incomplete but now more tightly scoped
- **PRD status overall:** **Partially Met**

The project is meeting the PRD best where the PRD makes concrete architectural commitments:

- three-domain graph with Control as hub
- JSON graph as source of truth
- hot-reload and package-based operator workflow
- retained native UI with live node previews
- LLM-facing Runtime API / control-server / MCP bridge

The project is furthest from the PRD where the PRD is really a product-shape document rather than a runtime document:

- the revised exploration surface is still too narrow beyond the node graph
- perception is stronger on introspection/checks than on higher-level analysis and AV evaluation
- several “equal breadth” and “visible exploration space” claims are only partially realized
- a few PRD-critical workflows still show live evidence gaps in current tests (`test_audio_hot_reload`, `test_control_server` live package rebuild path)

## Scoring Method

### Status Model

Each evaluated item is assigned one of:

- `Met`
- `Partially Met`
- `Not Met`
- `Open / TBD`
- `Out of 1.0 scope`

Each row also carries:

- `Confidence`: high / medium / low
- `Evidence type`: docs / code / tests / runtime behavior / manual

### Evaluation Rules

- `Open Questions` and `To Be Determined` items from the PRD default to `Open / TBD`.
- Items explicitly deferred in `docs/ROADMAP.md` are marked `Out of 1.0 scope`.
- “Present but weakly evidenced” is scored as `Partially Met`, not `Met`.
- Product-shape claims and architecture/runtime claims are evaluated separately so they do not collapse into one vague score.

### Evidence Used

Primary evidence for this pass:

- `docs/PRD.md`
- `docs/ARCHITECTURE.md`
- `docs/INTERFACE.md`
- `docs/LLM-INTEGRATION.md`
- `docs/ROADMAP.md`
- `docs/internal/CODE-AUDIT-TRACKER.md`
- `docs/internal/POST-AUDIT-CLOSEOUT.md`
- targeted test evidence from:
  - `test_runtime_api`
  - `test_hot_reload`
  - `test_audio_hot_reload`
  - `test_control_server`
  - `test_export_pipeline`
  - `test_package_contract_ecosystem`
  - `test_graph_snapshot_contract`
  - `test_ui_overlay_interactions`
  - `test_perception_introspection`
  - `test_runtime_stress`
- direct repo inspection of runtime, UI, package, perception, and variation/session surfaces

## Section-by-Section PRD Conformance

### 1. Vision + Core Principles

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| 1.1, 2.1 Audio-visual parity | Can creators author audio and visuals as peers in one graph with easy cross-domain interaction? | Partially Met | High | docs, code, tests | Three domains and Control-centered bridging are real; cross-domain wiring exists; parity of breadth and ease is still uneven across operator families and UX depth. |
| 2.2 Temporal plurality | Does Vivid avoid a master timeline and support clock-based reactive structure? | Partially Met | Medium | docs, code, tests | Clock/control infrastructure is present; variation and quantized recall exist; the broader pattern/state-machine/session-grid story is incomplete. |
| 2.3 General-purpose positioning | Does the current product shape support more than a performance-only tool? | Partially Met | Medium | docs, code, packages | Package ecosystem, export, install/rebuild flows, and demo graph spread support this, but product validation still leans heavily toward engineering/runtime evidence. |
| 2.4 Experimentation first | Is the product optimized for try-hear-see-iterate? | Partially Met | Medium | docs, code, tests | Graph editing, hot reload, live previews, variations, and package workflows support this; missing experimentation interfaces limit the full PRD claim. |
| 2.5 Text as source of truth | Is graph text authoritative and reflected across runtime/UI? | Met | High | docs, code, tests | JSON graph remains canonical; save/load/reload/snapshot/variation behavior is heavily tested and was hardened during audit. |
| 2.6 See every step | Is chain state materially inspectable through UI and tooling? | Partially Met | High | docs, code, tests | Node thumbnails, diagnostics, introspection, broken-wire visibility, and load diagnostics are real; universal “every point in chain” analysis is not fully there. |
| 2.7 Hot reload everything | Are routing changes instant and code changes hot-swapped without restart? | Partially Met | High | docs, code, tests, runtime behavior | Core hot-reload contract is strong and hardened, but current evidence includes a live failing `test_audio_hot_reload` case. |
| 2.8 LLM-native workflow | Does the system meaningfully expose graph/runtime/operator surfaces to LLM tooling? | Partially Met | High | docs, code, tests | MCP/control-server/runtime API are substantial; built-in chat is deferred and perception/critique tooling is only partial. |
| 2.9 Keep the core minimal | Is the product relying on seed operators + extensibility rather than giant built-ins? | Met | Medium | docs, code, packages | This remains true architecturally and in package/scaffold flows. |
| 2.10 Creator vs developer tools | Is realtime manipulation inside Vivid and code editing external? | Met | High | docs, code, tests | This split is explicit in product docs and implementation. |
| 2.11 Don’t reinvent the wheel | Does the architecture reuse established tools and libraries appropriately? | Met | Medium | docs, code | C++/CMake/WebGPU/miniaudio/GLFW/package model align with the principle. |

### 2. Experimentation & Interface Design

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| 3.1 perception-action loop | Are parameter/routing interactions effectively immediate? | Partially Met | Medium | docs, code, tests | Runtime contract supports same-frame graph mutation and next-boundary audio/GPU propagation, but the PRD latency targets are not comprehensively benchmarked in this pass. |
| 3.1 palette / extensibility | Is the operator palette easy to browse, extend, and populate by user/LLM? | Met | Medium | docs, code, tests | Seed operators, package libraries, scaffolding, and `list_types`/registry diagnostics provide a strong extensibility story. |
| 3.1 branching | Can users cheaply save and continue from good states? | Partially Met | High | code, tests | Variation CRUD, recall, queue, and UI strip exist; this is narrower than the PRD’s broader branching/session-grid vision. |
| 3.1 visible options / spatial exploration | Are alternatives made visible in the interface? | Not Met | Medium | repo inspection, docs | There is no implemented parameter-space explorer or visible multi-option exploration surface comparable to the PRD vision. |
| 3.2 node graph | Is the node graph a first-class experimentation interface? | Met | High | code, tests | This is the strongest shipped experimentation interface. |
| 3.2 session / variation grid | Is there a meaningful session/variation exploration interface? | Partially Met | High | code, tests | A bottom variation strip exists and variation workflows are real; this is now the main non-graph exploration surface, but it is not yet the richer session-grid model described in the revised PRD. |
| 3.2 live REPL | Is an integrated live REPL present? | Not Met | High | repo inspection | No built-in REPL surface was found. |
| 3.2 parameter space explorer | Is there a parameter space explorer? | Not Met | High | repo inspection | Not implemented. |
| 3.2 pattern algebra | Is a dedicated pattern algebra interface present? | Not Met | Medium | repo inspection, operators | Pattern-related operators/packages exist, but not the PRD’s named experimentation interface. |
| 3.2 state machine | Is a state-machine interface present? | Not Met | Medium | repo inspection | Not implemented as a first-class interface. |
| 3.3-3.5 exploration asymmetry strategies | Are the product’s exploration aids meaningfully shaped around audio/visual differences? | Partially Met | Low | docs, operators, packages | Some package/operator surface supports this, but the interface-level strategy support is not deeply realized. |

### 3. LLM Execution Bridge

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| 4.1-4.3 JSON routing layer | Is JSON the complete orchestration representation and a real LLM-facing control surface? | Met | High | docs, code, tests | This is one of the clearest PRD matches. |
| 4.3 visible control-operator orchestration | Are timing/automation/logic modeled as visible graph structure rather than hidden scripting? | Met | Medium | docs, code | Control/operator model supports this; WebSocket path remains deferred. |
| 4.4 operator author role | Can the LLM scaffold and extend operators through supported tooling? | Met | High | docs, code, tests | Operator creation, destination policy, package-aware scaffolding, hot reload, and custom-port hardening support this well. |
| 4.4 routing / variation / reflective roles | Are the four LLM roles all materially supported? | Partially Met | Medium | docs, code, tests | Author + architect roles are strongest; variation generation is structurally supported but not richly surfaced; critic/analyst role is limited by partial perception implementation. |
| 4.5 Runtime API as single source of truth | Is there one internal action surface behind tooling? | Met | High | docs, code, tests | Runtime API / control-server / MCP alignment is one of the strongest conformance areas. |
| 4.5 MCP path | Is the external LLM path real and broad enough to matter? | Met | High | docs, code, tests | MCP-facing tools and perception/check tooling are present. |
| 4.5 built-in chat | Is an in-app LLM chat path present? | Out of 1.0 scope | High | docs, roadmap | Explicitly deferred. |

### 4. System Architecture

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| 5.1-5.5 two-tier model / three domains / bridges | Is the core domain architecture implemented and stable? | Met | High | docs, code, tests, audit | This is strongly evidenced and heavily hardened by the audit. |
| 5.6 port type system | Does the current type system enforce real compatibility and domain crossing rules? | Met | High | docs, code, tests | Custom-port registry, stable ids, diagnostics, and audio safety rules are real. |
| 5.7 operator API contract | Is the operator authoring contract concrete and reusable? | Met | High | docs, code, tests | One of the project’s strongest areas after hardening. |
| 5.8 hot-reload behavior | Does hot reload preserve params, reset internal state, and fail safely? | Partially Met | High | docs, code, tests | Contract is explicit and mostly strong, but current `test_audio_hot_reload` failure keeps this from “Met”. |
| 5.9 spreads | Are spreads a real first-class data model feature? | Met | High | docs, code, tests | Broad test coverage and runtime implementation support this well. |
| 5.10 simulation zones | Are simulation zones implemented? | Out of 1.0 scope | High | PRD, roadmap, docs | Explicitly deferred past 1.0. |
| 5.11 JSON graph schema | Is the graph schema complete enough to be the live system truth? | Met | High | docs, code, tests | Strongly evidenced through graph, runtime API, and undo/snapshot/variation tests. |
| 5.16 export | Can Vivid export a standalone build consistent with runtime contracts? | Partially Met | Medium | docs, code, tests | Export contract is materially stronger and tested, but dedicated end-to-end product validation is still lighter than core runtime evidence. |
| 5.17 operator libraries / packages | Is the package/library model real and product-defining? | Partially Met | High | docs, code, tests, audit | Strong package surface and sibling-repo audit exist; current `test_control_server` live rebuild failure shows this area is still not fully closed. |

### 5. Interface Architecture

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| 6.1 native rendering | Does the UI run natively in the same rendering context? | Met | High | docs, code | Core architecture matches the PRD. |
| 6.2 retained mode | Is the UI retained rather than immediate mode? | Met | High | docs, code | Clear repo and docs evidence. |
| 6.3 purpose-built toolkit | Is the product relying on custom UI surfaces rather than a generic framework? | Met | High | docs, code | Strong match. |
| 6.4 application layout | Is the layout model materially present? | Partially Met | Medium | docs, code | Core node-graph-plus-inspector layout is present, but the broader multi-interface workspace model is not fully realized. |
| 6.5 node thumbnails | Are live node previews a real always-on interface principle? | Met | High | docs, code, tests | Strong match. |
| 6.6 visual style | Does the product reflect the dark-steel, sharp, content-forward visual system? | Partially Met | Medium | docs, code, runtime styling | The direction is present, but not every stylistic detail has been validated as a product-level conformance check. |

### 6. Roadmap / North Star / Perception System

| PRD area | Evaluation question | Status | Confidence | Evidence | Notes |
|---|---|---|---|---|---|
| North Star demo | Can the current product plausibly support the end-to-end demo scenario? | Partially Met | Medium | docs, operators, packages, tests | Most ingredients exist, but this exact scenario is not evidenced as a validated end-to-end flow in this pass. |
| 9.1-9.2 introspection layer | Can the LLM inspect graph/node/runtime state in a structured way? | Met | High | docs, code, tests | `introspect_nodes`, diagnostics, checks, and registry diagnostics are real. |
| 9.2 analysis layer | Can the system perform higher-level perceptual analysis across audio, visual, and AV relationships? | Partially Met | Medium | docs, code, tests | There is meaningful perception/check infrastructure, but the full analysis layer described in the PRD is not present. |
| 9.2 solo mode | Can nodes be isolated for inspection? | Partially Met | Medium | code, docs | Solo infrastructure exists in runtime/UI, but this pass did not validate the product workflow deeply enough to score it higher. |
| 9.2 performance metrics | Are node/performance metrics available for LLM/runtime inspection? | Partially Met | Low | docs, code | Some diagnostics exist; the richer PRD performance/per-node metric story is still partial. |
| 9.2 comparison tools / sweeps | Are A/B and sweep analysis tools present? | Not Met | Medium | repo inspection | Not found as first-class tooling. |
| 9.3 temporal and cross-domain metrics | Are time-window and AV-reactivity metrics present? | Partially Met | Low | docs, tests, runtime behavior | Stress and diagnostics infrastructure exist, but the richer PRD metric model is not fully implemented. |
| 9.4 checks as durable intent | Are checks/assertion-like quality gates explicit and machine-readable? | Met | High | docs, code, tests | Strong match, though naming has evolved from “assertions” to “checks”. |

## Open / TBD And Out-of-Scope Items

### Open / TBD

These should not be counted as misses in the current score:

- semantic tag depth
- audio/visual session grid interaction UX
- project file format shape
- performance targets as explicit acceptance numbers
- some error handling/recovery policy details beyond current hardened behavior

### Out of 1.0 Scope

Explicitly deferred or retained as future design:

- subpatches
- simulation zones
- multi-window / multi-monitor
- Windows / Linux support
- WebSocket API
- built-in chat panel
- library version pinning
- accessibility

## Highest-Priority PRD Gaps

Ordered by product importance rather than code ownership:

1. **Experimentation interface gap**
   - The shipped product strongly supports the node graph, but the broader revised PRD experimentation model is still only partially realized: richer session/variation exploration, parameter-space exploration, live REPL, state machine, and pattern-algebra interfaces remain missing or underdeveloped.

2. **Perception gap**
   - Introspection and checks are real; higher-level analysis, AV correlation tooling, comparison tools, and richer performance insight are still partial.

3. **Parity gap**
   - The architecture clearly supports A/V parity, but “equal breadth” and “equal ease” are only partially realized at the product layer.

4. **Workflow reliability gap in PRD-critical seams**
   - Current evidence includes a failing audio hot-reload test and a failing live package rebuild behavior assertion. Those do not invalidate the architecture, but they weaken the claim that the hot-reload/package workflow is fully meeting the PRD.

5. **Latency evidence gap**
   - The project likely meets many responsiveness goals structurally, but explicit PRD latency claims are not yet backed by a dedicated benchmark/reporting lane.

## Recommended Next PRD-Driven Priorities

1. Close the current hot-reload and live package rebuild regressions before making stronger product-level “hot reload everything” claims.
2. Decide which experimentation interfaces are truly in-scope for 1.0 versus post-1.0, then align the PRD and roadmap if needed.
3. Strengthen the perception layer from introspection/checks into richer analysis and AV-reactivity tooling.
4. Add a small PRD-facing latency validation lane so same-frame / hot-reload responsiveness claims are evidence-backed.
5. Run one explicit North Star demo validation pass and preserve it as a regression scenario.

## Evidence Appendix

### Strongest Evidence Of Alignment

- Runtime architecture and transactional hardening recorded in `docs/internal/CODE-AUDIT-TRACKER.md`
- Post-audit summary in `docs/internal/POST-AUDIT-CLOSEOUT.md`
- JSON graph / reload / variation behavior in `test_runtime_api` and `test_graph`
- Hot-reload safety and compatibility enforcement in `test_hot_reload`
- Package/test/library model in `test_package_contract_ecosystem`, `test_package_test_runner`, and sibling-package audit results
- LLM-facing introspection/checks in `test_control_server` and `test_perception_introspection`
- UI/runtime visibility guarantees in `test_graph_snapshot_contract` and `test_ui_overlay_interactions`
- Stress/reliability coverage in `test_runtime_stress`, `test_hot_reload_stress`, `test_package_stress`, and `docs/testing/STABILITY-STRESS-TESTS.md`

### Current Negative Evidence Used In This Scorecard

This pass also used live failures as real evidence:

- `test_audio_hot_reload` currently traps during compatible audio reload flow
- `test_control_server` currently fails the live package rebuild output-refresh assertion

Those failures are treated as conformance-relevant evidence for PRD-critical workflows rather than ignored as “just test noise”.

### Manual / Product Workflow Caveat

This scorecard is strongest on architecture/runtime/tooling evidence.

A short interactive manual pass is still worth doing later for:

- same-graph AV authoring feel
- inspectability and solo-mode workflow quality
- hot-reload/edit-in-IDE loop from the actual UI
- package browsing and extension workflow as a creator experience

Until that manual pass is done, some product-shape judgments remain `Partially Met` at medium confidence rather than `Met`.
