# Vivid 4 Glossary

Status: draft

## Purpose

Vivid 4 treats vocabulary as architecture. This glossary defines product terms before they become
schemas, APIs, UI labels, MCP tools, or implementation assumptions.

> First-release note (UX Ph1 F1): terms marked **_(planned)_** name the experimentation-loop model
> that is **not in the first release** — there is no UI or data model for them yet. They are defined
> here ahead of implementation, on purpose; treat them as the roadmap vocabulary, not shipped
> features.

## Terms

### Agent

The LLM collaborator that inspects, explains, edits, varies, and pressure-tests a Vivid project using
high-level product concepts before dropping into implementation details. In the current product
architecture, the agent drives Vivid through MCP tools backed by the loopback control server.

### Agent Adapter

The swappable component that connects a Vivid project to a specific agent provider, translating
Vivid's capability intents and project text to and from that provider's API. Adapters live outside
Vivid core, so vendor API churn never reaches the core. See ADR-0008.

### Agent Provider

The external process that fulfills agent capabilities for a project — Claude.app, a CLI agent, a
local model, or a user script. The model always runs outside Vivid core; a project may have one
attached or none. See ADR-0008, Agent Adapter.

### Audio-Visual Binding

A first-class relationship between a source signal and a destination behavior, such as
`kick_onset -> particle_burst` or `bass_envelope -> particle_size`. In the current implementation,
bindings are represented by the Mapping Bridge.

### Bridge

The first-class relationship layer between the DAW surface and the visuals graph. The bridge lets
audio characteristics drive visual node parameters and lets visual state drive audio/plugin
parameters.

### Clip

A behavior capsule launched from a track in a scene. A clip may contain MIDI notes, a theory pattern,
plugin state, automation, visual state, mapping behavior, or another session-level behavior.

### Cue Path _(planned)_

A performance progression through scenes. Cue paths may wait, loop, branch, or advance on musical
conditions, but they are not a linear arrangement timeline.

### Graph

The primary visuals authoring surface. It contains visual operators, texture edges, data-source
nodes, Output nodes, and live visual state. It is paired with Session View rather than hidden beneath
it.

### Live Take _(planned)_

The take currently active in a variation well — the one that plays and drives its cell. Switching
the live take auditions an alternative against the running loop without discarding the others. See
Take, Variation Well.

### Master Musical Transport

The shared musical clock for a session: BPM, time signature, beat, bar, phrase, and launch
quantization.

### Mapping

One bridge wire from a named source to a named destination, plus shaping values such as amount,
curve, polarity, and output range. Examples: `track_12.transient -> node:7.warp` and
`viz.warp -> param:1:0:42`.

### Project-Local Code

Custom code that belongs to one Vivid project before it earns promotion into a package or seed
operator.

### Scene

A named audiovisual section that launches coordinated clip assignments across tracks, such as Intro,
Verse, Chorus, Drop, Breakdown, or Reset.

### Session

The top-level authoring and performance state: transport, tracks, clips, scenes, bindings, selected
state, queued launches, and related agent-readable context.

### Session View

The primary DAW-style authoring surface. It presents tracks, clips, scenes, devices, transport,
selection, and performance state. It is one of Vivid's two primary surfaces, paired with the visuals
Graph.

### Take _(planned)_

One candidate clip held in a variation well: a concrete behavior (notes, pattern, plugin state,
visual state, or mapping) that can become the cell's live clip. A take may be *kept* (marked worth
holding onto) or *branched* (copied to mutate). See Clip, Live Take, Variation Well.

### Track

A role or responsibility in the session. A track may be an instrument, audio lane, visual layer,
mapping lane, or hybrid behavior lane.

### Variation _(planned)_

An alternative clip, scene, binding, or parameter behavior generated or edited while preserving its
musical/visual role and compatibility with the surrounding session.

### Variation Well _(planned)_

The set of takes available for a single track-and-scene cell, with one marked live. The well makes
the experimentation loop — audition, keep, branch, compare — local to the cell where the work
happens; agent-generated variations land in it. See Take, Live Take, Variation.
