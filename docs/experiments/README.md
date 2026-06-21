# Vivid 4 — Session View experiments

Status: active. Convergence recorded 2026-06-21.

These are disposable HTML prototypes for the Phase 1 proof (see
[`../roadmap/phased-development-plan.md`](../roadmap/phased-development-plan.md) and
[ADR-0005](../decisions/ADR-0005-proof-gated-development.md)). They exist to prove the Session
View interaction model before any native work — not to ship.

All prototypes render the **same canonical one-song session** (124 BPM, D minor; tracks Drums /
Bass / Chords / Lead / Particles / Camera / AV Mapping; scenes Intro / Verse / Chorus / Drop) so
they can be compared apples-to-apples.

## The spine

**[`session-view-variation-well.html`](session-view-variation-well.html) — the chosen Session
View base.** Keeps the grid matrix (a role's row across scenes is a built-in A/B/C/D comparison),
makes each cell a **variation well** (audition / keep / branch — the experimentation loop in the
cell), wears the Vivid Classic "Dark Steel" skin, and shows the agent-provider seam
([ADR-0008](../decisions/ADR-0008-agent-capability-surface.md)). New features fold into this file.

## Explored alternatives (kept for reference, not the base)

| File | What it is | Why it's not the spine |
|---|---|---|
| [`session-view-pressure-test.html`](session-view-pressure-test.html) | Original peer-row grid | Baseline / control. Audio-shaped; flattens the audio→binding→visual relationship into peer rows. |
| [`session-view-scene-cards.html`](session-view-scene-cards.html) | Scene-as-AV-state cards (live look + binding wires per card) | Wins on perception + relationship, but throws away the cross-scene comparison matrix. **Source for the perception layer** to fold into the spine at Phase 5. |
| [`full-interface-prototype.html`](full-interface-prototype.html) | Full app shell (Stage / Graph / Network / Code) | Parked — designs too much future shell. See [`session-view-interface-reset.md`](session-view-interface-reset.md). |

## Supporting files

- [`vivid.css`](vivid.css) — shared skin tokens. (The Classic "Dark Steel" skin currently lives
  inline in the spine; promoting it here is pending.)
- [`session-view-pressure-test.md`](session-view-pressure-test.md) — Phase 1 task plan + MCP sketch.
- [`session-view-interface-reset.md`](session-view-interface-reset.md) — Phase 1 scope guidance.
- [`mcp-surface.md`](mcp-surface.md) — agent/MCP surface design input.

## Verdict + open gaps

The Phase 1 convergence verdict, per-representation comparison, and the known gaps (perception +
binding legibility = Phase 5; variation well = Phase 4) are recorded in the roadmap's
[Phase 1 convergence verdict](../roadmap/phased-development-plan.md).

## Running locally

A static server config lives in `.claude/launch.json`; serve `docs/experiments/` and open any
file, e.g. `session-view-variation-well.html`.
