# Embedded Operator Slots

## Summary

This document reconstructs and modernizes the earlier embedded-slots design direction that was removed during the role-bindings transition. It is the new canonical design for this area.

**Embedded operator slots** are the replacement for the earlier role-bindings experiment.

The core model is:

- **Ports** are for generic transport between graph nodes.
- **Embedded slots** are for host-local composition using reusable operator code.
- **Explicit outputs** are for sharing host-local results back into the graph.

This direction is intentionally simpler than the earlier role-bindings approach:

- no graph-level cross-node slot references
- no bind/rebind/reference bookkeeping
- no role-binding-specific save/recall/export machinery
- no need to justify a separate structural relationship beyond ordinary wiring

The preferred product direction is:

- role bindings have been removed entirely
- restore owned embedded composition
- use curated slot types rather than arbitrary bindable assignment
- keep editing inspector-first, with optional expandable local editors rather than graph-visible child nodes

## Why Embedded Slots Exist

The most important reason embedded slots exist is **reusable operator code**.

A user should not have to rewrite envelope, LFO, MSEG, or similar modulation logic every time they make a new synth, particle system, flocking system, or visual simulation. Instead, a host should be able to own a slot and fill it with a reusable operator implementation that already exists in the system.

That gives Vivid three important benefits at once:

1. **Reuse**
   - hosts reuse real operator implementations instead of reimplementing modulation logic ad hoc
2. **Ownership**
   - the slot remains local to the host, so its lifecycle and semantics stay clean
3. **Simplicity**
   - the graph does not need a separate structural binding model to express host-local composition

This is the key idea to preserve from the earlier embedded-slots direction.

## Why Regular Signal Ports Are Not Enough

Ordinary signal ports are still important, but they solve a different problem.

A signal port says:

- this node can receive a value
- this node can emit a value
- values can travel through ordinary graph wiring

That is generic transport.

Embedded slots solve something different:

- the host has a named local modulation role
- the role is satisfied by a reusable operator implementation
- the instance is owned by the host
- the host controls lifecycle, triggering, and interpretation
- the slot is edited as part of the host rather than as a separate graph object

A plain signal port is sufficient when the host merely needs an incoming control value.

A plain signal port is **not** sufficient when the design intent is:

- “this synth has an amplitude envelope slot”
- “this particle system has a particle-envelope slot”
- “this simulation owns a local LFO for viscosity modulation”

In those cases, the slot is not just transport. It is reusable host-local composition.

## Architectural Model

The architecture should use three different mechanisms for three different purposes.

### 1. Ports = Transport

Use ports when a host just needs to send or receive data.

Examples:

- an LFO node drives a parameter through ordinary graph wiring
- a clock drives beat phase through a signal input
- a synth outputs gate or note spread to another operator

Ports remain the right model for:

- generic graph dataflow
- cross-node wiring
- cross-domain sharing of explicit signals

### 2. Embedded Slots = Owned Reusable Local Composition

Use embedded slots when a host needs a reusable operator internally.

Examples:

- a synth owns an `Amplitude Envelope` slot
- a particle system owns a `Particle Envelope` slot
- a visual operator owns a `Scale Mod` slot backed by an LFO

Embedded slots are:

- owned by the host
- serialized as host state
- edited in the host inspector
- not separate shared graph nodes

### 3. Explicit Outputs = Shared Graph-Visible Results

Use outputs when host-local behavior needs to become visible to the rest of the graph.

Examples:

- trigger pulse
- gate state
- envelope energy
- active voice count
- note spread
- chord summary

This lets other operators and other domains react to the result of host-local behavior without exposing the private machinery itself.

## Why Role Bindings Are Being Removed

Role bindings added a separate graph/runtime/UI concept for named structural references between nodes. In practice, that complexity is no longer justified.

The main reasons are:

### 1. They duplicate what ports and ownership already cover

Once host-local modulation is expressed through owned slots, and graph-level sharing is expressed through ordinary ports and outputs, there is little remaining need for a third mechanism.

### 2. They made graph truth heavier than necessary

Role bindings introduced:

- extra graph state
- extra runtime resolution logic
- extra control-server mutation paths
- extra snapshot/read-model fields
- extra UI and inspector complexity
- extra save/recall/export work

That is too much architectural weight for a mechanism whose remaining use case is weak.

### 3. They encouraged accidental complexity

If role bindings are not clearly doing something more valuable than ordinary ports or owned slots, they begin to look like accidental complexity rather than a necessary abstraction.

That is the current conclusion.

## Curated Slot Model

The replacement embedded-slot system should be **curated**, not fully open-ended.

A host slot should allow a small approved set of operator types chosen specifically for that slot.

Examples:

- `Amplitude Envelope` slot:
  - `Envelope`
  - `MSEG`
- `Pitch Mod` slot:
  - `LFO`
  - `MSEG`
- `Particle Envelope` slot:
  - `Envelope`
- `Simulation Mod` slot:
  - `LFO`
  - `RandomSH`
  - `Macro`

This should **not** become:

- “any compatible control operator can go in any slot”

The curated model is preferred because it:

- keeps UX simpler
- keeps validation simpler
- preserves product coherence
- avoids rebuilding role-binding flexibility under another name

## Inspector-First Editing

Embedded slot editing should be **inspector-first**.

Default behavior:

- the host inspector shows a slot card for each embedded slot
- the card shows:
  - slot label
  - selected operator type
  - main parameters
  - actions such as replace, reset, clear, bypass if relevant
- the user edits slot params directly in the host inspector

### Expandable Local Editors

Some slots will need more room than a compact card provides. For those, the inspector should support an **expanded local editor**.

Important rule:

- expansion means a richer local editor surface
- it does **not** mean the embedded operator becomes a normal graph node

This preserves locality and keeps the main graph focused on top-level structure.

### What We Are Explicitly Not Doing

We are not turning embedded operators into:

- top-level graph nodes
- shareable graph references
- nested child-node graph objects with their own public identity

That would reintroduce the same complexity classes we are trying to remove.

## Public Model and Intended Interfaces

The intended public-facing replacement model is:

- ordinary signal ports for generic transport
- owned curated embedded slots for host-local reusable composition
- explicit outputs for graph-visible sharing of results

The breaking changes that have been made are:

- `role_bindings` have been removed from graph schema
- role-binding fields have been removed from operator descriptor/process contexts
- role-binding control-server/runtime commands have been removed
- role-binding UI snapshot/read model has been removed

The intended replacement API surface is:

### Embedded Slot Descriptor

Restore a host-declared embedded-slot concept.

Recommended descriptor shape:

- `slot_id`
- `label`
- `slot_scope`
  - `shared`
  - `per_voice`
- curated allowed operator types
- default operator type
- preferred primary output if needed internally

### Host-Owned Slot State

Store slot state on the host node itself.

Recommended host slot state should include:

- selected embedded operator type
- embedded operator params
- optional enabled/bypass state if needed

No graph-external target references should be stored.

### Runtime Model

At runtime, hosts realize slots as owned instances:

- `shared` slots become one host-owned instance per slot
- `per_voice` slots become host-owned per-voice or per-entity pools

The runtime remains responsible for lifecycle and triggering semantics.

## Replacement Model for Current Role-Binding Use Cases

The current role-binding GPU/operator family should map to the new embedded-slot model as follows.

### Shared Modulation Roles

Current shared roles become **owned shared embedded slots** on the host.

Examples:

- `Fluid`
  - `viscosity_mod`
  - `buoyancy_mod`
  - `force_mod`
- `ReactionDiffusion`
  - `feed_mod`
  - `kill_mod`
  - `diffusion_mod`
- `CellularAutomata`
  - `birth_threshold`
  - `survive_threshold`

These become host-local slots, typically backed by a curated `LFO`/`MSEG`/`RandomSH` palette.

### Per-Voice / Per-Particle / Per-Agent Roles

Current per-voice roles become **owned slot pools** on the host.

Examples:

- `Particles`
  - `envelope`
- `InstancedShapes`
  - `scale`
  - `rotation`
  - `color_mod`
- `Flocking`
  - `speed_mod`
  - `separation_mod`
  - `alignment_mod`
- `Trails`
  - `width_mod`
  - `opacity_mod`
  - `color_shift`

These should remain host-local and be realized internally as per-instance/per-agent slot pools.

### Generic External Transport

Where a host simply needs an incoming control value, use an ordinary signal port.

Examples:

- externally driving a simulation parameter with one top-level graph LFO
- feeding beat phase into a host
- using a generic macro value to scale an effect amount

### Cross-Domain Reuse of Results

When a host-local slot result should become reusable elsewhere, expose an explicit output rather than exposing the slot mechanism itself.

Examples:

- note trigger output from a synth
- envelope energy output from a particle system
- chord-change pulse from a sequencer
- active-agent density from a simulation

## Operator-by-Operator Design Rubric

Use this rubric when deciding whether a feature should be a signal port, an embedded slot, or an explicit output.

### Use a Signal Port When

- the host just needs a value
- the relationship is ordinary graph transport
- there is no special host-local ownership requirement
- the signal should remain graph-visible as normal wiring

### Use an Embedded Slot When

- the host needs reusable local operator behavior
- the mechanism belongs to the host semantically
- the user should not have to rebuild the modulator from scratch each time
- the relationship should stay local rather than becoming a separate graph object

### Use an Explicit Output When

- another operator or domain needs the result of host-local behavior
- the result is graph-meaningful
- exposing the mechanism itself would leak unnecessary internal complexity

### Short Rule

- use ports to **transport values**
- use slots to **reuse owned operator behavior**
- use outputs to **share results**

## Worked Examples

### Example 1: Synth amplitude envelope

**Preferred model:** embedded slot

Reason:

- the synth owns the note lifecycle
- the envelope is reusable code
- the user should be able to drop an `Envelope` or `MSEG` into the slot in the inspector
- the slot should stay local to the synth

### Example 2: One top-level LFO modulates a fluid parameter

**Preferred model:** ordinary signal port

Reason:

- the host just needs an incoming value
- this is ordinary graph transport
- no local slot abstraction is required

### Example 3: Particle system wants per-particle fade shape

**Preferred model:** embedded slot

Reason:

- the particle host owns spawn/lifetime semantics
- the fade logic should reuse a real `Envelope` implementation
- the host should own a per-particle slot pool internally

### Example 4: Visuals need to react to synth note onsets

**Preferred model:** explicit output from the synth

Reason:

- the visual system needs the result of host-local note behavior
- it does not need access to the synth's internal slot mechanism

## Implementation Plan

This section is the handoff-ready implementation direction for the completed move from role bindings to embedded slots.

### 1. Role bindings have been removed entirely

Role bindings have been removed from:

- graph schema and serialization
- operator contract
- scheduler/audio/GPU runtime config plumbing
- runtime API and control server mutation surfaces
- UI snapshot/read model
- inspector/UI flows
- tests and docs built around role bindings

Graphs containing `role_bindings` fail load with a clear error.

### 2. Restore an embedded-slot data model on host nodes

Add host-local slot state back to node serialization.

Recommended node state shape:

- `embedded_slots`
  - keyed by slot id
  - stores selected type and embedded param state

This state is local to the host and should not reference external nodes.

### 3. Add curated slot descriptors to host operators

Host operators that own reusable modulation should declare slots with:

- slot id
- label
- scope (`shared` / `per_voice`)
- curated allowed types
- default type

The curated set should remain intentionally small.

### 4. GPU operators have been converted to owned slots

For the current GPU family:

- convert shared roles into shared owned slots
- convert per-voice/per-entity roles into owned slot pools
- keep rendering and simulation logic intact where possible
- move modulation configuration into host-owned slot state

### 5. Keep ports and outputs where they are the right abstraction

Do not force everything into slots.

- use ordinary signal inputs for generic external control transport
- add explicit outputs where host-local behavior should become reusable elsewhere

### 6. Build the inspector-first slot editor

Implement host inspector support for:

- slot cards
- choosing a type from a curated list
- editing embedded operator params inline
- expanding into a richer local editor when needed

Do not expose embedded slot operators as top-level graph nodes.

### 7. Update demos, presets, tests, and docs

Update:

- demo graphs that currently depend on role bindings
- any factory preset surfaces for affected operators
- runtime and graph tests
- control-server/API tests
- design docs so `EMBEDDED-OPERATOR-SLOTS.md` becomes the source of truth

## Validation

The new design is correct when all of the following are true:

- the product no longer depends on role bindings
- the graph/runtime/UI story is simpler and more coherent
- users can still reuse envelopes/LFOs/MSEGs inside hosts without rewriting code
- hosts can keep local semantics local
- graph-visible sharing happens through ports and outputs, not hidden binding machinery
- the GPU operator family still preserves its useful modulation behavior after conversion

## Historical Note

This document does not claim to recover the deleted embedded-slots document verbatim. It reconstructs the strongest ideas from that direction in a form that matches the current architecture decision and incorporates what was learned from the later role-bindings approach.
