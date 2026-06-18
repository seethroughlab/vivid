# Vivid 4 Phased Development Plan

Status: active planning

## Purpose

This document is the high-level development ledger for Vivid 4. It describes the order in which the
product should be proven, without prescribing implementation details.

Each phase should earn the next phase by proving a user-facing capability. Detailed implementation
plans should be written separately for individual phases only after the phase's product task,
hypothesis, pressure test, and exit gate are clear.

## Phase Discipline

Every phase follows the same proof loop:

1. Define the user task and product hypothesis.
2. Sketch, mock, or script the workflow before committing to runtime architecture.
3. Test whether the task can be completed in Vivid 4 terms.
4. Record evidence, failures, redesign notes, and open questions.
5. Write the detailed phase implementation plan only after the product shape survives the proof.

No feature should graduate because it is architecturally interesting. It graduates because a user
task became easier, clearer, or more creatively powerful.

## Progress Ledger

| Phase | Name | Status | Current Evidence |
|-------|------|--------|------------------|
| 0 | Reboot Foundation | in progress | Vivid 4 branch, PRD, retained Classic lessons, commit-history lessons |
| 1 | Disposable Session View Proof | in progress | HTML mock and Session View pressure-test plan |
| 2 | Agent Workflow Proof | not started | High-level MCP tool sketch only |
| 3 | Minimal Native Session Shell | not started | None |
| 4 | Music Authoring Core | not started | None |
| 5 | Visual Bindings Core | not started | None |
| 6 | Project-Local Code Authoring | not started | None |
| 7 | Composition and Performance Flow | not started | None |
| 8 | Usability Alpha | not started | None |

## Phase 0: Reboot Foundation

Status: in progress

Goal: Establish the Vivid 4 product contract.

This phase proves that the reboot has a clear reason to exist, a clean relationship to Vivid
Classic, and a stable set of product principles.

User task:

- Understand what Vivid 4 is, what it is not, and why it should start from a clean branch.

Hypothesis:

- Vivid 4 can reuse the hard-learned lessons of Vivid Classic without inheriting the old interface,
  operator catalog pressure, or graph-first mental model.

Pressure test:

- Explain the reboot without relying on old implementation mechanics.
- Identify which Classic principles are kept, revised, and rejected.
- Confirm that the first proof target follows from the PRD.

Exit gate:

- The PRD, pressure-test plan, prototype, and phased plan agree on the same product direction.
- The current repo is clean enough to serve as the Vivid 4 root.

Progress notes:

- Vivid Classic is preserved on the `vivid-classic` branch and `vivid-classic-final` tag.
- Vivid 4 starts from a clean branch with reboot docs and a disposable Session View prototype.
- The PRD now includes retained lessons from Vivid Classic.
- Additional commit-history lessons are recorded in `docs/research/vivid-classic-lessons.md`.

## Phase 1: Disposable Session View Proof

Status: in progress

Goal: Prove the primary surface before native implementation.

This phase proves whether Session View can represent and explain a one-song audiovisual loop with
tracks, clips, scenes, master transport, visual bindings, selection, and agent actions.

User task:

- Launch and inspect a one-song loop from Session View without opening the graph.

Hypothesis:

- A session grid can make coordinated music, visuals, and mappings understandable at the product
  level.

Pressure test:

- Complete the scripted one-song loop tasks in the clickable HTML mock.
- Confirm that active state, queued launches, selected clips/scenes, and visual bindings are visible.
- Confirm that graph/node vocabulary is not required for the primary path.

Exit gate:

- The mock teaches us what the real interface must support.
- Any confusing feature is redesigned in the mock before native work begins.

Progress notes:

- See `docs/experiments/session-view-pressure-test.md`.
- Current prototype: `docs/experiments/session-view-pressure-test.html`.
- Interface reset guidance: `docs/experiments/session-view-interface-reset.md`.
- Full application shell explorations are parked until the Session View primary path passes.

## Phase 2: Agent Workflow Proof

Status: not started

Goal: Prove that the agent can work at the session level.

This phase proves that the agent can inspect, explain, vary, and modify the session using high-level
concepts instead of raw graph topology.

User task:

- Ask the agent to explain the current scene, generate clip variations, and create or adjust an
  audiovisual binding.

Hypothesis:

- High-level session tools can give the agent enough semantic context to work productively without
  forcing it into implementation details.

Pressure test:

- Use mocked MCP responses to make the agent complete the same tasks as the HTML prototype.
- Check whether the agent can answer in track, clip, scene, binding, transport, and musical-role
  language.
- Redesign the tool sketch if the agent needs graph topology for normal session work.

Exit gate:

- The high-level MCP surface is proven as a product contract before tool implementation begins.
- The agent can complete the one-song loop tasks in session language.

Progress notes:

- The initial MCP tool sketch lives in `docs/experiments/session-view-pressure-test.md`.

## Phase 3: Minimal Native Session Shell

Status: not started

Goal: Prove the smallest real version of Session View.

This phase proves that the native app can load and display a simple session while preserving the
mental model established by the mock.

User task:

- Load a simple session, view tracks/scenes/clips, select objects, inspect state, and launch scenes
  on the master transport.

Hypothesis:

- The product shape proven in HTML can survive contact with native UI and real persisted state.

Pressure test:

- Repeat the Phase 1 primary task in the native shell.
- Verify semantic state first, then visual screenshots second.
- Confirm that the native slice does not expose graph vocabulary as the main path.

Exit gate:

- The native shell can complete the basic Session View task with clear state and selection.
- The implementation plan for deeper runtime work is based on proven product behavior.

Progress notes:

- Not started.

## Phase 4: Music Authoring Core

Status: not started

Goal: Prove plugin-first musical creation.

This phase proves that Vivid can author musical sections using instruments, clips, theory-aware
generators, plugin state, and master-transport scene launches.

User task:

- Create or modify drums, bass, chords, and lead parts for a one-song loop, then ask the agent for
  musically compatible variations.

Hypothesis:

- Vivid can feel like a musical authoring environment without trying to become a traditional DAW or
  native synth workstation.

Pressure test:

- Create variations that preserve track role and scene compatibility.
- Compare alternatives in a loop.
- Confirm that plugin-first authoring feels central rather than bolted on.

Exit gate:

- A user and agent can build or revise a small musical section using session concepts.
- Music authoring works before visual complexity is added.

Progress notes:

- Not started.

## Phase 5: Visual Bindings Core

Status: not started

Goal: Prove audio-visual parity through first-class bindings.

This phase proves that visual reactivity can be visible, editable, and explainable at the Session
View level.

User task:

- Select a visual clip or scene and understand which musical/control signals drive the visual
  behavior, then create or adjust a binding.

Hypothesis:

- Audio-visual parity is best expressed through inspectable relationships, not symmetric audio and
  visual interfaces.

Pressure test:

- Answer "what drives this visual state?" without opening the graph.
- Bind a musical signal to a visual parameter and preview the result.
- Ask why one scene looks more intense than another and receive a session-level explanation.

Exit gate:

- Visual reactivity is understandable as session state.
- Bindings can be authored and inspected before they become graph implementation details.

Progress notes:

- Not started.

## Phase 6: Project-Local Code Authoring

Status: not started

Goal: Prove that Vivid facilitates code authoring without growing a giant core catalog.

This phase proves that the user and agent can create project-local behavior, reload it, inspect it,
and use it in clips or bindings.

User task:

- Add one custom theory generator, visual behavior, or mapping helper as project-local code and use
  it in the one-song loop.

Hypothesis:

- The environment can make custom code feel immediate while keeping core operators minimal.

Pressure test:

- Scaffold or create one focused project-local behavior.
- Use it in a session clip or binding.
- Reload it without losing the creative context.
- Decide whether it remains project-local or deserves later promotion.

Exit gate:

- Custom code authoring feels like part of the creative loop.
- No project-specific behavior is promoted to core without evidence.

Progress notes:

- Not started.

## Phase 7: Composition and Performance Flow

Status: not started

Goal: Prove performance structure beyond a static loop.

This phase proves that Vivid can support scenes, cue paths, manual launch, follow behavior, waiting,
looping, and branching without becoming a linear arrangement timeline.

User task:

- Perform a small audiovisual piece with Intro, Verse, Chorus, Drop, breakdown, and return while
  preserving the session/grid mental model.

Hypothesis:

- Vivid can support performance state and structured progression without abandoning Session View as
  the primary surface.

Pressure test:

- Perform the piece using scene launches and cue/follow behavior.
- Confirm that the master transport governs musical timing.
- Confirm that branching remains clear and does not become a hidden timeline.

Exit gate:

- A short performance can be executed, inspected, and explained from Session View.
- Progression features are proven as performance tools, not arrangement-editor creep.

Progress notes:

- Not started.

## Phase 8: Usability Alpha

Status: not started

Goal: Prove the whole loop repeatedly.

This phase proves that Vivid 4 can support repeated end-to-end authoring sessions with the user and
agent.

User task:

- Start from a blank or template session, build a small audiovisual piece, vary it, explain it, save
  it, reload it, and perform it.

Hypothesis:

- The Vivid 4 product model holds together across repeated real use, not only isolated feature
  demos.

Pressure test:

- Run multiple end-to-end creation tasks.
- Record where the product model breaks, where the agent asks for the wrong abstraction, and where
  the user has to think too hard.
- Fix confusing concepts before expanding breadth.

Exit gate:

- The remaining work is depth, polish, reliability, and expansion rather than uncertainty about what
  Vivid is.

Progress notes:

- Not started.

## Progress Log

Use this section to record dated decisions and evidence as phases advance.

- 2026-06-17: Created Vivid 4 phased development plan as a high-level proof ledger.
- 2026-06-17: Added Vivid Classic commit-history lessons as reboot guardrails.
