# Phase 6: First-Run, Examples, And Release Packaging

Status: done (audited 2026-08-01)

## Verdict

**FAIL — 1×P2 (release-facing), no P0/P1.** The packaging *mechanics* and *honesty* are in good
shape: the release pipeline produces a **signed + notarized** DMG (exercised end-to-end, ADR-0040),
and the one unfinished piece — the Sparkle **auto-updater** — is a no-op stub that the runbook
**discloses honestly** (`docs/release/README.md`). The clean example subset works: neon, blob, pulse,
crystal, spectrum, constellation, prism all load and render non-blank. The blocker is the **bundled
example set**: the build copies *all* of `examples/demos/projects/` into `Resources/examples`
(`app/CMakeLists.txt:366-387`) and surfaces them in **File ▸ Open Example**, but **6 of the 18 demos
reference absolute developer/personal paths** that don't exist on any other machine and aren't
bundled — including a `.claude/worktrees/…` path and the developer's **personal Ableton sample
library** (`/Users/jeff/Music/Ableton/…`, not redistributable). A first user who opens one of those
gets broken media. That fails the acceptance criterion "bundled examples and assets load without
absolute developer paths" and the failure mode "examples depend on files outside the bundle."

Findings: **1×P2 + 1×P3.** The P2 is the broken examples (F1). The P3 is that the demos depend on
plugins beyond the one `start-here` installs, with no in-app cue when a demo opens without them (F2).
First-run *readiness* also inherits the still-open onboarding gaps from earlier phases (Ph1 F3 no
in-app onboarding + CLI "front door"; Ph4 F1/F3 photosensitivity note + shortcut cheat-sheet) — all
tracked, none new here.

> Scope note: a true clean-machine install of the notarized **DMG** could not be run in this
> environment. This phase audited the packaging *inputs* — the bundle contents, path references,
> plugin dependencies, and release runbook — which is where the F1 defect lives. A final
> install-the-DMG-on-a-fresh-Mac smoke test is the one residual check that needs a real release
> artifact (Open Question 1).

## Purpose

Verify the release candidate as a user's first contact with Vivid: install, launch, load examples,
learn the shape, and share or preserve work.

## User Task

Install or launch the release build, open bundled examples, create a small edit, and confirm that
the app's release-facing docs explain the path accurately.

## Hypothesis

If first-run packaging is ready, users can experience the product promise without development
tools, local build knowledge, or missing assets.

## Pressure Test

Exercise a clean-machine style run using release build artifacts, bundled assets, examples, docs,
and website/release-note paths.

## Scope

- Release artifact launch, first-run app state, bundled assets, bundled examples, website/download
  claims, release notes, basic install expectations, and update/signing disclaimers.
- Paths and assumptions that differ between developer builds and release builds.
- The user's first 15 minutes with the product.

Out of scope: full notarization/signing implementation if explicitly labeled scaffolded in the
release runbook.

## Audit Procedure

1. Start from a clean or clean-ish environment: no developer-only absolute paths, no preloaded
   project state, and no hidden local assets.
2. Launch the release candidate and record first-run state, visible next action, and any warnings.
3. Open each release-candidate example and check missing assets, plugin dependencies, playback,
   visuals, mappings, save-copy behavior, and close/reopen.
4. Compare release notes, website copy, and in-app examples with what actually works.
5. Verify that known infrastructure gaps are described honestly and do not appear as broken user
   promises.

## Evidence To Collect

- First-run screenshot and notes.
- Example inventory: name, purpose, required assets/plugins, pass/fail, and release suitability.
- List of absolute paths, missing bundle files, or developer-only assumptions.
- Copy mismatch list for website, release notes, docs, and in-app behavior.

## Deliverables

- First-run readiness verdict.
- Release example matrix.
- Packaging and copy findings with release actions.

## Acceptance Criteria

- The app launches cleanly from the release artifact.
- Bundled examples and assets load without absolute developer paths.
- First-run state has a clear next action.
- Release notes, website claims, and in-app behavior agree.
- Known scaffolded release pieces are labeled honestly.

## Failure Modes

- Examples depend on files outside the bundle.
- First launch opens to an empty or confusing state.
- Release documentation assumes developer tooling.
- Signing, update, or packaging gaps are hidden rather than documented.

## Evidence Log

Method: scanned every bundled example's `project.json` for absolute/dev paths; read the bundling
target (`app/CMakeLists.txt:366-387`); load-tested demos on the running build (commit `1d94ec01`,
post-#212) and checked render + `missing_media`; and compared `site/content/*`, tutorial READMEs, and
`docs/release/README.md` with actual behavior. Paths relative to repo root. First-run screenshot:
Phase-1 `evidence/phase-01/01-first-run-empty.png`.

### A. First-run readiness

Launch opens to the empty session + a visual graph with an unwired Output and a black preview
(Phase-1 evidence 01). There is **no in-app onboarding or next-action** (Phase-1 F3), and the
designated "front door" (`mcp-native-first-project`) is a `uv run build.py` MCP/CLI script, not an
in-app flow (Phase-1 F3 / Phase-2 F3). These are already-filed opens; this phase confirms the
first-15-minutes path still depends on external docs + a curated example. **Needs the onboarding
follow-ups to clear** (Ph1 F3, Ph4 F3), but the empty state itself is clean and non-broken.

### B. Release example matrix (bundled `Resources/examples` = all of `examples/demos/projects/`)

| Example | Renders | Plugin deps | Absolute/dev paths | Release-suitable? |
|---|---|---|---|---|
| neon (canonical) | ✅ nonblank | Surge XT + Cassette Drums | none | **yes** |
| blob, pulse, crystal, spectrum, constellation, prism | ✅ nonblank | Surge XT (+ Cassette Drums most) | none | **yes** |
| generative-fields, geometry, lattice, storm, surge-lead | (clean paths) | Surge XT (+ Cassette Drums) | none | **yes** (spot-clean) |
| **bloom** | — | Surge XT + Cassette Drums | `/Users/jeff/…/.claude/worktrees/…/bloom.txt` | **NO — worktree path** (F1) |
| **chop** | — | Surge XT | `/Users/jeff/Developer/vivid/…/break90.wav` | **NO — dev-repo path** (F1) |
| **signal** | — | Surge XT + Cassette Drums | `/Users/jeff/…/frank/scene.gltf`, `…/loop.mp4` | **NO — dev-repo paths** (F1) |
| **mirror** | — | Surge XT + Cassette Drums | `/Users/jeff/…/mirror.txt` | **NO — dev-repo path** (F1) |
| **drift** | — | Surge XT | `/Users/jeff/Music/Ableton/User Library/Samples/Dan Mayo/…` ×3 | **NO — personal, non-redistributable samples** (F1) |
| **grid** | — | Surge XT | `/Users/jeff/Music/Ableton/User Library/Samples/Dan Mayo/…` ×3 | **NO — personal, non-redistributable samples** (F1) |

The 12 clean demos load and render (six spot-verified `nonblank=pass`). The tutorials
(`examples/tutorials/*`) and showcase (`examples/demos/showcase/*`) are **not** bundled into the app
(only `demos/projects/` is), so File ▸ Open Example lists exactly these 18 demos.

### C. Absolute-path / dev-assumption inventory (the packaging defect)

- `examples/demos/media/` (where chop/bloom/mirror/signal point) is a **sibling of** `projects/` and
  is **not** copied into the bundle — so even the "repo media" examples break in the shipped app, and
  the absolute `/Users/jeff/…` prefix breaks them on any other machine regardless.
- `missing_media` reads **0 on the developer's machine** because those exact paths resolve *here* —
  a false-clean signal that masks the defect. On a clean machine every one of the 6 would be missing.
- **drift/grid** reference the developer's personal Ableton sample library — files that are neither in
  the repo nor redistributable, so they can never ship.
- → **F1.**

### D. Copy vs. behavior + packaging honesty

- **Packaging honesty: PASS.** `docs/release/README.md` states "exercised end-to-end; auto-update
  deferred" — the notarized DMG is real (Gatekeeper-clean), the Sparkle auto-updater is a disclosed
  no-op stub. Known gaps are labeled, not hidden (acceptance criterion met).
- **Plugin copy: mostly honest.** `start-here.md` scopes to the tutorial ("exactly one free plugin:
  Surge XT"); `site/content/free-plugins.md` documents the fuller set (Surge XT, Surge XT Effects,
  **BPB Cassette Drums** with install paths); `free-plugin-starter-list.md` says "Surge XT is
  required; everything else is optional … for demos." So the docs cover it — but a user who follows
  `start-here` (Surge only) and then opens a demo needing Cassette Drums gets a silent drums track
  with no in-app cue (→ F2).
- **`start-here` §3 wording** ("Open Vivid and work through the tutorial") still implies an in-app
  flow for what is a `uv run build.py` script — the Phase-2 F3 copy item (unchanged here).

### E. Findings

#### F1 (P2): 6 of 18 bundled demos reference absolute developer/personal paths and break on a clean machine

- Surface: `examples/demos/projects/{bloom,chop,drift,grid,mirror,signal}/project.json`, bundled by
  `app/CMakeLists.txt:366-387` and shown in File ▸ Open Example.
- Impact: a first user who opens one of these six examples gets broken media — the paths point at the
  developer's repo, a stale git worktree, or a **personal (non-redistributable) Ableton sample
  library**, none of which exist on their machine, and the media directory isn't bundled anyway. A
  third of the "first contact" examples fail. drift/grid additionally can never ship (licensed
  personal samples).
- Evidence: §B/§C; `grep '"/Users/'` over the six `project.json`s; `CMakeLists.txt` copies only
  `projects/` (not `media/`); `missing_media=0` here is a dev-machine false-negative.
- Smallest acceptable fix: **curate the bundled/Open-Example set for release** — exclude the six
  broken demos (or repair them: rewrite their media refs to bundle-relative paths, bundle the media,
  and replace the personal Ableton samples with royalty-free loops). The clean 12 + neon are already
  release-suitable. Owner/status: Unassigned | P2.

#### F2 (P3): Demos depend on plugins beyond the documented "one free plugin", with no in-app cue

- Surface: the demo `project.json`s (all need Surge XT; ~8 also need BPB Cassette Drums) vs
  `start-here.md` ("exactly one free plugin: Surge XT") and the in-app missing-instrument behavior.
- Impact: `free-plugins.md` documents Cassette Drums, so the docs are honest — but a user who follows
  the quick start (Surge only) and opens a demo needing Cassette Drums gets a **silently degraded**
  drums track (placeholder, stderr-only), with no in-app "this example needs BPB Cassette Drums" cue.
- Evidence: §D; plugin-dep scan of the demo `project.json`s.
- Smallest acceptable fix: surface a per-example missing-plugin note when a demo loads with an
  unresolved instrument (ties to Phase-4 error-surfacing), and/or list each bundled demo's plugin
  deps. Owner/status: Unassigned | P3.

## Open Questions

*(answered)*

- **Which exact artifact is the release candidate for this audit?** None was available to install in
  this environment; the phase audited the packaging *inputs* the release pipeline consumes
  (`release-macos.yml` builds a notarized DMG on a version tag; the app self-reports `v0.1.0`). The
  one residual check is an install-the-DMG-on-a-fresh-Mac smoke test with a real artifact.
- **Which examples are bundled, documented, hidden, or removed for first release?** Today **all 18**
  `demos/projects/` demos are bundled (File ▸ Open Example); tutorials + showcase are docs/website
  only. Recommendation: bundle the **clean 12** (neon canonical) and **remove or repair the 6** in F1
  for the release.
- **Are auto-update and notarization part of the public promise for this release?** **Notarization:
  yes** — the DMG is signed + notarized and opens Gatekeeper-clean (day-one promise). **Auto-update:
  no** — the Sparkle bridge is a disclosed no-op stub, deferred post-release; the runbook says so, so
  it is not a broken promise.

## Follow-Up Plans

- **F1** is the one release-gating item in this phase — curate the bundled example set (or repair the
  six) before the release candidate ships; small, self-contained, no code beyond the bundle/curation.
- **F2** pairs with the Phase-4 error-surfacing work (a missing-plugin cue on example load).
- **First-run readiness** depends on the accumulated onboarding follow-ups — **Ph1 F3** (in-app
  onboarding / reword the CLI "front door"), **Ph4 F1** (photosensitivity note), **Ph4 F3** (shortcut
  cheat-sheet), and **Ph5 F1** (label music-eval experimental) — which together form the natural
  "first-run polish" bundle a release checklist should track.
- **Cross-refs:** packaging honesty + notarization rest on the code audit's Phase-6 (ADR-0040 gate,
  #194 REQUIRE_NOTARIZE). This closes the UX audit's six-phase program; the release-gating open item
  is F1 (curate examples), with the remaining opens being P3 first-run polish.
