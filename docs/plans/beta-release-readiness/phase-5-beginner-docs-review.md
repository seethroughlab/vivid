# Phase 5: Beginner Docs Review

## Goal

Evaluate and revise beginner-facing documentation from the point of view of a synth-savvy non-programmer beginner.

## Inputs

- `README.md`
- `docs/GETTING-STARTED.md`
- `graphs/README.md`
- `graphs/intro/README.md`
- Package install docs used by the beta path
- Generated site pages that beta testers will see
- Findings from graph and inspector reviews

## Steps

1. Identify the exact docs a beta tester will be asked to read.
2. Separate user-facing docs from developer/internal docs so the beta path does not accidentally send beginners into architecture material.
3. Add or revise macOS first-run setup with Homebrew/CMake where needed.
4. Make it explicit that the beta path does not require programming knowledge.
5. Rewrite developer-first wording into task-first wording:
   - Open the app
   - Open examples
   - Hear/see what changed
   - Tweak these controls
   - Save a variation
   - Recover if something goes wrong
6. Create or revise a short "First 15 Minutes" path with a curated beginner example sequence.
7. Add fallback guidance for missing audio output, camera permission, microphone permission, MIDI devices, movie assets, or package-required examples.
8. Ensure docs do not name private reference people or mention internal planning typos.
9. Align docs with graph metadata and example browser labels.

## Pass/Fail Criteria

Pass when a synth-savvy non-programmer beginner can follow the first-run path without needing to understand C++, CMake internals, operator implementation, or architecture docs.

Fail if the docs rely on unexplained developer tools, bury the first successful audio/video moment, omit Homebrew/CMake setup where needed, send users to irrelevant internal docs, or leave obvious recovery paths undocumented.

## Evidence to Record

- Docs reviewed
- Changes made
- First-run commands tested
- Any screenshots or captures used in docs
- Remaining limitations and where they are documented
- Search result proving no private reference names leaked into beta docs

## Exit Criteria

Phase 5 exits when the beta reading path is clear, short, and current with the app behavior validated in Phases 2-4.
