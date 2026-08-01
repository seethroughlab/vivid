# Phase 1: Product Map And Promises

Status: done (audited 2026-08-01)

## Verdict

**PASS with follow-ups — no P0/P1.** The shipped product shape matches the core release promise:
the two authoring surfaces exist and are cleanly domain-zoned (amber Session View + audio graph on
the left, cyan visual graph on the right — see `evidence/phase-01/02-example-loaded-neon.png`), they
share one musical transport (124 BPM · 4/4 · launch-quantization pill), and the audio↔visual bridge
is a first-class, visible, and — confirmed in code — **bidirectional** object
(`evidence/phase-01/03-mapping-bridge-overview.png`; `viz.*` sources publish at
`app/src/ui/node_graph.cpp:419` and drive plugin/native params at `app/src/app/frame.cpp:397-403`).
The MCP-native creative loop (inspect → author → map → save → reload → verify) is real and is the
honest public promise per ADR-0040. Every primary PRD concept (session, track, scene, clip,
transport, visual graph, operator, mapping, preview/output, package) is present in the app and mostly
labelled consistently with the glossary.

The gaps are **vocabulary and onboarding reconciliations, not missing core capabilities**: an entire
glossary term-family for the experimentation loop (Take / Live Take / Variation / Variation Well /
Cue Path) is absent from both UI and data model; the docs' first-class "Bridge" is only ever called
"Mappings" in the app; there is no in-app first-run guidance and the designated "front door" is an
MCP/CLI builder script; the PRD/glossary "five track kinds" are not modelled as track kinds; and the
Gemini "Eval" menu exposes an experimental, key-gated capability as a top-level product affordance.

Findings: **2×P2 + 3×P3.** No release-blocker. The two P2s (F1 experimentation-loop vocabulary, F3
no-in-app-onboarding) are promise/expectation mismatches whose smallest fixes are copy/label
reconciliations, and both hand evidence to later phases (F1→glossary/PRD; F3→Phase 6).

## Purpose

Verify that the shipped product shape matches the release promise in the PRD, glossary, UI
principles, ADRs, examples, and website-facing claims.

## User Task

A new user should be able to understand what Vivid is, what the audio surface does, what the visual
surface does, and how the bridge between them becomes a creative object.

## Hypothesis

If the product map is coherent, the release candidate will not present competing mental models or
make promises that the app cannot satisfy.

## Pressure Test

Read the user-facing docs and first-run surfaces, then map every visible concept to the canonical
product vocabulary.

## Scope

- Product docs: `docs/product/PRD.md`, `docs/product/glossary.md`, and
  `docs/product/ui-principles.md`.
- Current product decisions, especially the two-surface bridge, session/audio graph, visual graph,
  agent capability, and release-gated tutorial ADRs.
- User-facing release docs, website content, example descriptions, menu labels, diagnostics text,
  and first-run empty states.
- Any command or agent response that explains the project to a user.

Out of scope: rewriting the product vision or auditing code quality unless a product promise cannot
be traced to the app.

## Audit Procedure

1. Build a concept inventory from docs and visible UI labels: session, track, clip, audio graph,
   visual graph, operator, bridge, mapping, package, preview, transport, and project.
2. For each concept, record the canonical definition, where users encounter it, and whether the app
   teaches it through behavior.
3. Compare release-facing copy against the actual release candidate. Mark each claim as shipped,
   scaffolded, hidden, or unsupported.
4. Identify product promises that require evidence from later phases.
5. Write a short "first-release promise" paragraph that later phases can approve or challenge.

## Evidence To Collect

- A concept map table linking docs, UI labels, examples, CLI/MCP names, and release copy.
- Screenshots or notes for first-run and primary navigation states.
- A list of unsupported or ambiguous claims with suggested wording.
- Open questions that need an ADR, glossary update, or release-note callout.

## Deliverables

- Product promise matrix: claim, source, release status, evidence phase, and release action.
- Vocabulary mismatch list with severity.
- Draft release promise text, no longer than one paragraph.

## Acceptance Criteria

- Primary concepts match `docs/product/glossary.md` and current ADRs.
- Audio, visual, and bridge domains are distinct without feeling like separate products.
- User-facing copy does not promise missing or unreleased capabilities.
- There is a clear first-release definition of "done" for the product experience.

## Failure Modes

- Docs describe a product that the UI does not expose.
- UI labels introduce synonyms that fracture the mental model.
- The bridge is treated as hidden implementation detail rather than user-facing material.
- Marketing, examples, or release notes imply unsupported features.

## Evidence Log

Method: read the three product docs in full (`docs/product/{PRD,glossary,ui-principles}.md`);
inventoried actual user-facing strings in the app (`app/src/ui/*`, `app/src/platform/menu_bar.mm`);
drove the running release build (commit `45ba98a9`, post-#204) via the control server to capture
full-window screenshots of the first-run and primary-navigation states; and read the release-facing
copy (`site/content/*.md`, `examples/tutorials/*/README.md`, `docs/release/README.md`). Screenshots
under `evidence/phase-01/`. Paths relative to repo root.

### A. Concept inventory & three-way map

Each primary concept mapped to its canonical definition, the app surface + visible label, and
whether the app teaches it through behavior. "In model?" distinguishes a shipped concept from a
doc-only one.

| Concept | Canonical (source) | App surface & visible label | Taught by behavior? | Status |
|---|---|---|---|---|
| Session | Top-level authoring/perf state (glossary) | Left "SESSION" zone header (amber); title "Vivid — <name>" | Yes — it's the home surface | **shipped** |
| Transport | Master musical clock: BPM/meter/beat/bar/quant (PRD §2) | Transport bar: play/stop, "124 BPM", "4 / 4", "Q 1/4 bar" pill | Yes | **shipped** |
| Track | A responsibility/role in the piece (PRD Tracks; glossary) | Session grid **columns**; "+ Track"; per-track mixer w/ "ARM"/"VIZ" | Yes | **shipped** (see F4 re: *kinds*) |
| Scene | Named audiovisual section launching clip assignments (PRD §Scenes) | Session grid **rows**; default names "A/B/C", demo names "intro/verse/chorus/…"; "+ Scene"; scene-launch quant | Yes | **shipped** |
| Clip | Behavior capsule launched from a track in a scene (glossary) | Grid **cells** w/ mini piano-roll thumbnails ("Clip A"); sidebar "drag a clip here to stash it" | Yes | **shipped** |
| Audio graph | Per-track plugin/routing deep view (ui-principles §1) | Bottom-left panel (amber), "MIDI In → Notes → <inst>"; "click a node to edit its parameters" | Yes — contextual deep view | **shipped** |
| Visual graph | Primary visual authoring surface (PRD §Visual Graph) | Main canvas (cyan): operator nodes + texture edges → Output; live node thumbnails | Yes — graph-is-home | **shipped** |
| Operator | Visual/audio node; minimal seed set (PRD §5) | Nodes "Instancer/Emitter/Composite/Notes/Output"; add-node chooser ("no match" placeholder) | Yes | **shipped** (never labelled "operator" in-canvas) |
| Output / Preview | Floatable rendered surface; identity = Output-node params (ui-principles §5, ADR-0014) | "Output → viewer" node + floating "OUTPUT · 1280×720 / Out" preview window | Yes | **shipped** |
| Mapping | One bridge wire src→dst + shaping (glossary) | "MAPPINGS (n)" modal (teal): "SOURCE → DEST", POL/AMT/CURVE/LO/HI; "No mappings yet — …(m)" | Partly (modal behind `m`) | **shipped** |
| Bridge | First-class relationship *layer* audio↔visual (glossary; ui-principles) | Delivered **as** "Mappings"; the word "Bridge" appears in **no** UI string | — | **shipped object, doc-only name** (F2) |
| Package | dlopen'd operator dylib; project-local code → package (glossary Project-Local Code) | Content-browser sidebar (☰ toggle); MCP `build_operator_package`; not a prominent GUI concept | Weakly | **shipped** (developer-facing) |
| Project | Portable, text-backed, reloadable folder (PRD §7) | File menu New/Open/Save/Open Example/Open Recent; folder projects under `examples/**/project.json` | Yes | **shipped** |
| Variation / Take / Live Take / Variation Well / Cue Path | The experimentation loop: audition, keep, branch, compare (glossary; PRD §6; ui-principles) | **None.** 0 UI strings, 0 model types (grep across `app/src`). Nearest affordance: sidebar clip "stash" | No | **doc-only — not shipped** (F1) |

### B. Product-promise / release-copy claim matrix

Claims from the release-facing copy, marked shipped / scaffolded / hidden / unsupported against the
actual candidate.

| Claim | Source | Release status | Evidence / phase |
|---|---|---|---|
| "DAW-style Session View and a rewireable visual node graph live in one project" | `site/content/home.md` | **shipped** | screenshots 01–02 |
| "everything is controllable over MCP … author, inspect and verify whole pieces in code" | home.md | **shipped** | ~120 MCP tools (`mcp/vivid_mcp.py`); Phase 5 to qualify polish |
| "Sound drives picture, picture drives sound" (bidirectional) | home.md | **shipped** | `node_graph.cpp:419` publishes `viz.*`; `frame.cpp:397-403` applies to plugin/native params |
| "yours to fork" | home.md | **shipped** | folder project = copyable; text source of truth (PRD §7) |
| Signed + notarized DMG, "opens without a Gatekeeper detour" | `site/content/start-here.md` | **shipped/exercised** | `docs/release/README.md`; `REQUIRE_NOTARIZE` guard (#194) |
| Surge XT auto-found on install | start-here.md | **shipped** | plugin scan; tutorial preflight `check_tutorial_prereqs` |
| "Open Vivid and work through the tutorial … in about ten minutes" | start-here.md §3 | **scaffolded/mismatch** | the tutorial runs via `uv run build.py` (MCP/CLI), not an in-app guided flow (F3) |
| "a project-local shader wired into the visual graph" + "audio-to-visual mappings" | start-here.md; tutorial README | **shipped** | `mcp-native-first-project` builds `PulseField → Blur → Output` + 4 mappings |
| Auto-update / Sparkle | (not promised) | **deferred — disclosed** | `docs/release/README.md` labels the stub honestly (good) |
| Experimentation loop: audition / keep / branch / compare (takes, variations) | PRD §6; glossary | **unsupported** | no UI/model (F1) — must be labelled "coming soon" |
| Five track kinds: instrument/audio/visual/mapping/hybrid | PRD §Tracks; glossary Track | **partially unsupported** | app has audio/instrument tracks + per-track VIZ link; visual/mapping/hybrid are separate surfaces, not track kinds (F4) |

### C. Vocabulary mismatch list

- **Bridge ≟ Mappings** — docs name a first-class "Bridge" layer; UI only ever says "MAPPINGS". Object
  delivered, name not. (→ F2, P3)
- **Take / Live Take / Variation / Variation Well / Cue Path** — core glossary terms with no UI or
  model referent. (→ F1, P2)
- **Track "kinds"** — glossary/PRD enumerate five role kinds; the app exposes one track flavor
  (audio/instrument) + a VIZ toggle. (→ F4, P3)
- **"Operator"** — the PRD's headline unit is never shown as the word "operator" on the canvas (nodes
  are named by type). Minor; no finding — the concept is taught by behavior.

### D. First-run & primary-navigation evidence

- `evidence/phase-01/01-first-run-empty.png` — **first-run empty state** ("Vivid — Untitled"): amber
  SESSION sidebar (scene rows A/B/C, "+ Track", "MIX/MAIN/VIZ"), cyan visual graph containing an
  "Output → viewer" node fed by nothing, a stray unwired "Output · Level" data node, and a **black**
  floating preview. No welcome, tour, or next-action guidance. The ☰ button top-left is a
  content-browser toggle (`session_view.cpp:408`), **not** an app/help menu. (→ F3)
- `evidence/phase-01/02-example-loaded-neon.png` — **populated product shape** ("neon" demo): tracks
  as columns (Cassette Drums / arp / bass), named scenes as rows (intro/verse/chorus/bridge/outro),
  clip thumbnails, per-track audio graph deep-view (bottom-left, amber), a full visual graph with
  live per-node thumbnails (cyan), and a live floating Output. Domain zones read clearly.
- `evidence/phase-01/03-mapping-bridge-overview.png` — **the bridge** (press `m`): "MAPPINGS (1)"
  (teal), `transport.beat_pul → node 4 · size (visual)` with POL/AMT/CURVE/LO/HI shaping. The bridge
  is a first-class visible editable object — surfaced as "Mappings". (→ F2)

The macOS menu bar (`app/src/platform/menu_bar.mm`) is **File / Edit / Eval** only — no View, Window,
or Help; "Eval" holds "Set Gemini Key…" and "Evaluate Output". (→ F5)

### E. Findings

#### F1 (P2): The experimentation-loop vocabulary is a glossary promise the product does not implement

- Surface: `docs/product/glossary.md` (Take, Live Take, Variation, Variation Well, Cue Path) vs. the app.
- Impact: PRD §6 ("try, perceive, branch, compare, refine") and ui-principles ("Experimentation-first:
  cheap to audition, inspect, keep, branch, compare") are anchored on a term-family the glossary
  defines as core product vocabulary — but there is **no UI and no data model** for any of it. A user
  reading the glossary, or an agent told to "branch this take into the variation well," will find no
  corresponding object. This is the phase's primary failure mode ("docs describe a product the UI
  doesn't expose") and its top open question ("what should be labelled coming soon").
- Evidence: `grep` across `app/src` for `Take/Variation/VariationWell/CuePath/LiveTake` → 0 UI strings
  and 0 model types (the only "Take" hits are incidental — a template param, "Take the snapshot"
  comments). Nearest shipped affordance is the sidebar clip **stash** (`session_view.cpp:176`), which
  is a holding area, not an audition/keep/branch/compare well.
- Smallest acceptable fix: reconcile the *docs*, not build the feature — mark these glossary terms
  (and PRD §6's loop) as **planned / not in first release** so the vocabulary contract stops promising
  unshipped objects. Building the Variation Well is a post-1.0 feature, not a release fix. Owner/status:
  Unassigned | P2.

#### F3 (P2): No in-app first-run guidance; the designated "front door" is an MCP/CLI builder script

- Surface: first-run app state; `site/content/start-here.md`; `examples/tutorials/mcp-native-first-project`.
- Impact: launching the release build cold drops the user into an empty session with an unwired Output
  node and a black preview (evidence 01), with only terse structural hints and no welcome / "start
  here" / example prompt. The one guided path is run via `uv run build.py` against the control server —
  an agent/CLI flow — yet start-here.md §3 says "**Open Vivid and work through** the tutorial … in
  about ten minutes," implying an in-app guided experience that does not exist. A GUI-first newcomer
  has no discoverable next action inside the app.
- Evidence: evidence/phase-01/01-first-run-empty.png (bare empty state; ☰ is a browser toggle, not a
  menu — `session_view.cpp:408`); tutorial README (`build.py` + control server on `127.0.0.1:9876`);
  start-here.md §3 wording. Menu bar has no Help (`menu_bar.mm`).
- Smallest acceptable fix: either (a) add a minimal in-app first-run pointer — empty-state text that
  says "Open an example (File ▸ Open Example) or see the Start Here guide" — or (b) reword start-here.md
  §3 to state the tutorial is agent/CLI-driven (`uv run …`), not an in-app walkthrough. Feed the
  first-run-readiness call to Phase 6. Owner/status: Unassigned | P2.

#### F2 (P3): The docs' first-class "Bridge" is only ever surfaced as "Mappings"

- Surface: `docs/product/{PRD,glossary,ui-principles}.md` ("the bridge is a first-class, visible
  object") vs. the app's "MAPPINGS" UI and `map_audio_to_visual_param` MCP naming.
- Impact: synonym drift — a reader looking for "the bridge" finds "mappings". The *object* is delivered
  and visible (good), only the *name* differs, so the mental-model fracture is mild.
- Evidence: evidence/phase-01/03-mapping-bridge-overview.png ("MAPPINGS", teal); `grep "\"Bridge"` over
  `app/src/ui` → 0 hits; glossary has both "Bridge" and "Mapping" entries.
- Fix: pick one user-facing name — either introduce "Bridge" in the UI/label, or demote "Bridge" in the
  docs to an internal/architectural term and standardise on "Mappings" for users. Owner/status:
  Unassigned | P3.

#### F4 (P3): The PRD/glossary "five track kinds" are not modelled as track kinds

- Surface: PRD §Tracks + glossary Track ("instrument, audio lane, visual layer, mapping lane, or hybrid
  behavior lane") vs. the app's track model.
- Impact: the app models tracks as audio/instrument lanes (device dock + clips + a per-track VIZ link);
  visual authoring lives on the visual graph and mapping in the Mappings overview — there is no
  "visual track" / "mapping track" / "hybrid track" *kind*. An agent or reader expecting to "create a
  visual track" won't find that kind. This reads as an intentional evolution (parity-not-symmetry:
  visuals own the graph), so it's a doc-accuracy gap, not a missing feature.
- Evidence: `bridge_source.h:19-23` `kTrackKindLabels` are per-track *signal* kinds (Level/Transient/…),
  not role kinds; no `TrackKind` role enum in the model; evidence 02 shows one track flavor + VIZ.
- Fix: update PRD/glossary Track to describe the shipped model (audio/instrument tracks + a visual
  link; visuals & mappings as their own surfaces), or explicitly mark the extra kinds as planned.
  Owner/status: Unassigned | P3.

#### F5 (P3): "Eval" (Gemini) is exposed as a top-level menu / key-gated capability on an otherwise thin menu bar

- Surface: `app/src/platform/menu_bar.mm` — top-level "Eval" → "Set Gemini Key…", "Evaluate Output".
- Impact: an experimental, externally-key-gated AI-evaluation capability is presented as a first-class
  top-level product affordance, on a menu bar that otherwise offers no Help/View/Window and no
  explanation of what "Eval" is or why it wants a Gemini key. For a first-release product-surface, this
  over-promotes an internal/experimental tool and can confuse the day-one promise (is AI evaluation a
  headline feature?).
- Evidence: `menu_bar.mm` menu labels (File/Edit/Eval); no in-app copy explaining Eval or the key.
- Fix: decide the first-release status of Eval — demote it (behind a settings/dev toggle) or add a
  one-line explanation + "coming soon"/"experimental" label; reconcile with Phase 5's public/private
  MCP-surface call. Owner/status: Unassigned | P3.

### F. Draft first-release promise (≤1 paragraph)

> **Vivid 1.0** is a macOS audiovisual environment where a DAW-style Session View (tracks, scenes,
> clips, one master transport) and a rewireable visual node graph live in one portable, text-backed
> project and meet through first-class, editable audio↔visual **mappings** that run in both
> directions. Sound is **plugin-first** (VST3 / CLAP / AU), visuals are **code-first** (project-local
> shaders and operators, hot-reloaded), and every object — session, graph, mapping, plugin, output —
> is inspectable and authorable over **MCP**, so a person or an agent can build, save, reload, and
> verify a whole piece. First release delivers the two-surfaces-one-transport core, the bidirectional
> mapping bridge, plugin hosting, project-local shader/operator authoring, and the MCP-native creative
> loop; the experimentation-well vocabulary (takes, variations, cue paths) and in-app auto-update are
> on the roadmap, not in 1.0.

## Open Questions

*(answered)*

- **Which examples define the first-release promise vs. demonstrate internal experiments?** The
  **tutorials** — chiefly `mcp-native-first-project` (ADR-0040 "Golden Path A") plus `live-shader-edit`
  and `project-cpp-operator` — and the **five showcase projects** (`examples/demos/showcase/*`) are the
  first-release promise set (each is a saved, regenerable project doubling as a regression case). The
  ~20 `examples/demos/projects/*` are richer internal experiments; they should not be presented as the
  canonical new-user path.
- **Which agent/MCP capabilities are public product surface on day one?** The MCP-native creative loop
  per ADR-0040 — inspect/explain, author graph + project-local code, connect mappings, transport, save,
  reload, and verify (capture/analyze/quality-checks). The Gemini **Eval** path is experimental and
  externally-key-gated and should be labelled as such, not presented as a day-one headline (F5).
- **What claims should be explicitly labelled "coming soon"?** The experimentation-loop vocabulary
  (Take / Live Take / Variation / Variation Well / Cue Path — F1); the extra **track kinds** (visual /
  mapping / hybrid — F4); and in-app **auto-update** (already disclosed as deferred in the release
  runbook — keep it that way).

## Follow-Up Plans

- **Doc/label reconciliations (own PRs, docs-only):** glossary/PRD "coming soon" annotations for the
  experimentation-loop terms (F1) and the extra track kinds (F4); pick a single user-facing name for
  the bridge/mappings and align docs + UI (F2).
- **First-run copy/onboarding (F3):** either a minimal in-app empty-state pointer or a start-here.md
  rewording; hand the first-run-readiness verdict to **Phase 6**.
- **Eval surface decision (F5):** reconcile with **Phase 5** (public vs. experimental MCP/product
  surface) before deciding the menu's first-release status.
- **Cross-refs:** F1/F4 give Phase 2 (core workflows) and Phase 6 (first-run/examples) their
  product-vocabulary baseline; F3 is the Phase 6 first-run dependency; F5 is a Phase 5 input. The
  drafted first-release promise (§F) is the baseline Phases 2 and 6 should approve or challenge.
- Durable outcome: if the "coming soon" set (F1/F4) and the bridge-naming choice (F2) are ratified,
  they should land as a **glossary/PRD revision** (the vocabulary contract), the doc-level analogue of
  the code audit graduating durable constraints to ADRs.
