# Q3 Plan: Audio-Visual Parity Validation

## Summary

Validate Vivid’s core thesis of audio-visual parity at the workflow level, not just the architecture level.

Q3 is not primarily an implementation phase. It is a structured validation phase that turns the current parity claim into explicit evidence. The work should answer three product questions from `docs/PRD.md`:

- **equal ease of creation**
- **equal breadth of options**
- **easy cross-domain interaction**

The output of Q3 should be:

- a parity rubric
- a small canonical set of parity workflows
- a documented findings summary
- tightly scoped follow-up fixes only where the gap is small and obvious

This phase should leave the PRD scorecard able to justify a stronger parity judgment, or clearly explain why parity is still only partial.

## Implementation Changes

### 1. Define one parity evaluation rubric and use it consistently

Turn parity into a repeatable evaluation instead of an impressionistic judgment.

The rubric should score exactly these categories:

- **Ease of creation**
  - how quickly a creator can get to a meaningful first result
  - whether the workflow is comparable across audio-first and visual-first paths
- **Breadth of options**
  - whether the shipped surface offers enough meaningful exploration depth in both domains
  - whether missing depth is a product gap or simply an example/docs gap
- **Cross-domain interaction**
  - how easily shared data can drive both audio and visuals in one graph
  - whether cross-domain modulation feels like a normal workflow, not a special case
- **Inspectability**
  - whether both domains expose enough live information to support iteration
- **LLM support**
  - whether the same workflow can be scaffolded, mutated, and understood through current LLM-facing tools

Use the same status language across the rubric:

- `Strong`
- `Adequate`
- `Weak`

Do not use a fine-grained scoring system in Q3. The output should be easy to discuss and revise.

### 2. Lock the parity validation to three canonical workflows

Evaluate parity through these three workflow families only:

- **Audio-first workflow**
  - start with an audio-driven idea
  - add visual response on top
- **Visual-first workflow**
  - start with a visual scene or motion idea
  - add audio behavior on top
- **Cross-domain-first workflow**
  - start with one shared control/data source and drive both domains together

Recommended canonical scenarios:

- **Audio-first**
  - start from a synth/rhythm graph
  - add FFT or control-derived visual response
- **Visual-first**
  - start from a GPU motion/feedback graph
  - add audio behavior that is meaningfully coupled to the same structure
- **Cross-domain-first**
  - start from one shared control source such as clock/LFO/envelope/analysis
  - route it into both audio and GPU branches in one patch

These workflows should use:

- seed operators first
- package operators only when they materially improve realism
- no custom operator authoring unless required to demonstrate a concrete parity blocker

### 3. Create one canonical parity fixture set

Q3 should leave behind a small, named set of parity fixtures that the team can reuse later.

Required fixture types:

- one audio-first parity graph
- one visual-first parity graph
- one cross-domain-first parity graph

Each fixture should have:

- a graph file
- a short workflow note
- the expected parity rationale
- a known-good result description

These are validation fixtures, not showcase demos. Keep them small, stable, and easy to rerun.

### 4. Separate the four kinds of parity gaps

The main value of Q3 is not only “score parity.” It is classifying why parity is not stronger yet.

Every weakness found must be classified into exactly one of:

- **Operator gap**
  - a real missing capability or thin domain surface
- **Docs/example gap**
  - the capability exists, but the workflow is poorly demonstrated
- **Exploration-surface gap**
  - the capability exists, but the interface makes it harder to explore in one domain
- **Product bias**
  - the system is subtly easier or more discoverable in one domain than the other

Do not leave findings as generic “audio still feels weaker” or “visual feels easier.” Every finding should land in one bucket.

### 5. Validate cross-domain interaction explicitly

Because cross-domain interaction is the strongest differentiator in the PRD, it needs its own specific checks.

For each canonical workflow, evaluate:

- whether shared control data can drive both domains without special casing
- whether routing is equally easy in both directions that are architecturally allowed through control
- whether the creator can understand the shared signal path from the graph and current tooling
- whether the live output makes the interaction legible enough to iterate on

The success condition here is not “everything is symmetrical.” It is that cross-domain interaction feels native and ordinary inside the same graph.

### 6. Use LLM-assisted validation as part of the parity proof

Parity in Vivid is also about whether the same workflows are equally available to current LLM tooling.

For each canonical workflow, validate whether an LLM can:

- inspect the graph meaningfully
- scaffold the workflow from current operators
- explain the cross-domain signal path
- suggest a useful next variation or adjustment

This should not become a broad MCP evaluation. It is a narrow parity-specific check on the existing LLM surface.

### 7. Produce two output artifacts

Q3 should leave behind:

- **Parity Validation Report**
  - rubric results
  - workflow findings
  - classified gap list
- **Parity Fixture Index**
  - the canonical graphs/workflows used for evaluation

The report should be internal-facing and decision-oriented.
The fixture index can live closer to demo/docs material if that makes future reuse easier.

## Public Interfaces / Artifacts

Q3 does not require new public runtime APIs by default.

The durable outputs should be:

- one parity rubric document
- one parity findings report
- one parity fixture set
- optional targeted updates to:
  - `docs/internal/PRD-CONFORMANCE-SCORECARD.md`
  - `docs/GETTING-STARTED.md`
  - `graphs/README.md`

Only add new runtime/control-server surface in Q3 if a parity blocker is small and obvious enough to fix immediately.

## Test Plan

### Validation scenarios
Run these three end-to-end evaluations:

1. **Audio-first**
   - create or load a meaningful audio patch
   - add visual response in the same graph
   - evaluate ease, breadth, and interaction
2. **Visual-first**
   - create or load a meaningful visual patch
   - add audio response in the same graph
   - evaluate ease, breadth, and interaction
3. **Cross-domain-first**
   - create or load a graph where one shared control source drives both domains
   - evaluate whether the interaction feels native and legible

### Evidence to collect
For each workflow, record:

- graph used
- operator/package surface relied on
- which parity categories scored `Strong`, `Adequate`, or `Weak`
- any findings classified as:
  - operator gap
  - docs/example gap
  - exploration-surface gap
  - product bias

### Acceptance scenarios
Q3 is complete when these are all true:

1. There is a reusable parity rubric.
2. There are three canonical parity workflows with durable fixtures.
3. The parity claim is backed by workflow evidence, not only architecture argument.
4. Every identified weakness is classified into one of the four parity-gap buckets.
5. The PRD scorecard can justify either a stronger parity judgment or a clearer, evidence-backed partial judgment.

## Assumptions And Defaults

- Q3 is validation-first, not feature-first.
- Seed operators and current package ecosystem are the primary validation surface.
- The goal is not numerical operator parity between audio and visual domains.
- The goal is not to prove that every technique exists in both domains.
- The key parity bar is workflow reality:
  - can creators and LLMs build, connect, inspect, and evolve audio and visuals as peers in one graph?
- Small, obvious parity fixes may be bundled into Q3 only if they emerge directly from the validation and do not expand the phase into a broader product sprint.
