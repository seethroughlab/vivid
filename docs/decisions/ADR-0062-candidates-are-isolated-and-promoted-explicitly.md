# ADR-0062: Candidates Are Isolated, Versioned, and Promoted Explicitly

Status: proposed — persistence and concurrency proofs pending

Date: 2026-09-17

Follows: [ADR-0061](ADR-0061-creative-work-persists-across-refinement-rounds.md).
Extends: [ADR-0017](ADR-0017-every-edit-is-reversible.md) and
[ADR-0018](ADR-0018-a-bad-operator-must-not-cost-you-your-work.md).

## Context

Background exploration must not replace the version the creator likes or overwrite manual edits.
Session undo records edits to one document; it does not provide independent candidate workspaces,
durable review evidence, or an explicit preferred version. Copying project.json alone does not
isolate mutable assets, project-local code, or plugin state.

## Decision

1. **Give versions immutable identity.** A version records a project snapshot, parentage, and
   referenced dependencies. A candidate associates a version with its purpose, source brief, and
   evidence. The preferred version is an explicit reference stored in the project document itself,
   so it participates in save/load and session undo. The open editing session may have newer
   unsaved edits and is not implicitly identical to that reference.
2. **Isolate the entire candidate.** Candidate writes cannot mutate the foreground document or
   another version's dependencies. Mutable assets and project-local code require independent
   storage or copy-on-write behavior. Capture available plugin state and dependency identity;
   missing dependencies make reproduction unavailable rather than silently substituting content.
   Reuse canonical persistence instead of inventing a second musical/visual document model.
3. **Separate audition, retention, and promotion.** Previewing does not modify the preferred version.
   Keeping an alternative preserves it without choosing it. Promotion is an explicit creator action
   applying the candidate as one reversible document edit through the edit gateway, which replaces
   the document contents and the preferred-version reference together.
4. **Reject stale promotion.** Atomically validate the expected document revision, preferred-version
   reference, brief, and protection revisions. The document revision is a whole-document counter
   advanced by every edit-gateway command (the existing per-clip `rev` is the precedent), so unsaved
   manual edits participate in that check.
   On conflict, retain the candidate and request reconciliation; do not overwrite or automatically
   merge whole projects in the first version. A replacement review must identify the new baseline.
5. **Keep history and evidence coherent.** Every rendered artifact identifies its input version,
   passage, playback recipe, available seeds, and relevant dependencies. Retain actual media when
   third-party behavior is not exactly reproducible. Later timing edits do not silently retarget
   comments on old previews. Undoing promotion restores the prior document and preferred reference
   in one step because both are document state; work records are append-only and record the
   reversal as an event without deleting feedback or pretending completed runs never happened.
6. **Retain work deliberately.** Preferred versions, explicitly kept alternatives, and evidence for
   unresolved reviews cannot be silently pruned. Temporary failed attempts may be collected under
   an explicit retention policy. Storage format and deduplication are implementation choices.

A candidate may affect one clip, a scene, or a whole project. The glossary's planned Take and
Variation Well are the same audition/keep/choose pattern at cell scope, stored *in* the document; a
candidate is that pattern at version scope, stored in the work records with one preferred-version
pointer in the document. When the well exists, promoting a clip-scoped candidate is the document
edit that adds its take to the cell's well and makes it live, and "keep as an alternative" adds the
take without making it live; until then a clip-scoped candidate promotes by replacing the clip. The
well receives content only — candidate identity, evidence, and feedback remain in the work records,
never duplicated into the well. Combining one candidate's audio with another's visuals creates a new
candidate requiring dependency checks and a new synchronized preview.

## Alternatives Considered

- **Explore in the active document and rely on undo.** Disrupts manual work and mixes two histories.
- **Copy only the session JSON.** Shared mutable files can change supposedly preserved candidates.
- **Use Git alone as the product model.** Useful underlying storage in some designs, but does not
  itself define plugin-state capture, playback evidence, review decisions, or safe promotion.
- **Automatically merge all changes.** Musical and visual dependencies make syntactic success an
  insufficient guarantee of creative compatibility.

## Consequences

Candidate isolation adds disk usage and dependency management. Explicit promotion makes exploration
safe but requires a conflict workflow. Snapshot and asset-manifest details need implementation design;
the first implementation may use complete copies before optimizing storage.

## Acceptance Evidence Required

Change shared shader/asset content in a candidate and prove the preferred version remains unchanged.
Edit the foreground project while rendering and prove stale promotion fails without loss. Promote,
undo, and redo while retaining truthful review history. Reload a saved candidate and identify any
unavailable dependencies. Evidence is pending.
