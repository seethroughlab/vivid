# PRD Alignment Implementation Queue

This document turns the remaining PRD conformance gaps into the next implementation queue.

Source inputs:

- `docs/PRD.md`
- `docs/internal/PRD-CONFORMANCE-SCORECARD.md`
- `docs/ROADMAP.md`

This queue is intentionally narrower than the full PRD. It is the next practical sequence for improving PRD conformance after the workflow-reliability fixes landed.

## Current Baseline

Recently closed:

- audio hot reload reliability
- live package rebuild refresh reliability

The remaining high-value PRD gaps are now:

1. experimentation surface beyond the node graph
2. perception and comparison depth
3. product-level audio-visual parity proof
4. latency evidence
5. North Star / product-story validation

## Queue Rules

- Keep the node graph as the structural and connection editor.
- Do not introduce a patchbay or connection matrix.
- Prefer one strong, finished surface over several half-built ones.
- Treat PRD alignment as product work, not just runtime work.
- Each queue item must leave behind durable evidence:
  - tests
  - docs
  - a validated workflow

## Queue Overview

| Queue | Theme | Why it matters |
|---|---|---|
| Q1 | Session / variation exploration surface | Biggest product-shape gap in the current PRD scorecard |
| Q2 | Perception analysis tier | Biggest LLM-collaboration gap after introspection/checks |
| Q3 | Audio-visual parity validation | Core thesis still stronger in architecture than in demonstrated workflow |
| Q4 | PRD-facing latency lane | Needed to back responsiveness claims with evidence |
| Q5 | North Star and product-story reconciliation | Needed to align shipped product, docs, and 1.0 promise |

Phase docs:

- [Q1: Session / Variation Exploration Surface](docs/PRD-GAPS-Q1.md)
- [Q2: Perception Analysis Tier](docs/PRD-GAPS-Q2.md)
- [Q3: Audio-Visual Parity Validation](docs/PRD-GAPS-Q3.md)
- [Q4: PRD-Facing Latency Validation Lane](docs/PRD-GAPS-Q4.md)
- [Q5: North Star Validation And Product-Story Reconciliation](docs/PRD-GAPS-Q5.md)

## Q1. Session / Variation Exploration Surface

### Goal

Turn the current variation system from a useful strip into a clearly first-class exploration surface.

### Why this is first

The scorecard’s largest remaining product gap is not runtime reliability anymore. It is that Vivid still feels strongest as:

- a graph editor
- a runtime
- an operator/package platform

The PRD asks for something more exploratory than that. The shortest path to closing that gap is to deepen the variation/session surface that already exists.

### Scope

- expand the current variation surface beyond simple save/recall affordances
- make alternate states visibly present at once, not only serially recalled
- improve variation naming, organization, and quick comparison workflow
- support variation-oriented iteration without requiring graph rewiring
- keep all graph wiring in the node graph

### Deliverables

1. A clearer session/variation model in the UI
2. Better variation visualization and navigation
3. A reliable workflow for:
   - save current state
   - branch a variation
   - compare or audition alternatives
   - promote one variation back to the working state
4. Updated docs showing the exploration workflow

### Dependencies

- existing variation runtime/control-server support
- existing UI/runtime contract hardening

### Exit Criteria

- Vivid has more than one meaningful exploration surface in active use
- alternate states are visibly present in the product, not just hidden in recall commands
- the PRD scorecard can upgrade the experimentation gap materially

### Explicit non-goals

- no patchbay
- no built-in REPL in this slice
- no state-machine UI in this slice unless it becomes necessary to make the session surface coherent

## Q2. Perception Analysis Tier

### Goal

Move the perception layer from mostly introspection/checks into structured higher-level analysis.

### Why this is second

The LLM story is already strong on:

- graph mutation
- operator authoring
- diagnostics
- checks

What is still weak is the critic/analyst role. This is the next most important PRD-alignment step after the experimentation surface.

### Scope

- add one richer audio analysis surface
- add one richer visual analysis surface
- add one explicit AV-reactivity metric
- add one comparison workflow for two captures, two graph states, or two variations
- keep outputs structured and compact enough for LLM use

### Recommended first cut

- audio: spectral/loudness summary
- visual: brightness/contrast/motion-style summary
- AV metric: audio-energy to visual-response correlation
- comparison: A/B summary between two candidate outputs

### Deliverables

1. New perception endpoints or extensions to current endpoints
2. Regression tests for the new analysis outputs
3. Docs for the new perception contract
4. At least one real example where analysis helps choose between two versions

### Dependencies

- current introspection/diagnostics/checks infrastructure
- capture/export surfaces where needed

### Exit Criteria

- the LLM can answer a higher-level quality question without only dumping structure
- at least one AV metric is implemented and validated
- at least one comparison workflow is available and documented

### Explicit non-goals

- no vague “AI taste” system
- no attempt to solve every PRD perception example at once

## Q3. Audio-Visual Parity Validation

### Goal

Prove audio-visual parity at the workflow level, not just the architecture level.

### Why this is third

The architecture strongly supports parity already. What is still weak is product proof:

- equal ease
- equal breadth
- easy cross-domain interaction

This queue item should validate the claim honestly and identify where remaining parity problems actually come from.

### Scope

- define a small set of explicit parity workflows
- test audio-first, visual-first, and cross-domain-first creation paths
- identify whether each weakness is:
  - operator gap
  - docs/example gap
  - exploration-surface gap
  - product bias

### Deliverables

1. A short parity evaluation rubric
2. A set of representative parity demo graphs or workflows
3. A small findings summary on where parity still falls short
4. Follow-up fixes only if the issue is small and obvious

### Dependencies

- Q1 should land first so the exploration story is not judged only through the graph
- Q2 is helpful but not strictly required

### Exit Criteria

- parity is evidenced by real workflows, not only by system architecture
- the scorecard can justify a stronger parity judgment
- remaining parity gaps are classified clearly instead of being hand-wavy

### Explicit non-goals

- no operator-count balancing exercise
- no claim that every audio and visual technique must ship for 1.0

## Q4. PRD-Facing Latency Validation Lane

### Goal

Add a narrow validation lane for the responsiveness claims the PRD makes explicitly.

### Why this is fourth

This is mostly an evidence problem, not a product-shape problem. It matters, but it should follow the more structural PRD gaps.

### Scope

- define a small benchmark/validation lane around:
  - parameter responsiveness
  - routing responsiveness
  - hot-reload timing
  - inspection responsiveness where practical
- keep it lightweight and repeatable
- tie the results to PRD wording rather than broad profiling

### Deliverables

1. A small automated or semi-automated latency validation lane
2. A doc explaining what is measured and what pass/fail means
3. Stable reporting that can be cited by the PRD scorecard

### Dependencies

- existing stress and regression harnesses
- current hot-reload and runtime hardening

### Exit Criteria

- the project can point to concrete evidence for the main responsiveness claims
- regressions in PRD-critical latency paths become detectable

### Explicit non-goals

- no full performance lab
- no generalized telemetry project

## Q5. North Star Validation And Product-Story Reconciliation

### Goal

Align the PRD, roadmap, and shipped product story after Q1-Q4 produce real outcomes.

### Why this is last

This queue item depends on the others. It is where we convert implementation and validation into a cleaner 1.0 story.

### Scope

- validate one explicit North Star-style workflow end to end
- update docs where 1.0 scope is now clearer
- separate:
  - shipped strength
  - near-term 1.0 commitments
  - clearly deferred ideas

### Deliverables

1. One validated North Star scenario
2. Updated PRD/roadmap wording where needed
3. A cleaner statement of what Vivid 1.0 is and is not

### Dependencies

- Q1 through Q4 should be substantially complete first

### Exit Criteria

- the product story is tighter and less aspirationally blurry
- PRD conformance can be discussed without relying on caveats that are no longer useful

### Explicit non-goals

- no full PRD rewrite
- no broad marketing pass disconnected from actual product state

## Recommended Execution Order

### Immediate next queue

1. Q1. Session / variation exploration surface
2. Q2. Perception analysis tier

### After that

3. Q3. Audio-visual parity validation
4. Q4. PRD-facing latency lane
5. Q5. North Star validation and product-story reconciliation

## What “Done” Looks Like

This queue is successful when:

- Vivid is not only reliable and extensible, but more visibly exploratory
- the LLM can analyze and compare output, not only inspect and scaffold
- audio-visual parity is demonstrated in workflows, not just claimed in architecture
- responsiveness claims are evidence-backed
- the 1.0 story is clearer and easier to defend
