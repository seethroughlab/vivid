# ADR-0012: Per-Track Audio Signal Graph — Rewireable Routing (a DAG), not a Linear Chain

Status: accepted, **extended by [ADR-0015](ADR-0015-notes-in-the-audio-graph.md)** (2026-07-13).
This ADR models the graph as audio processors joined by stereo-summing edges and is **silent on
notes** — MIDI reaches instruments through an invisible per-track broadcast, outside the graph.
ADR-0015 makes notes a signal *in* the graph (a `MidiIn` node + typed Audio/Note edges), so note
effects become possible and note routing becomes a wire. Everything below still holds for audio.

Date: 2026-07-05

Follows: [ADR-0009](ADR-0009-two-surface-bridge-and-cpp-poc.md) (two surfaces + bridge),
[ADR-0007](ADR-0007-node-graph-contextual-deep-view.md) (node-graph contextual deep view)

Decided: a track's audio devices become a **rewireable node graph** (a directed acyclic graph with
multi-input summing), replacing the fixed linear instrument→FX chain. It is edited visually in the
device dock — the audio peer of the visuals node graph — and is the substrate for parallel chains,
instrument racks, and sends/returns.

## Context

The visuals surface is a rewireable operator node graph; the audio surface is not. Today a track's
audio is a **strictly linear chain** — instrument (VST3 / native op / sampler) → VST3 FX chain →
native FX chain → gain → master — hard-coded in `session_process()`
(`app/src/audio/vst3_host.cpp:1044-1142`). There is no routing, no parallelism, no sends, not even a
reorder API. The device dock shows this as a flat left-to-right chip row.

This blocks the things that make a track expressive: **parallel processing** (dry/wet splits,
multiband), **instrument racks** (layered/parallel instruments summed under macro control), and
**sends/returns** (shared reverb/delay buses). It also breaks the product's own symmetry — ADR-0007
established the node graph as the contextual deep view, and ADR-0009 the two best-in-class surfaces;
the audio surface should be as rewireable as the visuals one.

The audio Operator API (AO-0…AO-4) gave us native audio operators + an RT-safe runtime
(`audio_op_process`), and the engine already has the RT discipline this needs: the per-track
edit-mirror + generation-counter + `try_lock` swap, preallocated capacity, and lock-free param
queues. The visuals `VisualGraph` is a proven compile→execute graph — but it is **single-input per
node** (a chain with an active Output), so it is a shape to borrow, not a model to reuse: audio
requires genuine **multi-input summing**.

## Decision

1. **Model.** A track owns a `TrackAudioGraph`: `AudioNode`s (Instrument / Effect / Rack / Send /
   Return / Output) wrapping a processor (a `Vst3Handle*` or a native `AudioOp*`), connected by
   directed `Edge`s. **Multiple edges into a node imply a stereo sum of its inputs** — the single
   primitive from which parallel chains and racks fall out. Exactly one `Output` node per track feeds
   the unchanged gain→master→characteristics tail.

2. **RT executor.** The graph is **compiled on the UI thread** into an immutable topo-ordered plan
   (steps of {processor, input-buffer indices to sum, output-buffer index}) over a **preallocated
   buffer pool** — cycles rejected at compile, last-good plan retained. The **audio thread swaps the
   plan** via the same gen-counter + `try_lock` edit-mirror already used for the FX chain, and runs
   the steps in order each block. No allocation or blocking lock in the callback — the same
   RT-safety contract as the rest of the engine (`docs/thread-safety.md`), verified by a zero-alloc
   test.

3. **Migration is mandatory and lossless.** The existing linear chain maps to a linear graph
   (instrument → fx₀ → … → Output). A migrated linear graph must be **bit-identical** to today's
   output (the parity gate); old sessions load by synthesizing that graph; persistence version-bumps
   to store nodes+edges.

4. **UI in the device dock.** A new `AudioNodeGraph` editor (a parallel class to the visuals
   `NodeGraph`, reusing `Renderer2D` / `ui_style` widgets / the Bézier wire / the Tab chooser /
   pan-zoom) replaces the chip row in the resizable device dock. Node cards render their params
   **inline** (the same widget dispatch as visuals nodes), so no separate param zone is needed.

5. **Phased, proof-gated** (per ADR-0005): AG-0 engine + parity, AG-1 C-API/MCP, AG-2 UI, AG-3
   racks + sends/returns, AG-4 mapping-by-node-id + meters + auto-layout, AG-5 verify + merge.

## Consequences

- **Positive:** the audio surface becomes rewireable and MCP-drivable; parallel chains / racks /
  sends become natural; the two surfaces regain symmetry; native + VST3 devices compose uniformly.
- **Cost / risk:** `session_process`'s core signal path is rewritten — the #1 audit target under the
  sanitizer + zero-alloc gates. Buffer liveness on fan-out, VST3's fixed process contract inside a
  graph, and the parity requirement are the sharp edges. Mitigated by building the graph path behind
  the linear one and gating on bit-identical parity before enabling multi-input.
- **Placement trade-off:** the graph lives in the device dock (user's choice), which is short; we
  lean on the resizable dock height + pan/zoom + collapsible cards, with a "pop to the big pane"
  affordance as a later option.

## Alternatives considered

- **Node-graph *visualization* of the linear chain (+ reorder), no real routing.** Cheaper (~weeks
  vs. months) and lower risk, but does not deliver parallel chains, racks, or sends — explicitly
  rejected in favor of full routing.
- **Reuse `VisualGraph` directly.** Rejected: it is single-input and GPU/texture-bound; audio needs
  multi-input summing and an audio processor model. We mirror its compile→execute *shape* only.
- **A session-wide audio graph** (all tracks in one DAG). Deferred: per-track graphs match the "click
  a track → see its graph" model; cross-track routing is handled by Send/Return bus nodes (AG-3)
  rather than one global graph.
