# Phase 1: Inventory and Gates

## Goal

Create the release-hardening control surface for the beta: an authoritative inventory, clear pass/fail gates, and a repeatable reporting format. This phase prevents later reviews from drifting into informal notes.

## Inputs

- `graphs/**/*.json`
- Runtime/operator registry output, such as `./build/vivid list-types` or MCP `list_types`
- Existing release/testing docs in `docs/testing/`
- Existing beginner-facing docs and graph metadata

## Steps

1. Create a beta readiness checklist with columns:
   - `Area`
   - `Item`
   - `Owner`
   - `Result`
   - `Blocking?`
   - `Evidence`
   - `Follow-up`
2. Generate the sample graph inventory from `graphs/**/*.json`, grouped by folder and graph metadata.
3. Generate the operator inventory from the registered operator surface, not raw filesystem directories.
4. Add environment labels for cases that require camera, mic, MIDI, OSC, Syphon, movie media, package operators, or an external display.
5. Define beta blocker classes:
   - Crash, hang, or launch failure
   - Graph load failure
   - Missing core operator
   - WebGPU validation error
   - Audio device lockup
   - Persistent silence in an intended-audio graph
   - Black or frozen output in an intended-visual graph
   - Scary clipping, runaway feedback, or stuck notes
   - Broken save/load or variation recall
   - Beginner-blocking docs or setup confusion
6. Define non-blocking classes:
   - Cosmetic wording issues
   - Minor layout polish
   - Advanced feature rough edges that are not part of the beta path
   - Environment-dependent skips that are clearly labeled and not in the first-run path

## Pass/Fail Criteria

Pass when every graph and every registered operator has a row in the readiness artifacts, every environment-dependent case is labeled, and blocker rules are written down before review begins.

Fail if the inventory is manually guessed, misses registered operators, omits graph categories, or leaves unclear whether a failure blocks the beta.

## Evidence to Record

- Inventory generation command or script path
- Date, commit hash, and build type
- Graph count
- Registered operator count
- List of environment-dependent cases
- Link to the readiness checklist

## Exit Criteria

Phase 1 exits when the review matrix exists, has complete graph/operator coverage, and is ready for Phase 2 automated evidence and Phase 3-5 human review results.
