# Lanes Architecture

*Target architecture for unifying Vivid's one-to-many behavior across control, audio, and GPU.*

## 1. Introduction

Vivid currently has multiple mechanisms for scaling from one element to many:

- **Spreads** for variable-length control and audio collections.
- **Auto-dup** for applying mono audio operators across multichannel signals.
- **Hand-coded multi-slot operators** that manually loop across fixed collections.
- **Kernel pressure** from operators that want to create or process many elements without fitting cleanly into either scalar or spread behavior.

These are not separate product features. They are separate implementations of one missing primitive:

> how a graph value represents and processes one or many parallel elements.

This document proposes **lanes** as the replacement model.

A lane is one parallel element in a value. A scalar is just a one-lane value. A chord is a multi-lane note value. Stereo audio is audio over two lanes. A particle parameter field is control data over many lanes. A value's base payload type and its multiplicity become separate concerns.

The goal is not only runtime cleanup. The goal is to give Vivid one user-facing story for one-to-many behavior and one operator-authoring story for one-to-many behavior.

## 2. Why Vivid Needs One 1-to-N Primitive

Today Vivid answers the same architectural question in several different ways.

### Spreads

Spreads are explicit graph-visible collection values. They are useful, but they split operator behavior into separate categories:

- scalar operators
- spread-aware operators
- duplicated scalar/spread operator families

That creates both conceptual and authoring overhead.

### Auto-dup

Auto-dup solves a similar problem in audio by duplicating mono operators across multiple audio channels. It is effective, but it is:

- audio-specific
- runtime-specific
- only partially visible to the user

So it works as an implementation trick rather than as part of the graph's primary model.

### Hand-coded slot arrays

Some operators carry their own arrays of per-element state and explicitly loop over them. This is often the right local implementation, but it is a sign that the runtime does not yet provide a shared way to represent parallel elements.

### Future kernel pressure

The desire for “any operator can define a kernel and output a spread” is another symptom of the same gap. It tries to add yet another mechanism for a problem that already has too many answers.

### Why this hurts

This fragmentation costs Vivid in four ways:

1. **Users learn different rules in different domains.**
2. **Operator authors duplicate logic across scalar and collection cases.**
3. **Runtime behavior becomes harder to reason about because multiplicity is encoded differently in different subsystems.**
4. **Future architecture discussions keep rediscovering the same missing abstraction under different names.**

Vivid needs one primitive for parallel elements, not several overlapping ones.

## 3. The Conceptual Shift

The central shift is:

- stop treating multiplicity as a special feature
- treat multiplicity as a basic property of values

In the target architecture, a graph value is described by three separate things:

1. **Base payload type**
   - signal
   - audio buffer
   - string
   - texture
   - custom payload

2. **Lane set**
   - the parallel collection the value lives over
   - includes lane count and, when needed, lane identity/lifecycle semantics

3. **Operator lane behavior**
   - how an operator behaves when values have more than one lane

This is the most important separation in the proposal.

Today, Vivid often bundles these together:

- `SPREAD` means both “float payload” and “many elements”
- multichannel audio means both “audio payload” and “parallel elements”
- auto-dup mixes multiplicity handling with execution strategy

The lanes model disentangles these concerns.

## 4. Why Lanes, Not Just Width

An alternate proposal framed this architecture around **width**. That proposal gets several important things right:

- Vivid should have one multiplicity model.
- Most operators should define one-element behavior and be lifted automatically.
- Runtime execution strategy should be an implementation detail.
- Stereo, polyphony, and collection processing should stop being separate user concepts.

Those ideas are correct and are intentionally kept here.

But **width alone is too shallow** to be the full conceptual primitive.

Width answers:

- how many parallel elements are there?

Vivid also needs to answer:

- which lane is which?
- does lane 3 persist across time?
- what state belongs to lane 3?
- what happens when lane count changes?

Those questions matter in several important Vivid cases:

- polyphonic voices
- persistent simulation elements
- any stateful pointwise operator over changing collections

So the stronger model is:

- **width** is the lane count
- **lanes** are the conceptual primitive
- a **lane set** is the collection of parallel elements a value lives over

This proposal should be understood as a refinement of the width model:

### Keep

- one multiplicity model
- default pointwise auto-lifting
- runtime-selected evaluation strategy
- staged implementation
- user-facing simplicity

### Change

- use lane sets instead of raw width as the conceptual center
- make identity/lifecycle available where needed
- separate payload type from operator lane behavior
- avoid using `VIVID_PORT_SPREAD` as the long-term semantic boundary
- keep nontrivial lane reshaping explicit

## 5. Lane Sets

A **lane set** is the parallel collection over which a value exists.

Examples:

- one LFO output = one-lane signal
- a 16-note chord = 16-lane signal value
- stereo audio = audio payload over two lanes
- a 512-bin FFT result = 512-lane signal value
- a list of file paths = string payload over many lanes
- a particle parameter field = many-lane control value

### 5.1 Lane count

Lane count is the simplest property of a lane set.

- `1` means scalar
- `N > 1` means multi-lane

This is the part the width proposal captures well.

### 5.2 Positional lane sets

Some lane sets only need positional meaning:

- FFT bins
- static lists
- generic parallel control values

For these, lane `i` is just “the ith element.”

### 5.3 Identity-bearing lane sets

Some lane sets need stable identity and lifecycle:

- synth voices
- long-lived simulated elements
- systems with per-lane persistent state

For these, lane `i` is not just an index. It represents a specific evolving element. The runtime and operators may need to preserve state per lane as the lane set changes.

### 5.4 Lane lifecycle

Identity-bearing lane sets may need lifecycle semantics:

- lane creation
- lane reuse
- lane release
- lane deactivation
- lane compaction or preservation

This should not burden the common case, but the model must allow it.

### 5.5 Scope of v1

Version 1 of this design should support **one lane axis only**.

That means:

- one signal or N signals
- one audio buffer or N audio buffers
- one texture or N textures

It does **not** attempt to turn Vivid into a general multidimensional tensor system.

## 6. Values and Ports

The target model for a value is:

> **payload type + lane set**

Examples:

- `signal<float>` over one lane
- `signal<float>` over 64 lanes
- `audio-buffer` over two lanes
- `string` over N lanes
- `texture` over N lanes

### 6.1 Ports primarily describe payload compatibility

In the target architecture, ports primarily describe:

- what payload kind moves across the edge

They should not be the main place where multiplicity semantics are encoded.

That means the long-term conceptual model moves away from:

- `SPREAD` as a separate primitive

and toward:

- base payload types plus lane-bearing values

### 6.2 Payloads remain domain-native

This is important for clarity.

The lane model does **not** flatten every domain into the same raw storage shape.

Examples:

- an audio buffer is still an audio buffer
- a texture is still a texture
- a string is still a string

What lanes unify is **parallel multiplicity**, not domain-native internal structure.

So audio sample time remains inside the audio payload. Texture pixels remain inside texture payloads. Lanes describe “how many parallel payloads” exist, not the internal dimensionality of the payload itself.

## 7. Operator Lane Behaviors

Operators should declare how they behave over lanes. Four behavior classes are sufficient for the target model.

### 7.1 Pointwise

A pointwise operator processes each lane independently and preserves lane structure.

Examples:

- LFO
- envelope
- oscillator
- filter
- gain
- clamp
- add
- multiply

This should be the default behavior for most operators.

### 7.2 Structural

A structural operator creates, reshapes, reorders, or filters lanes.

Examples:

- generate N lanes
- allocate voice lanes
- tile a pattern
- repeat a value
- zip lane sets together
- flatten nested conceptual groupings
- compact active elements

This is where current spread-building and lane-shaping behavior belongs.

### 7.3 Reduction

A reduction operator combines many lanes into fewer lanes, often one.

Examples:

- mixdown
- sum
- average
- max
- select one lane
- count active lanes

This is the explicit answer to “many becomes one.”

### 7.4 Kernel / Neighborhood

A kernel operator needs cross-lane access rather than pure per-lane independence.

Examples:

- smoothing neighboring lanes
- local differences
- convolution-like lane processing
- operators that need to read the whole lane set at once

This is where the “kernel” concept belongs. It is **not** a new graph model. It is simply one operator behavior over lane sets.

## 8. Lane Propagation Rules

The lane model should use strict and predictable propagation rules.

### 8.1 Automatic scalar broadcast

This should be automatic:

- `1 -> N`

A one-lane value can broadcast to a multi-lane consumer.

This covers the most common and intuitive case:

- one control modulating many elements

### 8.2 Elementwise alignment

This should also be automatic:

- `N -> N`

Two lane-aligned values combine elementwise.

### 8.3 Non-scalar mismatches are explicit

This should **not** be automatic:

- `3 -> 8`
- `5 -> 16`
- `N -> M` where both are non-scalar and different

These cases should require an explicit structural operator such as:

- `Repeat`
- `Tile`
- `Zip`
- `Resample`
- `Partition`
- `Flatten`

This is a deliberate simplification over current spread wrapping behavior. It makes larger graphs easier to reason about because nontrivial reshaping becomes visible.

### 8.4 Shape-preserving default

Unless an operator explicitly declares structural or reduction behavior, it should preserve lane structure.

This is one of the core simplifying rules of the whole architecture.

### 8.5 Lane-set provenance

Lane propagation should carry not only lane count, but also **lane-set provenance**.

This means the compiler/runtime should conceptually track:

- which lane set a value belongs to
- whether two multi-lane inputs are lanes of the **same** set or merely have the same count
- whether a node preserves, reshapes, reduces, or creates a new lane set

The target model should therefore treat lane-set identity as something that flows through the compiled graph alongside payload type and cadence classification.

Conceptually, every lane-bearing edge value should carry:

- a base payload type
- a lane count
- a `lane_set_id`

Where:

- `lane_set_id` identifies the source lane set provenance
- pointwise operators preserve the same `lane_set_id`
- structural operators may emit a new `lane_set_id`
- reductions consume one or more lane sets and emit a smaller or scalar lane set
- explicit reshapers define how output lanes relate to input lane sets

This is especially important for identity-bearing lane sets. Two values with count `8` are not automatically compatible just because they are both width 8. They are trivially aligned only if they are lanes of the same conceptual collection or have been explicitly reshaped to align.

In implementation terms, this means the graph compiler will eventually need a lane-set propagation pass, analogous to type and cadence propagation:

- source nodes either produce one-lane values or allocate a new lane set
- pointwise edges preserve lane-set provenance
- broadcast from `1 -> N` does not create a new lane set; it adapts a scalar input to an existing destination lane set
- structural nodes are the primary places where lane-set provenance changes

This is the missing link between “lane identity matters” and “how does the runtime know which lanes belong together?” The answer is: the compiled graph must explicitly track lane-set provenance, not infer it ad hoc from array length.

### 8.6 Worked example: `poly_voice_allocator -> wavetable_osc -> voice_mixer`

The clearest way to make lane-set provenance concrete is to walk a real chain from `vivid-wavetable`.

Consider this conceptual graph:

```text
MIDI / note-gate input
    -> PolyVoiceAllocator
    -> WavetableOsc
    -> VoiceMixer
    -> audio_out
```

#### Step 1: `PolyVoiceAllocator` creates a voice lane set

`PolyVoiceAllocator` is a **structural** operator. It consumes either:

- scalar MIDI events
- or note/gate/velocity control inputs

and emits lane-valued control outputs:

- notes
- velocities
- gates
- frequencies

In the lane model, those outputs do not merely have `lane_count = N`. They also carry a shared provenance:

- `lane_set_id = voice_set_42`

Conceptually:

| Output | Payload | Lane count | Lane set |
|--------|---------|------------|----------|
| `notes` | signal | 3 | `voice_set_42` |
| `velocities` | signal | 3 | `voice_set_42` |
| `gates` | signal | 3 | `voice_set_42` |
| `frequencies` | signal | 3 | `voice_set_42` |

And the active lanes might be:

| `lane_index` | `lane_id` | note | meaning |
|--------------|-----------|------|---------|
| 0 | 1001 | 60 | first active voice |
| 1 | 1002 | 64 | second active voice |
| 2 | 1003 | 67 | third active voice |

At this point:

- `lane_count = 3`
- `lane_set_id = voice_set_42`
- lane identities are `1001`, `1002`, `1003`

This is the first place where lane identity exists. The allocator owns the semantic meaning of the voice set; the runtime owns how those `lane_id` values are tracked and stored.

#### Step 2: `WavetableOsc` preserves the same lane set

`WavetableOsc` is a **pointwise** operator over the incoming voice lane set.

It receives:

- `frequencies` over `voice_set_42`
- `gates` over `voice_set_42`
- optional modulation values, which may be:
  - scalar and broadcast into `voice_set_42`
  - already lane-aligned to `voice_set_42`

Because it is pointwise, it does **not** create a new lane set. It preserves provenance:

| Output | Payload | Lane count | Lane set |
|--------|---------|------------|----------|
| `audio` | audio-buffer | 3 | `voice_set_42` |

For each callback/invocation:

- `lane_index` tells the runtime which active voice position is currently being processed
- `lane_id` tells the operator which persistent voice this is

So the operator’s oscillator state should be keyed conceptually like:

| `lane_id` | phase | current_freq | target_freq | was_gated |
|-----------|-------|--------------|-------------|-----------|
| 1001 | ... | ... | ... | ... |
| 1002 | ... | ... | ... | ... |
| 1003 | ... | ... | ... | ... |

If a note is released and the allocator later compacts the active set, the runtime may change positional indices:

| `lane_index` | `lane_id` |
|--------------|-----------|
| 0 | 1001 |
| 1 | 1003 |

But `WavetableOsc` state stays correct because it is keyed by `lane_id`, not by transient position.

#### Step 3: scalar modulation broadcasts into the voice lane set

Suppose `WavetableOsc` also receives:

- a scalar `wavetable_position` modulation source
- a scalar `gain`

Those inputs have:

- `lane_count = 1`
- no voice lane-set provenance of their own

When combined with `voice_set_42`, they broadcast into the destination lane set:

- they do not create a new `lane_set_id`
- they adapt to `voice_set_42`

So inside `WavetableOsc`, these become effectively:

- one value per active voice lane
- all still aligned to `voice_set_42`

This is why scalar broadcast should be implicit, but non-scalar reshaping should remain explicit.

#### Step 4: `VoiceMixer` reduces the lane set

`VoiceMixer` is a **reduction** operator.

It consumes:

- audio over `voice_set_42`
- optional per-voice envelope/pan/velocity values also over `voice_set_42`

And it reduces those lanes to:

- stereo output

Conceptually:

| Input | Payload | Lane count | Lane set |
|-------|---------|------------|----------|
| `voice_audio` | audio-buffer | 3 | `voice_set_42` |
| `voice_pan` | signal | 3 | `voice_set_42` |
| `voice_env` | signal | 3 | `voice_set_42` |

becomes:

| Output | Payload | Lane count | Lane set |
|--------|---------|------------|----------|
| `stereo_out` | audio-buffer | 2 | output lane set or fixed stereo shape |

The important point is that `VoiceMixer` is the first node in this chain that intentionally **consumes** `voice_set_42` rather than preserving it.

That means:

- `PolyVoiceAllocator` creates the voice lane set
- `WavetableOsc` preserves it
- `VoiceMixer` reduces it away

This is exactly the sort of provenance the compiler/runtime needs to understand.

#### Step 5: what happens when the active voice set changes

Assume a fourth note arrives. `PolyVoiceAllocator` may extend the lane set:

| `lane_index` | `lane_id` | note |
|--------------|-----------|------|
| 0 | 1001 | 60 |
| 1 | 1002 | 64 |
| 2 | 1003 | 67 |
| 3 | 1004 | 71 |

The runtime-visible changes are:

- `lane_count` changes from `3` to `4`
- `lane_set_id` remains `voice_set_42`
- a new `lane_id = 1004` appears

`WavetableOsc` should therefore:

- preserve existing state for `1001`, `1002`, `1003`
- initialize fresh per-lane state for `1004`

Now assume note `64` is released and the allocator compacts:

| `lane_index` | `lane_id` | note |
|--------------|-----------|------|
| 0 | 1001 | 60 |
| 1 | 1003 | 67 |
| 2 | 1004 | 71 |

What changes:

- `lane_index` changes for surviving lanes after compaction
- `lane_count` becomes `3`

What does **not** change:

- `lane_set_id` remains `voice_set_42`
- surviving lanes keep their `lane_id`
- `WavetableOsc` state for `1003` and `1004` survives correctly

This is why the runtime cannot key persistent operator state by positional slot alone.

#### Step 6: compiler implications of the worked example

This example makes the compiler/runtime responsibilities concrete:

- `PolyVoiceAllocator` is a structural node that allocates `voice_set_42`
- all pointwise downstream consumers preserve `voice_set_42`
- scalar broadcasts adapt into `voice_set_42` without replacing it
- `VoiceMixer` is a reduction node that consumes `voice_set_42`

So the compiler must be able to represent, at minimum:

- lane-bearing outputs with a `lane_set_id`
- whether a node preserves or transforms the incoming lane set
- where lane identity originates
- where it is intentionally reduced away

That is the practical meaning of lane-set provenance in a real patch.

### 8.7 Lane identity quick reference

The lane model uses three related but distinct concepts:

| Term | Meaning | Stability | Example |
|------|---------|-----------|---------|
| `lane_set_id` | Identifies the conceptual collection a value belongs to | Stable until a structural node creates or replaces the collection | `voice_set_42` for one active voice collection |
| `lane_id` | Identifies one persistent element within an identity-bearing lane set | Stable across reordering and compaction while that element is alive | voice `1003` keeping its oscillator state |
| `lane_index` | Current positional slot in the active lane array | May change after compaction, sorting, filtering, or reshaping | voice `1003` moving from index `2` to index `1` |

The intended rule is:

- `lane_set_id` answers “which collection is this value part of?”
- `lane_id` answers “which persistent element is this?”
- `lane_index` answers “where is that element positioned right now?”

Only `lane_index` should be treated as a cheap positional convenience. Persistent operator state should never rely on `lane_index` remaining stable.

### 8.8 Worked example: `fft_analysis -> instanced_shapes`

Not every lane set needs stable identity. The clearest positional-only example is FFT-driven visual instancing.

Consider this conceptual graph:

```text
audio_in
    -> FFTAnalysis
    -> Normalize / ColorMap / Scale
    -> InstancedShapes
    -> video_out
```

#### Step 1: `FFTAnalysis` creates a positional lane set

`FFTAnalysis` is a **structural** operator. It converts an audio input into a magnitude spectrum.

Conceptually, it emits:

| Output | Payload | Lane count | Lane set |
|--------|---------|------------|----------|
| `magnitudes` | signal | 512 | `fft_bins_17` |

Here:

- `lane_set_id = fft_bins_17`
- `lane_count = 512`
- each `lane_index` corresponds to a frequency-bin position

Unlike voices, these lanes do not need long-lived identity. Bin 37 is meaningful because it is position 37 in the spectrum, not because it is a persistent object with independent lifecycle.

So for this lane set:

- `lane_set_id` matters
- `lane_index` matters
- `lane_id` can be elided or treated as trivially derived from positional bin identity

#### Step 2: pointwise visual shaping preserves the lane set

Suppose the graph then applies:

- normalization
- color mapping
- size scaling

These are all **pointwise** operators. They preserve the same lane-set provenance:

| Output | Payload | Lane count | Lane set |
|--------|---------|------------|----------|
| `sizes` | signal | 512 | `fft_bins_17` |
| `colors` | signal or custom color payload | 512 | `fft_bins_17` |

This means every downstream operator can rely on:

- lane 0 in `sizes`
- lane 0 in `colors`

referring to the same FFT bin because both values still belong to `fft_bins_17`.

#### Step 3: scalar controls broadcast into the FFT lane set

If `InstancedShapes` also receives:

- one scalar `base_radius`
- one scalar `rotation_speed`

then those are one-lane values that broadcast into `fft_bins_17`:

- they do not create a new lane set
- they adapt to the destination lane set

This is the same broadcast rule as the voice example, but here there is no identity-bearing lifecycle pressure.

#### Step 4: `InstancedShapes` consumes the positional lane set directly

`InstancedShapes` is conceptually a **pointwise GPU operator** over the FFT lane set:

- lane 0 generates or modulates instance 0
- lane 1 generates or modulates instance 1
- ...
- lane 511 generates or modulates instance 511

The operator can preserve positional semantics entirely:

- `lane_index` maps directly to instance position or array slot
- no separate persistent `lane_id` mechanism is needed unless the operator introduces per-instance temporal state

This is the key contrast with the voice example:

- FFT bins are naturally positional
- synth voices are naturally identity-bearing

The lane architecture needs to support both, but it should not force the heavier identity machinery on positional lane sets that do not need it.

#### Step 5: compiler implications of the FFT example

This example clarifies that lane-set provenance is useful even when identity is trivial.

The compiler/runtime still benefits from tracking:

- `lane_set_id = fft_bins_17`
- `lane_count = 512`
- preservation through pointwise operators
- reduction or reshaping if a later node changes the collection

But unlike the voice case, there is no need for allocator-managed lifecycle or persistent lane-state storage keyed by `lane_id`.

That distinction is why the architecture should separate:

- positional lane sets
- identity-bearing lane sets

under one common lane-set model rather than forcing every case into the same heavy-weight semantics.

## 9. Runtime Execution Model

The user should see one concept: lanes.

The runtime should be free to choose the best evaluation strategy for that concept.

### 9.1 Scalar fast path

When lane count is `1`, runtime behavior should be equivalent to today’s scalar fast path.

This keeps the common case cheap.

### 9.2 Instance duplication

For some operators and some lane counts, the runtime may evaluate lanes by duplicating operator instances.

This is closest to today’s auto-dup behavior and has one strong property:

- independent operator state per lane comes “for free” because each lane has its own instance

### 9.3 Loop-based evaluation

For larger lane counts, instance duplication may be too heavy. The runtime may instead:

- keep one operator instance or a reduced number of instances
- iterate across lanes
- manage per-lane state explicitly

This is conceptually similar to how some current multi-slot operators work by hand, but generalized by the runtime.

### 9.4 Future GPU-backed evaluation

For very large lane counts, especially in GPU-facing scenarios, the runtime may choose GPU-backed evaluation strategies.

This should remain an implementation detail. The graph model should not change.

### 9.5 Semantic invariants

This is the most important execution-rule constraint:

> Changing runtime strategy must not change semantics.

If the runtime switches from duplicated instances to loop-based evaluation, the user-facing behavior must remain the same. This implies a stronger operator contract than today’s scattered mechanisms.

## 10. Runtime Representation in Vivid

The current runtime is already close enough to make the target direction concrete.

Relevant current components include:

- `CompiledNode`
- `CompiledEdge`
- `FrameExecutor`
- `AudioExecutor`
- `CadenceBridge`

The target architecture would conceptually change each of these.

### 10.1 Compiled graph

Today, `CompiledNode` state is split across:

- scalar arrays (`input_values`, `output_values`)
- spread buffers (`input_spreads`, `output_spreads`)
- string arrays
- audio buffers
- custom payload structures

The target direction is to model these as lane-bearing values instead of separate scalar-vs-spread categories.

This does **not** mean everything must share one identical C++ container immediately. It means multiplicity should stop being represented as a special port-type fork.

Concretely, the compiled graph will eventually need to represent, per relevant port or edge:

- payload kind
- lane count
- lane provenance (`lane_set_id`)
- lane behavior expectations at the operator boundary

That does not require an immediate “one container type everywhere” rewrite, but it does require that the compiler stop treating spread buffers, audio channel multiplicity, and future kernel-like multiplicity as unrelated cases.

### 10.2 Frame execution

Today, frame execution propagates:

- scalar values
- spread values
- string values
- textures
- custom payloads

largely through different code paths.

The target direction is:

- lane-aware propagation
- pointwise lifting as the default
- structural and reduction behavior handled explicitly

Frame execution should stop treating “spread” as the one special collection case.

### 10.3 Audio execution

Today, audio execution contains a special mechanism for mono operators in multichannel chains: auto-dup.

The target direction is:

- audio payloads can also live over lane sets
- pointwise audio operators lift over lanes
- stereo, polyphony, and similar multiplicity patterns stop being a separate auto-dup concept

Audio sample-time structure remains distinct from lane multiplicity.

### 10.4 Cadence bridge

Today, cross-cadence transport special-cases:

- scalar snapshots
- spread snapshots with hard limits
- special handling for audio-to-frame and frame-to-audio propagation

The target direction is:

- lane-bearing snapshot transport
- explicit scaling constraints for larger lane counts
- the same conceptual model crossing both cadences

The bridge may still need size-dependent transport strategies, but that should not affect the graph model.

### 10.5 Operator state

The runtime must support both:

- stateless or purely positional lane evaluation
- stateful lane evaluation where identity matters

That implies the target architecture needs a clean place for:

- per-lane persistent state
- lane activation/deactivation
- strategy-independent semantics for stateful pointwise operators

The target ownership model should be:

- **operators own state schemas**
- **the runtime owns per-lane state storage and indexing**

In other words, a stateful pointwise operator should conceptually define:

- what per-lane state it needs
- how that state is initialized, updated, and destroyed

But the runtime should control:

- how lane IDs are assigned
- how lane IDs map to live storage slots
- how lane state survives compaction, deactivation, or runtime strategy changes

The important reason for this split is semantic stability. If instance-duplication and loop-based execution are both valid runtime strategies, per-lane state cannot be implicit inside raw duplicated operator instances alone. There needs to be a strategy-independent notion of “state for lane X”.

The target architecture should therefore provide a conceptual lane-state service:

- pointwise operators receive the current `lane_id`
- the runtime exposes lane-local state storage keyed by that `lane_id`
- the operator accesses its per-lane state through a runtime-mediated handle rather than assuming “lane index equals storage slot forever”

Conceptually, the API should look like:

- `lane_index` — current positional index in the active lane set for this invocation
- `lane_count` — active lane count
- `lane_id` — stable identity token for this lane when identity matters
- `lane_state<T>(lane_id)` — access operator-owned persistent state for that lane

The exact symbol names should remain open for implementation, but the semantics should be fixed:

- `lane_index` is positional and may change after compaction or reshaping
- `lane_id` is the stable key for identity-bearing lane sets
- runtime storage is keyed by `lane_id`, not by transient positional index

This also defines what happens when lanes are reordered or compacted:

- reordering changes `lane_index`, not `lane_id`
- compaction removes inactive positional gaps, but surviving lanes keep their `lane_id`
- releasing a lane marks its state as eligible for teardown
- structural operators that intentionally construct a new lane set also intentionally create new `lane_id` values unless they explicitly preserve upstream identity

For purely positional lane sets, the runtime can optimize away most of this machinery. But for identity-bearing lane sets, especially voices and persistent simulation elements, this contract needs to be explicit in the architecture.

## 11. Target Operator API

The target operator API should make the lane model concrete without burdening the common case.

### 11.1 Descriptor-level concepts

An operator descriptor conceptually needs to declare:

- base payload compatibility
- lane behavior
- lane capability constraints
- optional stateful-lane contract when relevant

Examples of capability constraints:

- scalar-only
- pointwise-liftable
- structural lane generator
- reduction
- kernel / full-lane access

### 11.2 Common-case authoring model

The common case should be simple:

- most operators are pointwise
- most operators define one-lane behavior
- runtime lifts them automatically when inputs are multi-lane

This is one of the main reasons the lanes approach is worth doing.

### 11.3 Pointwise operator context

A pointwise operator may conceptually need:

- current lane value view
- current lane index
- lane count
- optional lane identity token for stateful cases

But this should stay lightweight. Most operators should not need full-lane access.

For stateful pointwise operators, the target API should additionally distinguish:

- **positional access**
  - “which lane position am I processing right now?”
- **identity access**
  - “which persistent lane is this?”

That distinction is what makes voice-preserving or simulation-preserving behavior possible without forcing every operator to manage its own slot maps.

### 11.4 Structural / reduction / kernel contexts

These operators may need richer access:

- full lane-set input
- output lane-set construction
- neighborhood reads
- explicit cardinality changes

This is where full collection access belongs.

### 11.5 Lane identity access

Operators that need persistent per-lane state should have a lane identity mechanism available.

This should exist for:

- oscillators with voice state
- envelopes with release tails
- simulation-like persistent operators

But it should not complicate the default pointwise authoring experience for ordinary stateless or per-lane-local operators.

The intended rule is:

- stateless pointwise operators usually care only about `lane_index`
- stateful pointwise operators with identity-bearing lane sets care about `lane_id`
- structural operators are the primary place where new lane identities are created, preserved, remapped, or discarded

## 12. Cross-Cadence Behavior

Lanes must work coherently across the current two-cadence runtime:

- frame cadence
- audio cadence

### 12.1 Frame-to-audio

Lane-valued control data sent into audio should preserve lane semantics.

Examples:

- lane-valued frequencies feeding lane-valued oscillators
- lane-valued modulation entering polyphonic audio paths

The bridge should preserve:

- lane count
- lane order
- lane identity where relevant to the receiving operator contract

### 12.2 Audio-to-frame

Lane-valued audio-derived data sent back to frame should also preserve lane semantics.

Examples:

- lane-valued per-voice analysis
- FFT-like outputs
- lane-valued metering or control signals

### 12.3 Bridge scaling constraints

Current snapshot infrastructure has hard limits and special cases. The target architecture should explicitly acknowledge that not all lane counts are equally cheap to transport across cadences.

So the model should distinguish:

- conceptual lane correctness
- runtime transport cost

Lane semantics should remain consistent even when the runtime chooses different transport/storage backends for small and large lane counts.

### 12.4 No new cadence model is required

The lane proposal does **not** replace the current dual-cadence runtime model. It sits orthogonally to it.

Cadence answers:

- when does this node execute?

Lanes answer:

- how many parallel elements does this value/operator work over?

Keeping those concerns separate is important.

## 13. What Happens to Existing Concepts

### 13.1 Spreads

Spreads stop being the long-term conceptual primitive.

In the target model:

- a spread is just a lane-valued signal or string collection

For transition purposes, spread ports may still exist, but the architecture should stop treating them as the foundation.

### 13.2 Auto-dup

Auto-dup stops being a separate graph-visible concept.

Its useful behavior survives as:

- pointwise lane lifting in audio
- one possible runtime execution strategy

### 13.3 Spread operators

Operators whose only reason for existing is “scalar operator but spread-aware” should eventually disappear or collapse into their ordinary pointwise equivalents.

Examples in spirit:

- spread-specific versions of ordinary modulation operators

### 13.4 Kernels

Kernels should not become a new top-level graph abstraction.

They should be:

- an operator lane behavior

That keeps the graph model unified instead of introducing a third one-to-many mechanism.

## 14. `vivid-wavetable` as the Proof Case

`../vivid-wavetable` is one of the best possible stress tests for this architecture because it already demonstrates both the need for unification and the limits of a shallow width-only model.

### 14.1 What the package does today

[poly_voice_allocator.cpp](/Users/jeff/Developer/vivid-wavetable/src/poly_voice_allocator.cpp) outputs:

- notes as spreads
- velocities as spreads
- gates as spreads
- frequencies as spreads

[wavetable_osc.cpp](/Users/jeff/Developer/vivid-wavetable/src/wavetable_osc.cpp) then:

- reads those spreads by slot
- uses per-voice audio channels for generated audio
- manually resolves modulation channels for each voice
- maintains per-voice oscillator state arrays

[voice_mixer.cpp](/Users/jeff/Developer/vivid-wavetable/src/voice_mixer.cpp):

- accepts N-channel per-voice audio
- reads per-voice control spreads
- manually mixes voices down to stereo

This means the package currently uses:

- spreads for voice control data
- channels for voice audio
- manual operator logic to reconcile the two

### 14.2 What this proves

The package proves two things at once:

1. spreads alone are not enough
2. count alone is not enough either

Why spreads are not enough:

- the package has to bridge between spread-based control and channel-based audio manually

Why width/count is not enough:

- voices are stateful
- voice stealing, release, glide, and gate transitions all depend on stable identity/lifecycle

This is why the lane-set model is stronger than both the current spread model and a pure width-count model.

### 14.3 Target reinterpretation under lanes

#### PolyVoiceAllocator

This becomes:

- a **structural lane allocator**
- responsible for creating and managing a voice lane set
- outputs lane-valued note, gate, velocity, and frequency data over that lane set

#### WavetableOsc and SubOsc

These become:

- **pointwise lane-aware audio operators**
- each lane has its own oscillator state
- modulation arrives lane-aligned instead of as a separate spread-vs-channel reconciliation problem

#### VoiceMixer

This becomes:

- a **reduction** from voice lanes to stereo output

This is conceptually much cleaner than “special operator that understands channelized voice audio plus spread control inputs.”

## 15. Phased Implementation Plan: Vivid

This is a clean conceptual break with staged implementation.

### Phase 1: Architecture and vocabulary

- document lane sets, lane behaviors, and lane propagation rules
- make the clean-break target explicit in docs
- define the target relationship between payload type, lanes, and operator behavior

### Phase 2: Internal runtime groundwork

- separate payload type from multiplicity in internal runtime structures
- add internal operator lane behavior metadata
- prepare compiled graph and executor structures for lane-aware values
- avoid freezing the final public ABI yet

### Phase 3: Control-domain adoption

- replace spread-first reasoning with lane-valued control data
- introduce structural and reduction primitives in the new vocabulary
- collapse duplicated scalar/spread operator families where possible

### Phase 4: Audio-domain adoption

- replace auto-dup as a separate special concept with lane lifting
- preserve the distinction between sample-time structure and lane multiplicity
- support stateful pointwise operators over lane sets

### Phase 5: Kernel behavior support

- add neighborhood/full-lane operator behavior support
- keep kernels as a behavior class, not a new graph model

### Phase 6: UI and authoring

- add lightweight lane-count indicators
- update operator authoring docs/templates
- make structural and reduction nodes visibly legible in the graph

## 16. Phased Implementation Plan: `vivid-wavetable`

### Phase 1: Voice lane-set model

- define voices as a lane set with stable identity
- define allocator-controlled lifecycle semantics

### Phase 2: Allocator conversion

- reinterpret `poly_voice_allocator` as structural lane allocation
- move from spread-slot vocabulary to lane-set vocabulary

### Phase 3: Oscillator conversion

- convert `wavetable_osc` and `sub_osc` into lane-aware pointwise operators
- keep per-lane oscillator state explicit
- eliminate manual spread-slot vs channel reconciliation

### Phase 4: Mixdown conversion

- convert `voice_mixer` into a reduction from voice lanes to stereo

### Phase 5: Validation

Preserve and validate:

- voice stealing
- gate transitions
- release tails
- glide / portamento
- modulation semantics
- active-voice normalization
- stereo spread / panning behavior

## 17. Acceptance Scenarios

The architecture should support all of these cleanly:

- scalar operator lifted automatically to N lanes
- one-lane control broadcasting to many-lane consumers
- N-lane pointwise processing across ordinary operators
- explicit reshape from `3 -> 8`
- explicit reduction from many lanes to one
- kernel operator reading neighboring or full-lane data
- stereo audio without a separate auto-dup concept
- multivoice audio over lane sets
- FFT bins driving visuals using the same multiplicity model
- particle or instance systems driven by lane-valued control data
- `vivid-wavetable` voice allocation and oscillator state under stable lane identity

## 18. Conclusion

Lanes are the right long-term architecture for Vivid.

They replace multiple overlapping answers to “one to many” with one coherent primitive. They preserve the good part of the width model — one multiplicity concept, automatic lifting, hidden runtime strategy — while making the model strong enough for the cases Vivid actually needs to support.

Most importantly, lanes fit what Vivid is trying to be:

- a compositional environment
- a cross-domain environment
- an environment where common creative patterns feel native rather than bolted on

Spreads, auto-dup, and future kernel pressure are all evidence of the same missing idea. Lane sets provide that idea in a form that is simpler for users, better for operator authors, and more coherent for the runtime.
