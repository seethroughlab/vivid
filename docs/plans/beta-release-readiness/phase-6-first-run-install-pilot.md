# Phase 6: First-Run, Install, and Pilot

## Goal

Validate the complete friend-beta experience: setup, launch, permissions, first example, beginner experimentation, pilot feedback, and final go/no-go.

## Inputs

- Green or triaged Phase 2 automated baseline
- Completed graph review from Phase 3
- Completed inspector review from Phase 4
- Beginner docs from Phase 5
- Clean-ish macOS account or machine
- Beta feedback form or issue intake path

## Steps

1. Start from a clean-ish macOS account or machine.
2. Follow only the beta-facing docs, including Homebrew/CMake setup where needed.
3. Launch Vivid through the intended app/user-facing path.
4. Open the first recommended example and confirm both audio and video work.
5. Walk through the beginner example sequence from the docs.
6. Validate permission and device paths:
   - Audio output
   - Microphone, if used
   - Camera, if used
   - MIDI/IAC, if used
   - Syphon, if used
   - File/media access for movie graphs
7. Validate friendly failure behavior when devices or permissions are missing.
8. Confirm example browser metadata supports the beta path:
   - Beginner examples are findable
   - Package-required graphs are labeled
   - Environment-dependent graphs are not mistaken for first-run starters
9. Run one internal full pass and fix all blockers.
10. Send a tiny pilot to one trusted tester only after the high-confidence gate passes internally.
11. Collect pilot feedback on:
    - Install confusion
    - Audio device confusion
    - Example browser confusion
    - Unclear inspector labels
    - Panic-inducing audio or visual output
12. Fix pilot blockers and update known limitations.
13. Make final go/no-go decision before sending to the wider friends/colleagues group.

## Pass/Fail Criteria

Pass when a beta tester can follow the documented path to a successful audio/video moment, tweak a graph, save a clip or scene, and recover from common missing-device cases without developer help.

Fail on install dead ends, launch failure, permission dead ends, first-example silence/black output, unexplained device problems, scary audio, unreadable beginner docs, or beta-path blockers carried forward from earlier phases.

## Evidence to Record

- Machine and macOS version
- Install path used
- Commands run
- App/build version and commit hash
- First example opened
- Permission prompts observed
- Device setup used
- Pilot tester feedback
- Known limitations note
- Final go/no-go decision

## Exit Criteria

Phase 6 exits when the internal pass and tiny pilot are both complete, all blockers are fixed or removed from the beta surface, and the wider beta package has a concise known limitations note and feedback path.
