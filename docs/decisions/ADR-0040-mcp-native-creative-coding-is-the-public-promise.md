# ADR-0040: MCP-Native Creative Coding Is the Public Promise

Status: accepted

Date: 2026-07-26

Related research: [Native Instrument Candidate Pass](../research/native-instrument-candidates.md).

## Context

The website revival started as a small task: retrieve the general site structure from
`vivid-classic` and start with a placeholder page. That quickly exposed a product-ordering problem:
the real website depends on proof artifacts that do not exist yet, especially high-quality showcase
demos, beginner tutorials, generated references, and a community-package story.

Classic's public promise was "audio and visuals are equal peers in the same graph." Vivid 4 is no
longer that product. It has two primary authoring surfaces, a mapping bridge, and an MCP control
surface. Recent implementation has leaned audio-heavy because plugin hosting and musical timing are
hard, but the public promise must not collapse into "an agent-friendly DAW." Vivid is still a
creative coding app.

## Decision

Vivid's public product promise is:

> Vivid is an MCP-native creative coding app.

This is not only marketing copy. It is a product gate. The beginner path and showcase demos must
prove that a user and agent can author, inspect, modify, and verify a real audiovisual project using
Vivid's product concepts and MCP tools.

This promise does **not** mean the core should become a large creative toolkit. The core stays lean.
It owns the authoring loop, safety rails, and perception layer; project-local code, packages,
examples, and tutorials supply most of the creative material.

The lean-core responsibilities are:

- **Project-local creative code.** Core must make project-local shaders, visual operators, package
  manifests, media assets, and saved projects feel native.
- **Fast authoring loop.** Core must support scaffold/fork, edit, reload, preserve state where
  possible, and report errors without requiring an app rebuild.
- **Safe failure.** Core must keep bad code from costing the user's project: compile errors,
  shader errors, operator crashes, ABI mismatches, missing assets, and missing plugins must be
  inspectable and recoverable.
- **Semantic MCP surface.** Core MCP tools must operate in Vivid concepts: tracks, clips, scenes,
  visual nodes, mappings, operators, project assets, health, and output. Raw graph surgery remains
  available, but it is not the beginner path.
- **Perception and proof.** Core must let an agent verify creative edits with health, audio checks,
  visual checks, mapping checks, frame/audio capture, and concise evidence.
- **Minimal built-in content.** Core operators should be infrastructural and broadly reusable:
  output, media input, basic visual generators/effects, shader-as-operator support, and
  mapping/control utilities. Native audio instruments stay test utilities unless a genuinely strong,
  low-maintenance instrument earns core status. The current research conclusion is that Surge XT
  remains the beginner assumption; `vivid-wavetable` is the strongest first-party package candidate,
  not a core-port candidate.

The non-goals are:

- Building every desirable synth, effect, shader, visual system, or demo into core.
- Treating the public website as a substitute for product proof.
- Making agents reason only in raw node IDs and topology for normal creative tasks.
- Expanding the built-in operator catalog when a tutorial, project-local operator, or package would
  prove the idea with less long-term weight.

The first-run proof must include:

- Downloading and launching the signed macOS build.
- Connecting through the MCP bridge or control-server surface.
- Opening or creating a saved project.
- Editing audiovisual structure, not only changing audio/plugin settings.
- Creating or forking project-local creative code, starting with shader/operator workflows where
  users are most likely to write their own nodes.
- Inspecting the result through product concepts: tracks, clips, scenes, visual nodes, mappings,
  health, and output.
- Producing a reusable sample project.
- Recording gaps where the product cannot yet make the promise true.

If this proof fails, we fix Vivid before expanding the website. The website should not soften the
claim to fit incomplete product behavior.

## Fulfillment Gates

ADR-0040 is fulfilled when the following can be demonstrated from a signed release build:

1. **Golden path project.** A beginner tutorial creates a saved folder project from scratch or from a
   starter example, then reloads it successfully.
2. **MCP connection.** The tutorial can connect through MCP/control-server tools and read a compact
   session/project summary before editing.
3. **Visual creative-code edit.** The user or agent forks a shader or visual operator, changes it,
   reloads it, and sees the changed output without rebuilding the app.
4. **Audio context.** The project uses Surge XT as the assumed beginner instrument, unless/until a
   strong native instrument earns a core spot. If that happens, it must beat package residency under
   the core promotion rule.
5. **Mapping bridge.** A musical/control signal is mapped to a visual destination through
   discoverable mapping sources/destinations, not hand-invented strings.
6. **Inspection.** The agent can explain the relevant tracks, clips/scenes, visual nodes, mappings,
   project assets, and health status in product language.
7. **Verification.** The agent can run checks that prove the output is nonblank, audio is not
   clipping/silent where applicable, mappings resolve, and no operator has been quarantined.
8. **Recovery path.** The tutorial intentionally or incidentally covers at least one failure mode:
   shader compile error, missing plugin, missing asset, package build error, or bad operator.
9. **Reusable artifact.** The resulting project is checked in or generated by a checked-in script and
   can become a tutorial/showcase/regression fixture.

## Current Surface Inventory

Existing Vivid 4 surfaces that already support this ADR:

- **Shader authoring:** shaders are operators; shader metadata is discoverable; `fork_shader` creates
  editable user-tier shaders and registers them live.
- **Project-local assets:** folder projects can carry sessions, shaders, media, and
  `vivid-package.json` manifests; `list_project_assets`, `resolve_asset`, and `reload_project_files`
  expose that over MCP.
- **Package authoring:** `scaffold_operator_package`, `validate_operator_package`,
  `build_operator_package`, `reload_operator_package`, `install_operator_package`, and
  `clone_operator` cover the local compiled-operator loop.
- **Semantic discovery:** `get_session`, `summarize_session`, `list_operator_catalog`,
  `find_operators`, `list_mapping_sources`, `list_mapping_destinations`, `suggest_mappings`, and
  `explain_mapping` let agents avoid inventing raw topology or mapping strings for common work.
- **Verification:** `get_health`, `list_quality_checks`, `run_quality_check`, `capture_frame`,
  `analyze_frame`, `capture_audio`, and audio-analysis tools provide proof hooks.
- **Safety:** shader errors, operator ABI checks, package validation/build errors, crash recovery,
  and operator quarantine give the authoring loop failure boundaries.

### Project Asset Reference Contract

Folder projects persist authored asset references in the portable form the author chose, usually
relative to the project folder: `assets/shaders/foo.glsl`, `media/kick.wav`, or
`textures/logo.png`. The top-level `shaders/` directory is reserved for shader-operator files with
Vivid JSON metadata headers; raw files consumed by FILE params should live under `assets/`.
Runtime code must not require each operator to know project layout. The host resolves FILE params
and legacy node assets at the `VisualGraph` boundary, then passes usable runtime strings into
operators. Save/load keeps the authored reference stable; MCP exposes the same meaning through
`resolve_asset` and `list_project_assets`.

Likely gaps to pressure-test first:

- Whether a beginner can discover and use these loops through the UI, or whether the golden path is
  MCP-only.
- Whether the shader/operator fork workflow creates project-local artifacts by default or user-tier
  artifacts that are harder to ship as tutorial projects.
- Whether generated projects can be reloaded cleanly from a signed build without local dev paths.
- Whether missing Surge XT produces clear recovery instructions rather than silent broken audio.
- Whether visual output checks are stable enough for tutorial/showcase QA.
- Whether the mapping bridge can be taught through intent helpers without dropping to raw IDs too
  early.
- Whether showcase demos visibly demonstrate code authoring, not only finished audiovisual output.

**Golden Path A artifact.** The first shader-only artifact lives at
`examples/tutorials/mcp-native-first-project/`. It assumes Surge XT but is intentionally free of C++
toolchain requirements so the first pass can test the MCP/project-local shader loop without making
the user compile code. Its preflight reports missing Vivid/control-server and Surge XT setup before
deleting the generated project, and `examples/tutorials/free-plugin-starter-list.md` keeps the
required/optional free plugin story explicit. The follow-up C++ artifact should use or simplify
`examples/song-sketch`.

**First friction result.** The initial live run successfully authored the project through the control
server, loaded Surge XT, produced audio/visual output, and wrote proof hooks. It also exposed that
the tutorial was following stale `CustomShader` language rather than ADR-0016's current shader-file
operator model. The durable fix is not to restore the fixed four-param `CustomShader` contract. A
project-local shader file should declare its metadata, register as a named operator, and appear in
the same catalog/mapping surface as bundled shaders and compiled operators.

**Second friction result.** The live shader edit artifact at `examples/tutorials/live-shader-edit/`
showed that project-local shader edits need a real refresh contract, not a tutorial-only rebind.
`reload_project_files` refreshes the project shader tier and preserves graph identity for nodes using
the same metadata-named shader operator. This keeps the core lean: no watcher requirement for the
tutorial, no path mutation, and no special tutorial behavior.

**Third friction result.** The first-project builder's Surge XT checks were too product-shaped to
live only in tutorial Python: download hints, expected install paths, plugin-catalog visibility,
project-shader workflow availability, and safe "do not delete generated work yet" behavior are
onboarding diagnostics. Vivid now exposes `check_tutorial_prereqs` through the control server/MCP.
The first checklist is `mcp_native_first_project`, which reports `ready`, structured checks, missing
prerequisites, and next actions before a tutorial mutates project files. Vivid also exposes
`scaffold_project_shader_operator`, which writes a metadata-bearing shader under `<project>/shaders`,
registers it live as its declared operator name, and returns the path/operator pair for tutorials and
MCP clients. The builder still handles the one case Vivid cannot answer itself: the app/control
server is not running.

**Fourth friction result.** Project-local C++ operators need the same project scope rule as
project-local shaders: a folder project's `vivid-package.json` registers named operators that belong
to that open project, not to the whole Vivid process forever. Vivid now tracks compiled operators
registered from a project package, unregisters them and removes their dylib loaders after the old
graph is destroyed on New/project switch, and tracks later project-package registrations from
`reload_project_files` / `reload_operator_package`. The user-facing model is parallel:
`project/shaders/Foo.wgsl` registers `Foo` as a shader operator; `project/vivid-package.json` plus
`Foo.cpp` registers `Foo` as a compiled operator. The C++ path remains a later tutorial tier because
it adds compiler, ABI, crash/quarantine, and hot-reload diagnostics beyond the shader path.

## Implementation Order

Work on this ADR should proceed in this order:

1. **Inventory existing proof surface.** Confirm which MCP tools and UI affordances already cover
   scaffold/fork, reload, project assets, mappings, health, frame/audio capture, and quality checks.
2. **Choose the smallest golden path.** Prefer a shader fork or project-local visual operator over a
   new core operator. Add audio only as much as needed to prove the bridge.
3. **Write the tutorial script/checklist before polishing the site.** The tutorial exposes product
   gaps; the site reflects what the tutorial proves.
4. **Fix product gaps in the smallest layer.** Prefer metadata, MCP helper, diagnostic, example, or
   project-local code fixes before adding built-in operators.
5. **Promote only repeated needs to core.** A capability graduates to core only when multiple
   tutorials/projects need the same infrastructure and a package/project-local solution would create
   repeated friction.

## Core Promotion Rule

A new core feature or built-in operator is justified only when it satisfies at least one of these:

- It is infrastructure required for project-local authoring, reload, safety, asset resolution,
  introspection, verification, or mapping.
- It is necessary for first-run/tutorial stability and cannot reasonably live as a package.
- It represents a stable cross-project primitive rather than a specific look, genre, effect, or demo
  taste.
- It reduces repeated user confusion in the golden path and cannot be solved with metadata, docs, or
  a better MCP helper.

Everything else starts as tutorial code, a project-local shader/operator, or a package.

## Alternatives Considered

- **"Agent-first audiovisual environment."** Accurate, but too broad. It does not force the creative
  coding workflow to stay central.
- **"DAW plus visual graph."** Useful architecture shorthand, but it undersells MCP and the code
  authoring loop.
- **"Audio-visual graph platform."** Rejected as a classic-era promise that conflicts with Vivid 4's
  two-surface design.

## Consequences

- The first CTA can still be "Download Vivid", but the first product story is "build with it", not
  "browse feature claims".
- Beginner tutorials become product proofs, not passive docs.
- Showcase demos must visibly include MCP-native creative coding, not only finished output.
- Audio work remains important, but future onboarding and demos must rebalance toward visual/code
  authoring and the mapping bridge.
- The core stays small by treating extension, inspection, and verification as the product's stable
  center while creative content lives in projects and packages.
- Product gaps found during tutorials should usually become MCP/diagnostic/metadata/example work
  before they become new built-in operators.
