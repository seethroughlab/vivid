# Vivid 4 Session View Pressure Test

Status: planned

## Purpose

Pressure-test the Vivid 4 product direction before runtime implementation. The target is not to
ship a new native UI yet; it is to prove that Session View can become the primary authoring surface
for an agent-first audiovisual music environment.

The first proof target is a one-song loop workflow optimized for you plus an agent:

- master musical transport with BPM and time signature
- plugin-first audio authoring
- clips as behavior capsules, not only parameter snapshots
- visual bindings as first-class session objects
- graph/node details available as a deeper implementation layer, not required for the primary path

## Prototype Scenario

Use one canonical session in the first clickable mock:

- **Transport:** 124 BPM, 4/4, bar-aware launch quantization.
- **Tracks:** Drums, Bass, Chords, Lead, Particles, Camera/Palette, AV Mapping.
- **Scenes:** Intro, Verse, Chorus, Drop.
- **Clip kinds:** MIDI, theory pattern, audio/plugin state, visual state, and AV binding.
- **Primary interactions:** launch scenes, select tracks/clips/scenes, inspect visual bindings, ask the agent for variations, and explain why the Drop is more intense.

The initial disposable HTML mock lives at
[`docs/experiments/session-view-pressure-test.html`](session-view-pressure-test.html).

## HTML Prototype Gate

Before implementing native UI or runtime/schema changes, the HTML mock must prove the scripted task
without backend integration, graph topology, or node-level vocabulary.

The mock must show:

- master transport and queued/active scene state
- scene x track grid with typed tracks and clip cells
- selected clip/scene inspector
- visual binding inspector
- mocked agent action panel or transcript for the scripted agent tasks
- enough mocked session state to explain what is active, what will launch, and what drives visuals

The mock may represent agent work with a lightweight panel or transcript, but it should not decide
the final agent integration model. Whether the production agent is embedded, external-only, or a
hybrid is outside the Phase 1 interface proof.

The mock is disposable product evidence, not a commitment to HTML as the production UI technology.

## Phase 1 Scope Reset

The current Phase 1 job is to prove the Session View primary path. It is not to design the full
Vivid application shell.

Keep in scope:

- Session View as the first and dominant surface.
- Master transport, active/queued launch state, tracks, clips, scenes, and selection.
- Visual bindings as first-class, inspectable objects.
- A small live visual preview only when it helps explain bindings or scene intensity.
- Mocked agent actions that complete the scripted one-song loop tasks.

Keep out of scope for this prototype:

- top-level Stage mode
- top-level Graph mode
- scoped operator network editing
- project-local code browser/editor
- final MCP architecture decisions
- final production navigation model

Explorations that include those ideas should be treated as later-phase research, not as the Phase 1
prototype unless the Phase 1 task proof specifically demands them.

## High-Level MCP Tool Sketch

These tools are product-level planning targets only. Do not implement them until the HTML mock proves
that the workflow is useful. They should layer on top of the existing graph/session tools rather than
replace them.

The Phase 1 mock can show agent actions as a lightweight product placeholder. Phase 2 should decide
the real agent workflow surface with mocked MCP responses and separate pressure testing.

| Tool | Purpose |
|------|---------|
| `inspect_session_overview()` | Return tracks, scenes, clips, active/queued state, transport, and available bindings in one agent-readable object. |
| `create_session_track(kind, name, intent)` | Create typed tracks: `instrument`, `audio`, `visual`, `mapping`, or `hybrid`. |
| `create_clip(track_id, kind, name, intent, content)` | Create behavior-capsule clips: MIDI, theory pattern, automation, visual state, or AV binding. |
| `create_scene(name, assignments, intent)` | Create a scene from explicit track-to-clip assignments. |
| `create_av_binding(source, destination, curve, timing, scope)` | Create relationships such as `kick_onset -> bloom`, `bass_env -> particle_size`, or `chord_brightness -> palette`. |
| `generate_clip_variations(track_id, clip_id, count, intent)` | Produce alternatives while preserving track role and scene compatibility. |
| `explain_session_state(selection)` | Explain what is happening in a scene, clip, track, or binding using session language. |

## Usability Gate Sequence

Every feature slice must pass a task proof before moving deeper into implementation.

1. **PRD Assumption Gate**  
   Write the user task, hypothesis, expected evidence, and failure modes.

2. **HTML Prototype Gate**  
   Complete the scripted task in the clickable mock. No graph/node vocabulary may be required for
   the primary path.

3. **Agent Workflow Gate**  
   Using mocked high-level MCP responses, the agent must complete the same task in session terms.
   Pass only if the agent does not need raw graph topology to explain or modify the session.

4. **Native Slice Gate**  
   Implement the smallest native slice after prototype pass. Add semantic UI assertions where
   possible, following the existing `UI_SMOKE` / `GUI_SMOKE` style: state assertions first,
   screenshots second.

5. **Promotion Gate**  
   Promote a concept into runtime/API/schema architecture only after one successful native slice
   proves it. Follow the existing guardrail: start local, prove before promoting.

## Scripted Task Proofs

The first pressure-test pass must prove:

- Create or load the one-song loop session.
- Launch Verse, then Chorus, then Drop on bar boundaries.
- Select a visual clip and explain what musical signals drive it.
- Create three bass clip variations through the agent panel.
- Bind kick onset to a visual parameter and preview the result in the mock.
- Ask why the Drop looks more intense and receive an answer in scene/clip/binding language.
- Complete the primary path without opening the node graph.

## Acceptance Criteria

The first feature slice can move from prototype to native implementation only when:

- the primary task can be completed from Session View
- audio timing is governed by master BPM and time signature
- visual reactivity is visible as bindings, not hidden graph trivia
- agent actions use high-level session concepts
- any feature that fails the task proof is redesigned in the mock before native work starts

## Explicit Non-Goals

- Do not implement new MCP tools yet.
- Do not change graph schema or runtime behavior yet.
- Do not expand the core operator catalog as part of this pressure test.
- Do not treat the HTML mock as the production UI architecture.
