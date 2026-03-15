# PRD Alignment Roadmap

This roadmap turns the current PRD conformance gaps into an execution order.

Source inputs:

- `docs/PRD.md`
- `docs/internal/PRD-CONFORMANCE-SCORECARD.md`
- `docs/ROADMAP.md`

The goal is not to close every aspirational PRD idea immediately. The goal is to raise Vivid’s PRD conformance in the areas that most affect the product thesis:

- Vivid as an exploration environment, not just a runtime
- audio and visuals as genuine peers
- LLMs as collaborators, not just scaffolding tools
- hot, inspectable, trustworthy iteration loops

## Priority Order

### P0. Restore PRD-Critical Workflow Reliability

#### Why this comes first

Before expanding the product surface, the existing PRD-critical workflow claims need to be trustworthy under test.

Current live evidence still weakens the scorecard in two core areas:

- audio hot reload
- live package rebuild behavior

Those sit directly under the PRD’s strongest workflow claims around hot reload, runtime continuity, and extensibility.

#### Scope

- fix the current `test_audio_hot_reload` failure
- fix the current `test_control_server` live package rebuild refresh failure
- ensure these workflows remain covered by regression tests
- tighten any supporting diagnostics if the current failure modes are still too opaque

#### Success criteria

- compatible audio hot reload succeeds and preserves the current hardened safety contract
- incompatible audio hot reload is rejected safely
- live package rebuild updates the running graph coherently
- the PRD scorecard no longer needs a workflow-reliability caveat in these two areas

#### What this does not include

- no new experimentation surfaces
- no broader package redesign
- no perception expansion yet

## P1. Decide and Deliver the 1.0 Exploration Surface

#### Why this comes second

The biggest PRD/product gap is the experimentation-interface gap.

The product currently has:

- a strong node graph
- a narrower variation strip
- strong runtime and authoring infrastructure

But the PRD describes a richer exploration environment. The project needs a concrete 1.0 decision here:

- either implement more of that exploration surface
- or narrow the PRD/roadmap so the public promise matches the actual 1.0 target

This is the most important product-shape decision in the roadmap.

#### Locked decision for this phase

Do not try to build all PRD experimentation interfaces at once.

For 1.0, the target should be:

- keep the node graph as the central structural editor
- strengthen the session/variation model into a more visibly exploratory surface
- strengthen one additional first-class exploration surface beyond the node graph
- defer the remaining named interfaces unless they become essential to the 1.0 story

Recommended 1.0 exploration investment:

- **Session / variation surface, expanded beyond the current strip**

Reason:

- it builds directly on an exploration surface that already exists
- it improves visible branching without replacing the current graph connection model
- it gives the LLM a stronger target for generating alternate states and mappings
- it respects the existing decision that graph wiring should remain in the node graph rather than move to a separate matrix

#### Scope

- define the 1.0 exploration-surface set explicitly
- expand the current variation/session model beyond a simple strip if needed for visible branching
- deepen the session / variation surface into a true second first-class exploration surface
- update the PRD or roadmap if some named interfaces are formally deferred

#### Success criteria

- the product has more than one meaningful exploration interface in active use
- creators can explore alternate states and mappings without relying only on linear recall or manual rewiring
- variation/branching is spatially more visible than it is today
- the PRD scorecard’s experimentation-interface gap is reduced from “materially incomplete” to “partially met with a clear 1.0 story” or better

#### What this does not include

- no commitment to ship every PRD interface for 1.0
- no built-in REPL, parameter-space explorer, or state-machine UI unless they are explicitly chosen over the session-surface path

## P2. Strengthen the Perception Layer from Introspection to Analysis

#### Why this comes third

The current perception surface is real and useful, but it mainly supports:

- introspection
- diagnostics
- checks

The PRD promise is stronger:

- the LLM should be able to evaluate quality, compare alternatives, and reason about AV behavior over time

This phase is the biggest opportunity to move the LLM story from “powerful tooling integration” toward “genuine creative collaborator.”

#### Locked decision for this phase

Do not try to implement the full PRD perception system in one pass.

For the next step, target a minimum meaningful analysis tier:

1. one richer **audio analysis** surface
2. one richer **visual analysis** surface
3. one explicit **audio-visual reactivity metric**
4. one **comparison workflow**

Recommended first set:

- audio: spectral character / loudness summary
- visual: brightness / contrast / motion-style comparison beyond raw introspection
- AV metric: onset-response or energy-to-brightness correlation
- comparison: A/B summary for two captures or two graph states

#### Scope

- extend the perception tool surface beyond diagnostics and checks
- keep outputs structured and LLM-usable
- preserve deterministic compact summaries alongside fuller payloads
- add tests for at least one cross-domain metric path

#### Success criteria

- the LLM can ask a higher-level analysis question and receive more than a structural dump
- at least one AV-reactivity metric is implemented and testable
- the product can compare two candidate outputs in a way that meaningfully aids iteration
- the PRD scorecard’s perception gap becomes narrower and more evidence-backed

#### What this does not include

- no broad aesthetic AI layer
- no full-blown artistic judgment system
- no attempt to solve every PRD analysis example at once

## P3. Validate Audio-Visual Parity as a Product Reality

#### Why this comes fourth

Parity is the project thesis, but right now it is evidenced more by architecture than by product-level proof.

After P1 and P2, the product surface should be strong enough to validate parity more honestly.

#### Locked decision for this phase

Treat parity as a workflow-validation problem, not just an operator-count problem.

The focus should be on proving three PRD claims with concrete scenarios:

- equal ease of creation
- equal breadth of options
- easy cross-domain interaction

#### Scope

- define explicit A/V parity evaluation scenarios
- validate representative audio-first, visual-first, and cross-domain-first flows
- use seed operators plus sibling-package surface where appropriate
- identify whether any remaining parity issue is:
  - missing capability
  - missing examples/docs
  - missing exploration surface
  - product bias toward one domain

#### Success criteria

- at least one explicit North Star-style A/V parity scenario is validated end-to-end
- parity claims are backed by creator workflows, not just architectural argument
- the scorecard can upgrade parity from “partial by product evidence” toward a stronger status

#### What this does not include

- no pressure to equalize operator count numerically across domains
- no requirement that every audio and visual technique ship in 1.0

## P4. Add a PRD-Facing Latency Validation Lane

#### Why this comes fifth

The PRD makes concrete responsiveness claims, and the architecture likely supports them. What is missing is a dedicated proof lane.

This is lower priority than the workflow and product-shape gaps because it is mostly an evidence problem, not a conceptual product gap.

#### Locked decision for this phase

Keep the latency lane narrow and PRD-facing.

It should validate only the responsiveness claims that materially affect the product thesis:

- parameter / routing responsiveness
- hot-reload timing
- visibility/inspection responsiveness where practical

This should not become a broad performance lab.

#### Scope

- define a small benchmark/validation lane tied to PRD wording
- capture repeatable measurements or bounded pass/fail assertions
- keep it lightweight enough to run regularly
- use it to support scorecard updates and future release confidence

#### Success criteria

- the project can point to actual evidence for “instant” and “1–3 second” style claims
- regressions in core interaction latency are detectable
- the PRD scorecard can treat responsiveness with more than architectural inference

#### What this does not include

- no generalized profiling framework
- no premature optimization campaign across the whole app

## P5. Reconcile the PRD, 1.0 Roadmap, and Shipped Product Story

#### Why this comes last

By the time P0-P4 are done, the team should know which gaps were:

- actually closed in product/runtime
- deliberately deferred
- still aspirational

At that point, the docs should be reconciled so Vivid is not simultaneously underselling and overselling itself.

#### Scope

- update `docs/PRD.md` where resolved decisions or 1.0 scope boundaries have changed materially
- update `docs/ROADMAP.md` so 1.0 scope reflects the intended experimentation and perception story
- refresh `docs/internal/PRD-CONFORMANCE-SCORECARD.md`
- keep `docs/PRD-GAPS.md` as the roadmap or collapse it into the scorecard/roadmap set if it has been fully consumed

#### Success criteria

- the PRD, roadmap, and shipped product story no longer pull in different directions
- deferred PRD ideas are clearly marked as deferred rather than silently absent
- implemented product surfaces are described as they actually exist

## Cross-Cutting Rules

### 1. Do not treat every PRD idea as a 1.0 obligation

The PRD contains:

- core thesis statements
- architecture commitments
- product-shape ambitions
- open questions
- explicitly deferred concepts

The roadmap should prioritize:

- the core thesis
- the strongest 1.0 product claims
- the areas where misalignment most changes what Vivid *is*

### 2. Prefer strengthening one exploration surface deeply over shipping many shallow ones

For the PRD, one real second exploration interface is more valuable than several placeholder interfaces.

### 3. Prefer measurable perception improvements over broad speculative analysis scope

A single real AV-reactivity metric is more valuable than a long list of unevidenced future analysis ideas.

### 4. Keep scorecard updates coupled to roadmap progress

Each roadmap phase should update `docs/internal/PRD-CONFORMANCE-SCORECARD.md` so PRD alignment is re-evaluated as work lands.

## Recommended Execution Order

1. **P0** — restore PRD-critical workflow reliability
2. **P1** — define and deliver the 1.0 exploration surface
3. **P2** — strengthen perception from introspection to analysis
4. **P3** — validate audio-visual parity as product reality
5. **P4** — add PRD-facing latency validation
6. **P5** — reconcile PRD, roadmap, and shipped story

## Definition of Success

This roadmap succeeds when Vivid can make a stronger version of the following claim honestly:

Vivid is not only architecturally aligned with its PRD, but product-level evidence now supports the central thesis that it is a live, inspectable, LLM-native audiovisual exploration environment where audio and visuals are genuine peers.
