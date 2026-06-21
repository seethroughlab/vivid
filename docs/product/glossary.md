# Vivid 4 Glossary

Status: draft

## Purpose

Vivid 4 treats vocabulary as architecture. This glossary defines product terms before they become
schemas, APIs, UI labels, MCP tools, or implementation assumptions.

## Terms

### Agent

The LLM collaborator that inspects, explains, edits, varies, and pressure-tests a Vivid project using
high-level product concepts before dropping into implementation details.

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
`kick_onset -> particle_burst` or `bass_envelope -> particle_size`.

### Clip

A behavior capsule launched from a track in a scene. A clip may contain MIDI notes, a theory pattern,
plugin state, automation, visual state, mapping behavior, or another session-level behavior.

### Cue Path

A performance progression through scenes. Cue paths may wait, loop, branch, or advance on musical
conditions, but they are not a linear arrangement timeline.

### Graph

The deeper implementation view where low-level operators, signals, and runtime structure can be
inspected or edited. The graph is not the primary Vivid 4 authoring surface.

### Live Take

The take currently active in a variation well — the one that plays and drives its cell. Switching
the live take auditions an alternative against the running loop without discarding the others. See
Take, Variation Well.

### Master Musical Transport

The shared musical clock for a session: BPM, time signature, beat, bar, phrase, and launch
quantization.

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

The primary Vivid 4 authoring surface. It presents the session as tracks, clips, scenes, transport,
bindings, selection, and agent actions.

### Take

One candidate clip held in a variation well: a concrete behavior (notes, pattern, plugin state,
visual state, or mapping) that can become the cell's live clip. A take may be *kept* (marked worth
holding onto) or *branched* (copied to mutate). See Clip, Live Take, Variation Well.

### Track

A role or responsibility in the session. A track may be an instrument, audio lane, visual layer,
mapping lane, or hybrid behavior lane.

### Variation

An alternative clip, scene, binding, or parameter behavior generated or edited while preserving its
musical/visual role and compatibility with the surrounding session.

### Variation Well

The set of takes available for a single track-and-scene cell, with one marked live. The well makes
the experimentation loop — audition, keep, branch, compare — local to the cell where the work
happens; agent-generated variations land in it. See Take, Live Take, Variation.
