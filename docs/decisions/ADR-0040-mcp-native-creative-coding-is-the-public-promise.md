# ADR-0040: MCP-Native Creative Coding Is the Public Promise

Status: accepted, **partially amended by [ADR-0055](ADR-0055-the-website-is-a-manifesto-for-a-new-instrument.md)** (2026-08-12) — the *public-facing headline lead* is reframed to the instrument/manifesto ("a new instrument for live visuals"); MCP + LLM-authored operators become the site's reveal rather than the H1. The product gate, lean-core rules, and fulfillment gates below stand unchanged.

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

**Fifth friction result (Phase 3 — beginner recovery, Fulfillment Gate #8).** Loading a saved
project on a fresh machine degraded *silently* over MCP: a track whose plugin is not installed fell
back to a placeholder with only a stderr line (`persist.cpp`); a project-local shader/operator that
failed to register or a package source that was gone showed up only as a nameless `get_health`
count or, worst, a shader compile error visible only in the desktop node canvas. Recovery guidance
existed **only** as tutorial preflight (`check_tutorial_prereqs`), never at load time. Vivid now
turns these into named, recoverable findings through its own inspection tools, in the same
`{issue, suggestion}` + `next_actions` vocabulary as the preflight:

- `validate_project` cross-references the *saved* session's intended plugins against this machine's
  catalog/disk (naming the missing plugin + track + install/relaunch action), folds in package
  operator-source existence, and walks the live `VisualGraph` for unregistered ops and shader
  compile errors (naming the node/op + `reload_project_files` action). It reports a new `degraded`
  flag and top-level `next_actions`; `valid` still flips only for hard on-disk-integrity problems
  (missing session file / media), so a degraded-but-loadable project stays `valid:true`.
- `inspect_signal_flow`/session summaries now carry a per-op `error` field and a `broken_ops` rollup
  so an agent sees *which* visual op is broken, not just a count.

The recovery-analysis core (`cli/project_recovery.{h,cpp}`) is a pure, resolver-injected unit with
portable CI coverage (`test_project_recovery`); the live-`VisualGraph` op health stays in the
handler. No new operators, no RT-path changes, no new MCP methods — recovery is surfaced through the
existing perception surface, keeping the core lean.

**Sixth friction result (tutorial tier 2 — live shader edit as a first-class walkthrough).**
`examples/tutorials/live-shader-edit/` was promoted from a pressure-test script into a self-contained
beginner walkthrough (ADR-0035 step 9): it scaffolds its own shader-only project (no Surge/synth
prerequisite), then teaches discover → edit → break → recover → verify → save over MCP. Driving it
live exposed two real gaps, now fixed in the smallest layer:

- **Shader operators were not self-describing.** The operator catalog a beginner naturally reaches for
  (`list_operator_catalog` / `find_operators`) exposed no backing file and did not even flag an op as
  shader-backed — the only node→file link was to *know* to call `list_shaders` and join on name. The
  catalog now attaches `format:"shader_file"` and `source:{path,tier}` to a shader op by joining
  against the shader library by type name (the ABI-frozen `VividOperatorDescriptor` is untouched).
- **`reload_project_files` did not reach live nodes.** It re-registered the shader *type* with the new
  body but left already-built *nodes* on their old compiled pipeline; the frame-loop poll only fires
  while the window actively renders. So an agent editing a project shader and calling reload saw
  nothing change, and a newly-broken body was never surfaced — the live-edit promise silently failed
  for MCP-driven use. `reload_project_files` now rebuilds the live nodes of each re-registered project
  shader type (`VisualGraph::rebuild_op_instances`, preserving params by name), so both the creative
  edit and its compile errors are deterministic over MCP. Combined with the Phase 3 diagnostics, a
  WGSL compile error now shows up by name in `validate_project` and `inspect_signal_flow.broken_ops`
  with the real compiler message, and clears on fix — exactly the failure/recovery mode Fulfillment
  Gate #8 asks a tutorial to cover.

The walkthrough's `build.py` is a runnable acceptance test (hard asserts on node identity, the
compile-error report, and recovery); it is app/GPU-tier, so it is proven by the live drive rather
than portable CI, consistent with the repo's app-tier test boundary.

**Seventh friction result (tutorial tier 3 — project-local C++ operator).**
`examples/tutorials/project-cpp-operator/` is the compiled-code sibling of the shader tutorials: it
scaffolds its own `gpu_visual` operator package, builds it with a real `clang++`, registers it into
the folder project, uses it, breaks it on purpose and recovers via the build diagnostics, then edits
+ recompiles on load. This is the hardest tier — it adds the compiler, the ABI guard, and crash
quarantine on top of the authoring loop. Findings:

- **Product fix — compiled project operators are now self-describing in the catalog.** Like the shader
  fix, a compiled operator that came from the open project's `vivid-package.json` was indistinguishable
  from a core op in `list_operator_catalog` / `find_operators`. The catalog now marks such an op
  `format:"compiled_operator"`, `source:{tier:"project"}` (joining the visual op name against
  `App::project_operator_types`); its `.cpp` stays enumerable via `list_project_assets`.
- **Recovery works out of the box.** A C++ compile error is reported by `build_operator_package` with
  verbatim `clang++` output in the per-op `error` field; crash quarantine is covered by
  `run_quality_check no_quarantined_operators` (ADR-0018). No new product work was needed here — the
  tutorial exercises the existing safety rails.
- **Two known gaps recorded — both now fixed as follow-ups.** (1) Hot-swapping an *already-live*
  compiled operator was not done over MCP. **Fixed:** `reload_project_files` now swaps the recompiled
  dylib in place inside its loader and rebuilds the live VISUAL nodes (`compiled_nodes_rebuilt`, params
  preserved) — the compiled-op analogue of the shader hot-swap, reusing the shipping
  `HotReloadManager::tick` sequence and never `unregister_type`ing a live type. AUDIO ops are refused
  (releasing their dylib from under the RT audio thread is unsafe) — the swap *and* the source
  file-watcher now skip `has_process_audio` ops, closing a latent use-after-`dlclose` hazard; reload
  the project to apply an audio-op edit. (2) `abi_mismatch` / `dlopen` failures surfaced only to
  stderr. **Fixed:** `load_and_register_operator_ex` threads the loader's structured
  `{error_key, error_msg}` into `reload_operator_package` / `install_operator_package` /
  `reload_project_files`.

The `build.py` asserts the compiled-operator loop (build → register → discover → break/recover →
recompile-on-load → no quarantine) deterministically; the freshly-recompiled op's rendered frame is
best-effort evidence, since a headless run driven by rapid synchronous control calls starves the
main-thread frame loop and a dylib op initializes its GPU pipeline on first draw (it renders correctly
once the app is idle/focused, as the existing `song-sketch` `AuroraField` op does).

**Signed-build meta-gate met.** ADR-0040's Fulfillment Gates all read "demonstrated from a signed
release build," but every proof above ran in dev worktrees. The macOS release pipeline
(`release-macos.yml` + `scripts/release/sign_and_notarize.sh`) was scaffolded but never exercised;
running it exposed and fixed four real, pre-existing blockers, none of them product code:

- **rpath** — the host binary's only `LC_RPATH` was an absolute dev build-deps path, so a distributed
  `.app` couldn't find bundled `libwgpu_native.dylib`; added `@executable_path`.
- **wgpu arch** — the self-hosted runner runs under Rosetta (host reports x86_64), so the
  WebGPU-distribution fetched the x86_64 wgpu against an arm64 target; pinned `ARCH` to
  `CMAKE_OSX_ARCHITECTURES` + a clean release build.
- **codesign keychain** — `errSecInternalComponent` on the headless runner; the workflow now imports
  the Developer ID cert (`APPLE_CERT_P12_B64`) into a dedicated temporary keychain with a
  `set-key-partition-list` grant.
- **hardened-runtime entitlements** — signing had none, so a notarized app's `dlopen` of
  runtime-compiled operator dylibs would be refused (breaking the whole package-operator feature);
  added `com.apple.security.cs.disable-library-validation` and sign the main executable with it.
- Plus the DMG staging path (`../..`) and a tag-gated GitHub Release step.

Result: GitHub Actions produces a **signed + notarized** DMG (`spctl --assess` → "accepted, Notarized
Developer ID"; ticket stapled). Installed to `/Applications/vivid.app`, it launches and the
**project-cpp-operator tutorial passes against it** — scaffold → real `clang++` build → `dlopen` of
the operator dylib *under the hardened runtime* (the entitlement's payoff) → recover → recompile — so
the compiled-operator loop, the hardest gate, is demonstrated from a signed build. A new
`check_tutorial_prereqs` `project_cpp_operator` checklist verifies the toolchain + bundle Resources on
the signed build (Xcode CLT is a documented tier-3 prerequisite — the compiler is not bundled, matching
vivid-classic). The acceptance *scripts* still run from a repo checkout against the signed app;
distributing them to a no-repo user (bundling `examples/` + `mcp/`) remains a follow-up, as does the
showcase regenerate→screenshot harness that ADR-0037 gates the website on.

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
