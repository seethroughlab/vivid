# Phase 6: First-Run, Examples, And Release Packaging

Status: proposed

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

- Pending.

## Open Questions

- Which exact artifact is the release candidate for this audit?
- Which examples are bundled, documented, hidden, or removed for first release?
- Are auto-update and notarization part of the public promise for this release?

## Follow-Up Plans

- Link release-runbook updates, packaging bugs, and first-run copy changes here.
