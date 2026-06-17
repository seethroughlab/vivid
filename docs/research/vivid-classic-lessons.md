# Vivid Classic Lessons

Status: living reference

## Purpose

This document records product and process lessons from the Vivid Classic codebase and commit
history. It is not a list of implementation details to preserve. It is a set of guardrails for
Vivid 4 so the reboot can borrow hard-won insight without inheriting the old mental model.

## Lessons To Carry Into Vivid 4

### 1. Vocabulary Is Architecture

Classic repeatedly paid migration costs when early vocabulary hardened before the concepts were
clear. The `spread` to `lane` to `value/multiplicity` arc is the clearest example.

Vivid 4 should name core concepts slowly and protect them once they are proven. Terms like `track`,
`clip`, `scene`, `binding`, `source`, `destination`, `variation`, `transport`, and
`project-local code` need short definitions before they become APIs, schemas, UI labels, or MCP
tools.

Principle: name concepts slowly; rename aggressively before they spread.

### 2. Musical Time Is Infrastructure

Classic repeatedly circled around clocks, metronome sync, bar-locked clips, time signatures,
tempo-synced effects, and timing vocabulary. The lesson is that musical time cannot be incidental.

Vivid 4 should start with one master musical transport: BPM, time signature, beat, bar, phrase, and
launch quantization. Local clocks may exist as creative modulation sources, but the session needs a
shared musical grid.

Principle: musical time belongs to the environment, not to scattered operators.

### 3. Plugin State Is A Product Promise

Classic plugin work exposed repeated risks around VST3/CLAP/AU state loss, topology changes, sample
rate changes, hot reload, preset recall, direct parameters, and inspector behavior.

Plugin-first music authoring means Vivid 4 must treat plugin state preservation as a core product
promise. Session edits, graph edits, reloads, and agent actions must not silently destroy the sound.

Principle: never let structural edits silently change or lose the sound.

### 4. Agent APIs Need Semantic Compression

Classic added many MCP tools, then needed summaries, diagnostics, large-output ergonomics,
operator docs, perception tools, and higher-level inspection. More endpoints were not enough.

Vivid 4 agent tools should return intent-shaped state: session overview, active scene, queued
launches, clip roles, musical signals, visual bindings, plugin roles, and task-relevant evidence.
Raw topology should be available, but it should not be the default language of normal agent work.

Principle: agent APIs should return product concepts before implementation internals.

### 5. Prototype Interfaces Before They Become Architecture

Classic explored several interface directions: extension workflows, devtools, webview/IDE attempts,
docking, native graph UI, dedicated editor windows, and session grids. Some were useful; some became
expensive detours.

Vivid 4 should use disposable prototypes to prove interaction models before making runtime, schema,
or native UI commitments.

Principle: an interface earns architecture only after a task proof.

### 6. Dedicated Editors Must Earn Their Place

Classic gained many focused editors and inspectors: MIDI clip, tracker, drum sequencer,
arpeggiator, MSEG, parametric EQ, SP404, waveform editing, and plugin macro views. These improved
specific workflows but also expanded the surface area quickly.

Vivid 4 should prefer session-level editing first. A dedicated editor should appear only when a task
proof shows the Session View cannot carry the workflow clearly.

Principle: edit in session terms until a focused editor proves it is necessary.

### 7. Diagnostics Are Creative Surface Area

Classic's later history added dropped-connection warnings, crash guards, health telemetry,
GPU/audio diagnostics, movie diagnostics, lockfile findings, semantic UI smoke tests, and
agent-facing inspection.

Vivid 4 should treat failures, missing assets, invalid bindings, plugin problems, timing drift, and
runtime health as inspectable product state that both the user and agent can understand.

Principle: every meaningful failure should become explainable state.

### 8. Product Proof Comes Before Platform Breadth

Classic spent significant energy on Windows, Linux, Raspberry Pi, CI, release packaging, and
cross-platform dependency behavior. That work matters eventually, but it can outrun product clarity.

Vivid 4 should stay macOS-first during early product proof unless a phase explicitly requires a
broader platform target.

Principle: do not let portability work outrun product proof.

### 9. Demos Are Not Enough

Classic accumulated many demo graphs and example projects, then increasingly moved toward scripted
verification, smoke tests, semantic assertions, and production gates.

Vivid 4 should make every major feature prove a repeatable task. A demo can inspire, but it does not
prove that the feature is usable.

Principle: a feature is proven by a repeatable task, not by a demo.

### 10. Borrow Subsystems, Not Mental Models

Classic contains valuable work in plugin hosting, hot reload, MCP/perception tooling, diagnostics,
media handling, tests, and runtime hardening. It also contains assumptions that Vivid 4 is explicitly
leaving behind.

Borrowing from Classic should be explicit, narrow, and filtered through the Vivid 4 PRD.

Principle: every borrowed subsystem must pass through the Vivid 4 product model first.

## How To Use This Document

Before planning a phase, check whether the phase risks repeating a Classic pattern:

- Is it introducing vocabulary before the concept is proven?
- Is it scattering musical time across local mechanisms?
- Could it lose plugin or session state silently?
- Is the agent being pushed into raw internals too early?
- Is the UI being made permanent before a task proof?
- Is a custom editor replacing a simpler session-level workflow?
- Are diagnostics being treated as optional engineering detail?
- Is platform or packaging work outrunning product proof?
- Is the evidence only a demo?
- Is borrowed code bringing along an old mental model?

If the answer is yes, the phase should be redesigned or narrowed before implementation planning.
