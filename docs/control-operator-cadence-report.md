# Control-Operator Cadence And Graph Execution

This memo is for architecture reasoning, not a committed product spec or runtime contract. It is meant to clarify the current system and describe a cleaner conceptual model for thinking about control operators.

It assumes there is no meaningful legacy-compatibility burden. In other words, the absence of users and legacy graphs should push the design toward conceptual clarity rather than backward-compatible transition safety. Any staging discussed later should be read as an implementation tactic for reducing engineering risk, not as a requirement to preserve old semantics.

## 1. Current problem statement

Vivid currently presents one graph to the user, but executes that graph through two different runtime systems:

- the main-thread [Scheduler](/Users/jeff/Developer/vivid/docs/runtime/scheduler.md), which processes control and GPU nodes during `tick()`
- the audio-thread [AudioEngine](/Users/jeff/Developer/vivid/docs/runtime/audio_engine.md), which processes audio nodes during the device callback

That split is already explicit in [runtime architecture](/Users/jeff/Developer/vivid/docs/runtime/architecture.md) and in code:

- [scheduler.cpp](/Users/jeff/Developer/vivid/src/runtime/scheduler.cpp) skips `is_audio` nodes during `tick()`
- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp) separately extracts audio-domain nodes from the same `Graph`

So the real system is not "one uniform graph executor." It is:

- one serialized graph, [Graph](/Users/jeff/Developer/vivid/docs/runtime/graph.md)
- two execution worlds
- explicit bridge paths between them

Those bridge paths are already substantial runtime concepts:

- `ParamSnapshot`: frame/control -> audio
- `AnalysisSnapshot`: audio -> frame/control
- `generation` / `last_processed_gen`: freshness propagation inside the scheduler, including audio-originated changes injected back into scheduler state

The problem is that `VividDomain` currently carries too much meaning at once. In practice, "domain" means all of these:

- semantic category
- execution cadence
- thread ownership
- which bridge rules apply
- which side of the scheduler/audio split owns the node

That overload is why some cases feel architecturally awkward rather than merely buggy. A graph containing audio operators but no audio sink needed extra handling because audio freshness is not native to the main scheduler pass; it has to be reintroduced through snapshot/injection and generation tracking. Likewise, making something like `LFO` an "audio operator" does not merely say "this is modulation relevant to audio." It says:

- this node now belongs to the audio executor
- it advances on audio time, not frame time
- it must obey audio-thread rules
- the rest of the graph must observe it through a bridge

That is a much bigger semantic commitment than the name "audio operator" suggests.

So the current pain is not mainly about push vs pull. It is that one concept, domain, is being asked to encode both what an operator is and how it executes.

The existing runtime is therefore best used as evidence about what is hard, not as a constraint on what the target model must continue to look like. Since there are no users or legacy projects to preserve, the design question is not "how do we migrate safely?" It is "what execution model do we actually want to be true going forward?"

## 2. Proposed conceptual model for control operators

The redesign surface should stay narrow and explicit: this is about control operators, not all operators.

Some operator families are inherently fixed:

- audio-buffer producers/processors belong to the audio world
- texture/render/texture-analysis operators belong to the frame/GPU world
- operators with hard executor dependencies should stay fixed

The interesting category is scalar/spread/event-style control operators: things like `LFO`, `Envelope`, `Clock`, `Sequencer`, `Math`, `Logic`, `Smooth`, `Gate`, and similar operators that are conceptually about modulation, timing, and value transformation rather than buffer or texture ownership.

For those control operators, the clean model is to separate three axes that are currently conflated:

1. Semantic role
- `LFO`
- `Envelope`
- `Clock`
- `Sequencer`
- `Math`
- etc.

2. Execution cadence
- `frame-rate`
- `audio-rate`

3. Bridge direction
- `audio -> frame`
- `frame -> audio`

Only the middle axis is a true execution-mode choice.

- `frame-rate` and `audio-rate` are real cadences
- `sampled-from-audio` and `applied-to-audio` are not operator modes
- they are descriptions of how values cross between execution worlds

That means an `LFO` stays conceptually one thing: an `LFO`. What may vary, if the operator supports it, is its cadence:

- a frame-rate `LFO`
- an audio-rate `LFO`

That is cleaner than forcing the semantic identity to change when the timing requirements change.

This also avoids overpromising flexibility. Cadence flexibility should not apply to every control operator automatically. It should apply only to control operators that explicitly support both cadences. Some control operators are likely portable across cadences; others are probably frame-only because they depend on UI events, external input timing, file state, or semantics that are naturally frame-based.

For this memo, "audio-capable" should mean a concrete execution contract, not just a vague category. A control operator should only be considered audio-capable if it can satisfy all of these:

- no heap allocation during audio-rate execution
- no blocking, locks, or waiting on shared state
- no dependence on frame-thread-only services such as UI input, file dialogs, or GPU resources
- no dependence on non-deterministic external state that can change outside the audio handoff model
- deterministic behavior for a given input stream and internal state
- bounded CPU cost that is reasonable to run at audio cadence
- state model that still makes sense when advanced at audio rate rather than once per frame

That checklist is important for two reasons:

- it gives a real rule for classifying existing control operators
- it prevents cadence support from being treated as a UI toggle that can be bolted onto any control node

So the model is not:

- every operator can run anywhere

It is:

- fixed-domain operators remain fixed
- control operators are the only family where semantic role and cadence may be decoupled
- even there, cadence support is an explicit capability, not a universal toggle

That should be read as the intended replacement model for control operators, not as a coexistence layer over the old domain semantics. In the target design, `domain` should stop being the overloaded umbrella concept for control operators. The current implementation can still inform the design, but it should not dictate the final conceptual shape.

## 3. Rules for cadence selection

If control operators can support more than one cadence, the rules need to stay very strict.

The most important rule is:

- one control source has one real execution cadence at a time

That implies:

- one source
- one state evolution
- one phase/timing truth
- one cadence contract

Any model where one node adapts itself per consumer should be rejected, because that turns a single source into a hidden multi-source system. A graph node that behaves one way for audio consumers and another way for frame consumers stops being a stable thing. Its behavior changes when the graph changes elsewhere, which makes the graph less honest and much harder to reason about.

So cadence selection should follow a single-source rule:

- a control source must satisfy its most timing-sensitive consumer
- if any consumer truly needs audio-rate precision, the source should be audio-rate
- slower consumers can sample an audio-rate source at frame cadence
- frame-rate control feeding audio is allowed as a coarse bridge, but it should not be mistaken for audio-rate modulation

That gives the intuitive rule:

- if an `LFO` drives both audio and video, and the audio side needs precision, make the `LFO` audio-rate
- the visual side then samples the current state of that audio-rate `LFO`

This is the safe direction because `audio-rate -> frame-rate` is conceptually natural:

- one continuously advancing source
- lower-rate consumers read snapshots of it

The inverse direction is where confusion starts:

- a frame-rate source can steer audio parameters
- but it is not equivalent to an audio-rate modulator

So the cadence rules should be:

1. one source, one cadence
2. highest true timing demand wins
3. slower consumers sample faster sources
4. faster consumers should not pretend a slower source is truly faster
5. no hidden per-consumer adaptation

This also suggests an explicit conceptual distinction inside control operators:

- `audio-capable` control operators
- `frame-only` control operators

That is probably the right level of constraint. It is flexible where it helps, but does not pretend all control logic is naturally portable into the audio world.

The hardest open design question is how cadence is selected in the first place. There are three broad options:

- user-explicit selection on the node
- runtime inference from downstream connections
- hybrid behavior where the user selects a default and the runtime validates or suggests promotion

Those options have very different tradeoffs. Runtime inference is attractive because it reduces user work, but it also makes rewiring able to silently change a source's timing model and CPU cost. Pure user-explicit selection is easier to reason about, but it can make the graph feel verbose and asks users to think about cadence more often. For now, the most promising direction appears to be the hybrid:

- the source has one explicit cadence
- downstream wiring establishes whether that cadence is sufficient
- the runtime can surface a mismatch or suggest promotion
- the runtime should not silently create per-consumer behavior

That preserves the single-source rule while keeping the user in control of timing semantics.

The cost model also needs to stay visible. Promoting a control source from frame-rate to audio-rate is not a semantic-only change:

- a 60 Hz source promoted to 48 kHz runs roughly 800x more often
- promotion of one cheap `LFO` may be fine
- promotion of a cluster of control operators can become meaningful audio-thread budget

So "highest timing demand wins" should not be read as "promote freely with no consequences." It is a correctness rule, not a claim that promotion is always cheap. Any final design will need to balance timing accuracy against audio-thread cost and likely make promotion more appropriate for small, bounded operators than for heavier control graphs.

There is also an important edge case: what should happen to an audio-rate control operator when it has zero audio consumers? The single-source rule alone does not answer this. The most stable default would be:

- do not automatically demote it just because an audio sink disappears
- keep its cadence explicit and stable until the user changes it or the graph becomes invalid for that choice

That avoids a graph where disconnecting one consumer silently changes the timing behavior of the source. If the product eventually wants automatic demotion for convenience, that should be treated as an explicit policy decision with visible UX, not an invisible side effect of rewiring.

## 4. Bridge semantics between audio and frame worlds

The bridge model should be treated as intentionally asymmetric.

That asymmetry is not a flaw. It is the honest shape of the system.

### `audio -> frame`

This direction should be understood as:

- sampled
- observed
- snapshotted

The frame/GPU/control side is reading a less precise view of a continuously advancing process owned by the audio world.

Examples today already fit this model:

- RMS
- peak
- waveform
- scalar outputs or spreads derived from audio processing
- any audio-originated state that gets injected back into scheduler state

That is exactly what `AnalysisSnapshot` and scheduler-side injection already do in the current runtime.

The important point is:

- the frame world is not co-owning the audio timeline
- it is observing audio state at frame cadence

### `frame -> audio`

This direction should be understood as:

- applied
- staged
- snapshotted

The frame/control side is not "running audio." It is publishing values that the audio world consumes at safe boundaries.

That is what `ParamSnapshot` already models today:

- main-thread values are prepared
- audio reads the latest stable snapshot
- audio execution remains authoritative on its own timeline

So the right conceptual language is:

- frame can steer audio
- frame does not precisely schedule audio
- frame-originated values entering audio should be understood as coarse or staged unless they originated from an audio-rate control source

That asymmetry gives a much more honest user story:

- audio is the authoritative timeline for audio work
- frame/GPU is the authoritative timeline for visual/control tick work
- frame may react to audio
- frame may steer audio
- frame should not pretend to sample-accurately drive audio execution

This also shows that the proposed conceptual model is not alien to current Vivid. In a real sense, it is already how the runtime behaves:

- `ParamSnapshot` is already a frame -> audio bridge
- `AnalysisSnapshot` is already an audio -> frame bridge
- generation injection is already part of making audio-originated changes visible to main-thread nodes

The proposal is mostly about making this architecture legible and letting control operators participate in it more cleanly.

## 5. Likely adoption path from the current domain model

This memo assumes a clean break in the conceptual model, not an incremental migration strategy.

### Conceptual adoption

The target design should be treated as a replacement for the current overloaded meaning of `domain`, at least for control operators:

- old domain semantics are no longer the design target
- fixed-domain audio and GPU operators remain fixed in the new model
- control operators are reclassified under the new model of semantic role, cadence capability, and bridge behavior
- compatibility with the old mental model is not required

The likely long-term simplification is not:

- "make everything push"
- or "make everything pull"

It is:

- make cadence explicit
- make cross-world behavior explicit
- keep one source as one source
- stop making semantic operator identity depend on executor ownership

That is the real architectural cleanup here.

### Implementation rollout

Implementation may still be staged if that reduces engineering risk, but those stages should all serve one new model rather than support a long-lived hybrid state.

A practical rollout could still happen in phases:

1. Keep fixed-domain operators fixed
- audio-buffer operators remain audio-owned
- texture/render operators remain frame/GPU-owned
- anything with hard executor dependencies stays where it is

2. Treat control operators as the redesign surface
- this is where cadence decoupling is meaningful
- this is where the semantic/execution mismatch is most obvious today

3. Define the cadence-capability contract and bridge semantics explicitly
- semantic role
- cadence capability
- bridge behavior

4. Classify existing control operators under the new model
Likely buckets:
- `frame-only`
- `audio-capable`
- `unclear / needs design work`

A rough intuition:

- `LFO` and `Envelope` are strong candidates for audio-capable
- `Clock` and `Sequencer` are plausible but need careful semantics
- UI/input/external-state operators are likely frame-only
- some value-transform operators may be portable if they satisfy the audio-capable contract above and remain cheap enough to justify promotion

5. Preserve useful current mechanisms only where they fit the new model
- keep snapshot-based crossing if it remains the right bridge implementation
- keep audio authoritative for audio timing
- keep frame authoritative for visual/control tick timing
- do not preserve old semantics merely because the current runtime happens to implement them

So the right way to read this memo is:

- clean break in model
- optional staging in implementation
- no commitment to long-term coexistence between old and new control-operator semantics

If this direction is right, then the design goal is not "flatten the runtime into one uniform graph machine." It is almost the opposite:

- admit that there are different execution worlds
- give control operators a cleaner way to participate in them
- and expose that to users in timing language rather than thread language

## Appendix: First-pass classification of current control operators

This is a rough first pass using the audio-capable checklist from section 2. It is intentionally conservative. The goal is to identify how these operators likely fit into the clean-break target model, not to define a migration queue.

### Likely `audio-capable`

These look like the strongest candidates for optional audio-rate execution because they appear bounded, self-contained, and semantically meaningful at higher cadence:

- `alternate`
- `envelope`
- `gate`
- `lfo`
- `logic`
- `macro`
- `math`
- `modulated_gain`
- `note_duration`
- `quantizer`
- `sample_hold`
- `smooth`
- `spread_noise`
- `stack`
- `step_counter`

Short rationale:

- mostly scalar/spread/value-transform behavior
- limited dependence on external services
- semantics still make sense when advanced faster than frame rate
- likely cheap enough to consider promotion, subject to actual profiling

### Likely `frame-only`

These appear tightly tied to external state, UI/input, filesystem, networking, or observational frame-side behavior and therefore do not look like good audio-rate candidates:

- `basename`
- `fft_analysis`
- `folder_list`
- `keyboard`
- `midi_input`
- `mouse`
- `osc_in`
- `osc_out`
- `string_select`

Short rationale:

- depend on input devices, incoming messages, file/path state, or observational analysis behavior
- would either violate the audio-capable contract directly or produce confusing semantics on the audio thread

### `Unclear / needs design work`

These may be portable in principle, but need more semantic and runtime design work before they can be called audio-capable with confidence. In the clean-break framing, these are the operators whose new semantics need to be defined before they should survive in recognizable form:

- `arpeggiator`
- `chord_progression`
- `clock`
- `drum_sequencer`
- `euclidean`
- `mseg`
- `note_pattern`
- `pat_transform`
- `path_animate`
- `pattern_seq`
- `phase_to_midi`
- `random`
- `random_sh`
- `sequencer`
- `state_machine`
- `step_seq`
- `tracker`

Short rationale:

- timing semantics matter more than raw implementation safety
- several produce gates, steps, or MIDI-adjacent behavior where "audio-rate" changes the meaning, not just the precision
- some have UI/editor-oriented or pattern-editing behavior that likely belongs to the frame world even if a stripped-down runtime core could be made audio-safe

### Notes on borderline cases

- `clock` is conceptually central to this whole topic, but it is exactly the kind of operator where cadence changes the meaning of the output, so it should stay in the "unclear" bucket until its audio-rate semantics are defined explicitly.
- `random` and `random_sh` may be implementable in an audio-safe way, but they should not be treated as audio-capable until their determinism and state-advance rules are made explicit.
- MIDI-producing sequencer operators may eventually want a split model where timing/core logic is audio-capable but MIDI/event publication remains frame/applied-to-audio oriented. That is a design question, not just a classification detail.
