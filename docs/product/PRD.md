# Vivid 4 Product Requirements

## Vision

Vivid 4 is an agent-first audiovisual session environment for authoring live performance,
song-like audiovisual pieces, and interactive installations.

The product is the environment, not a large built-in operator catalog. Vivid should make it fast
to compose, inspect, perform, and author project-local code. The core should stay small; user and
project code are the creative material.

## Product Thesis

Vivid 4 starts from the hard-learned lessons of Vivid Classic:

- Vivid has two primary authoring surfaces: a DAW-style Session View and a rewireable
  visuals node graph.
- Audio authoring should be plugin-first, not an attempt to rival mature synths and effects.
- Visuals should remain first-class in power, but often second-order in reactivity: music and
  session structure provide time, while visuals bind to musical/control signals.
- The mapping bridge is the parity layer: audio characteristics can drive visual parameters, and
  visual state can return to audio/plugin parameters.
- The agent should work in product concepts first: tracks, clips, scenes, operators, mappings,
  variations, explanations, and task proofs.

## Scope

Vivid is for real-time audiovisual works where audio, visuals, interaction, and performance state
shape each other:

- live audiovisual performance
- song-like audiovisual pieces
- interactive and generative installations
- museum, gallery, and branded experiential work
- creative prototyping where hearing, seeing, and modifying the system must stay close together

Vivid is not trying to become a game engine, a traditional DAW, a film/video post-production tool, a
general desktop app framework, or a replacement for mature synths and effects.

## Core Principles

### 1. Two Surfaces, One Transport

The default workspace is not one blended surface. Audio and visuals each get the interface model their
domain deserves.

Session View organizes music as tracks, clips, scenes, devices, and performance state. The visuals
node graph organizes generators, effects, outputs, and visual logic. Both share the same musical
transport and meet through first-class mappings.

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
Visuals own spatial behavior: layers, looks, motion, density, color, camera, and output. The mapping
bridge owns the relationship between them.

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

### 6. Experimentation First

The central product loop is try, perceive, branch, compare, and refine. Vivid should make it cheap to
audition ideas, inspect what happened, keep the good accidents, and ask the agent for focused
variations.

Audio and visual exploration have different rhythms. Audio often needs stable loops, A/B comparison,
musical context, and theory-aware variation. Visual exploration can mutate faster against a stable
musical anchor. The interface should support both without pretending they are the same activity.

### 7. Text Is the Source of Truth

The project state should be readable, diffable, recoverable, and agent-addressable. Session
structure, clips, bindings, plugin references, visual states, and project-local code should have a
clear textual representation.

The visual interface is an authoring and performance surface over that state, not the only place the
truth exists.

### 8. See Every Step

The user and agent should be able to inspect what is happening at each meaningful layer: active
scene, queued launches, clip content, generated variations, plugin roles, control signals, visual
nodes, mappings, and the project-local code that implements them.

Vivid should favor explainable state over hidden magic. When the Drop looks more intense, the system
should be able to answer in scene, clip, graph-node, and mapping language.

### 9. Hot Reload Project Code

Custom project code should feel immediate. Editing a visual behavior, theory generator, mapping
helper, or local operator should preserve the session whenever possible and return feedback quickly
enough to stay in the creative loop.

Hot reload is a product requirement because it protects experimentation, not because dynamic loading
is inherently valuable.

### 10. Creator Tools, Not Developer Tools

Vivid should integrate with the user's existing development environment rather than absorbing it.
External editors, source control, package managers, plugin installers, and asset tools remain the
right homes for their own jobs.

The Vivid interface should focus on real-time feedback, performance control, inspection,
coordination, and agent-mediated authoring.

### 11. Do Not Reinvent the Wheel

Vivid should rely on excellent existing tools where they already solve the problem: VST3/CLAP/AU
plugins for sound, external IDEs for code, normal text files for project state, and mature libraries
for specialized runtime needs.

Vivid's job is to make those parts playable, inspectable, reactive, and agent-authorable as one
environment.

### 12. Agent-First Workflows

The agent should not be forced to reason in raw graph topology for normal work. It should be able to:

- inspect the session at a high level
- create tracks, clips, scenes, and bindings
- generate clip variations
- explain why a scene sounds or looks a certain way
- identify which musical signals drive which visual behaviors
- suggest code only when the environment needs new project-local behavior

The agent also needs perception and evaluation tools. It should be able to capture output, inspect
semantic state, compare variations, explain tradeoffs, and verify task proofs using the same product
concepts the user sees.

## Retained Lessons From Vivid Classic

The reboot should preserve the strongest ideas from Vivid Classic while rejecting the parts that made
the system harder to steer.

Keep:

- audio and visuals as equal creative domains
- unified, inspectable relationships between music, control, and visuals
- fast feedback loops for parameters, routing, and project-local code
- text-backed state that both people and agents can read
- structured perception and diagnostics for agent workflows
- a sharp, content-forward interface that makes domains legible without decorative chrome

Revise:

- audio-visual parity means equal power and inspectability, not symmetric interfaces
- the node graph becomes a primary visuals authoring surface, not a clone of the DAW surface
- the built-in operator set becomes minimal seed infrastructure, not an ever-growing catalog
- the agent works in product concepts first and authors code only when the environment needs it

Reject:

- no-master-clock temporal plurality for music authoring
- rebuilding mature synths and effects inside Vivid
- forcing audio and visual authoring into a single symmetric interface
- promoting project-specific code into core before repeated use proves the abstraction

## Primary Surfaces: Session View, Visual Graph, Mapping Bridge

Vivid adapts Ableton's clip-launching idea for audio performance and TouchDesigner-style node
authoring for visuals. The bridge between them is first-class and bidirectional.

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

Mappings must be visible, explainable, and editable as product objects. Their implementation may
touch the graph, the DAW surface, or both.

### Visual Graph

The visual graph is the primary authoring surface for visual behavior. It contains operators,
texture edges, data-source nodes, Output nodes, and live visual state. It is not a hidden
implementation detail; it is where visualists author structure.

### Mapping Bridge

The mapping bridge connects sources and destinations across cadences and domains:

- `master.transient -> node:7.warp`
- `track_12.low -> node:9.density`
- `viz.warp -> param:1:0:42`

Mappings carry shaping data such as amount, curve, polarity, and output range.

## First Proof Target

The first Vivid 4 proof is a one-song loop:

- master transport at 124 BPM, 4/4
- tracks: Drums, Bass, Chords, Lead, Particles, Camera/Palette, AV Mapping
- scenes: Intro, Verse, Chorus, Drop
- clips spanning MIDI/theory, plugin state, visual state, and binding behavior
- agent actions for bass variations, kick-to-particle binding, and Drop explanation

The pressure-test plan and disposable HTML mock are historical evidence for the early Session View
direction. The accepted product direction is now recorded in ADR-0009, ADR-0010, and ADR-0011.

- [`docs/experiments/session-view-pressure-test.md`](../experiments/session-view-pressure-test.md)
- [`docs/experiments/session-view-pressure-test.html`](../experiments/session-view-pressure-test.html)

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
