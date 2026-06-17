# Vivid 4 Product Requirements

## Vision

Vivid 4 is an agent-first audiovisual session environment for authoring live performance,
song-like audiovisual pieces, and interactive installations.

The product is the environment, not a large built-in operator catalog. Vivid should make it fast
to compose, inspect, perform, and author project-local code. The core should stay small; user and
project code are the creative material.

## Product Thesis

Vivid 4 starts from the hard-learned lessons of Vivid Classic:

- Session View should be the primary authoring surface.
- Audio authoring should be plugin-first, not an attempt to rival mature synths and effects.
- Visuals should remain first-class in power, but often second-order in reactivity: music and
  session structure provide time, while visuals bind to musical/control signals.
- The graph is a deep implementation view, not the first surface a user or agent must manipulate.
- The agent should work in session concepts first: tracks, clips, scenes, bindings, variations,
  explanations, and task proofs.

## Core Principles

### 1. Session First

The default question is not "what nodes are connected?" It is "what should this performance section
do?"

Session View organizes the piece as tracks, clips, scenes, and cue paths. The graph remains available
for inspection and low-level authoring, but the primary workflow should not require graph vocabulary.

### 2. Master Musical Transport

Vivid has one master musical transport: BPM, time signature, beat, and bar. Session launches, note
clips, pattern generators, plugin sync, follow rules, and rhythmic visual bindings all align to this
grid.

This is not a linear arrangement timeline. A session can branch, wait, loop, or sit in a scene
indefinitely, but it does so against a shared musical clock when timing matters.

### 3. Audio-Visual Parity, Not Symmetry

Audio and visuals should be equal in expressive power and inspectability, but they do not need the
same interface model.

Audio owns musical time: tempo, meter, clips, phrase length, rhythm, harmony, and plugin sound.
Visuals own spatial behavior: layers, looks, motion, density, color, camera, and output. Bindings own
the relationship between them.

### 4. Plugin-First Music Authoring

Vivid should integrate existing instruments and effects rather than compete with them. VST3, CLAP,
and AU hosting are central to the product direction.

Native audio operators should be modest: routing, mixing, analysis, utility processing, reference
implementations, and small helpers that make the session environment work.

### 5. Facilitate Code Authoring

Vivid should make custom code feel safe and immediate. The environment should provide scaffolding,
hot reload, examples, diagnostics, and agent assistance, but it should not accumulate every useful
idea as a permanent core operator.

Core operators belong only when they are infrastructure-like, broadly necessary across projects, or
essential to the default session workflow. Everything else starts as project-local code or a package.

### 6. Agent-First Workflows

The agent should not be forced to reason in raw graph topology for normal work. It should be able to:

- inspect the session at a high level
- create tracks, clips, scenes, and bindings
- generate clip variations
- explain why a scene sounds or looks a certain way
- identify which musical signals drive which visual behaviors
- suggest code only when the environment needs new project-local behavior

## Primary Surface: Vivid Session View

Vivid Session View adapts Ableton's clip-launching idea to audiovisual behavior.

### Tracks

Tracks are responsibilities in the piece, not only audio channels.

Initial track kinds:

- `instrument` - plugin/native instrument plus note clips, macro state, effects, and mixer lane
- `audio` - audio clips, looping, file playback, and effects
- `visual` - visual states, layers, cameras, palette, shader behavior, and output looks
- `mapping` - audiovisual bindings between musical/control signals and visual/audio destinations
- `hybrid` - deliberately bundled musical and visual behavior

### Clips

Clips are behavior capsules. A clip may contain MIDI notes, a theory generator, plugin state,
automation, a visual look, or an audiovisual binding.

The session grid should communicate what the clip does without requiring the user to open the graph.

### Scenes

Scenes launch coordinated clip assignments across tracks. A scene is a named audiovisual section:
Intro, Verse, Chorus, Drop, Ambient, Blackout, Reset, Audience Reactive, or similar.

### Bindings

Visual bindings are first-class session objects:

- kick onset -> particle burst
- bass envelope -> particle size
- snare transient -> bloom
- chord brightness -> palette
- scene energy -> camera shake

Bindings must be visible, explainable, and editable at the session level before they become graph
implementation details.

## First Proof Target

The first Vivid 4 proof is a one-song loop:

- master transport at 124 BPM, 4/4
- tracks: Drums, Bass, Chords, Lead, Particles, Camera/Palette, AV Mapping
- scenes: Intro, Verse, Chorus, Drop
- clips spanning MIDI/theory, plugin state, visual state, and binding behavior
- agent actions for bass variations, kick-to-particle binding, and Drop explanation

The pressure-test plan and disposable HTML mock are the current source of truth for this proof:

- [`docs/plans/vivid-4-session-view-pressure-test.md`](plans/vivid-4-session-view-pressure-test.md)
- [`docs/prototypes/vivid-4-session-view.html`](prototypes/vivid-4-session-view.html)

## Development Gate

No native runtime, schema, or MCP implementation should happen before the relevant product behavior
passes a task proof.

Gate sequence:

1. PRD assumption gate
2. HTML prototype gate
3. Agent workflow gate with mocked high-level MCP responses
4. Smallest native slice gate
5. Promotion gate into runtime/API/schema architecture

The Vivid Classic codebase is preserved on the `vivid-classic` branch and tagged
`vivid-classic-final`. Borrow code only when it serves the Vivid 4 architecture without importing the
old mental model.
