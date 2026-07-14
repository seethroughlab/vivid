# ADR-0015: Notes Are a Signal in the Audio Graph — a MidiIn Node and Typed Note Edges

Status: accepted

Date: 2026-07-13

Amends: [ADR-0012](ADR-0012-per-track-audio-graph.md) (per-track audio signal graph), which is
**silent on notes** — it models the graph as audio processors joined by stereo-summing edges and
never says how MIDI reaches them.

Decided: the per-track note stream becomes an **explicit `MidiIn` node**, graph edges become
**typed** (`Audio` or `Note`), and **note effects become nodes**. A graph with no note edges behaves
exactly as it does today — that equivalence is the migration gate.

## Context

Today every note — clip playback, live hardware MIDI, musical typing, an MCP `note_on`, the
piano-roll's preview — is assembled into **one per-track vector, `Track::nev`**
(`app/src/audio/vst3_host.cpp:1842-1859`), and then **broadcast to every source node in the graph**,
filtered only by that node's key range (`run_track_graph`, `vst3_host.cpp:526-560`).

So the note bus exists, but it is **invisible and unaddressable**:

- **The graph cannot see it.** `AudioGraphEdge` is an untyped `{from_id, to_id}` and `CompiledStep`
  carries only audio-buffer indices (`app/src/audio/audio_graph.h:47,52`). A user looking at a track's
  graph sees instruments with no inputs, fed by nothing. That contradicts the product's own
  "see every step" principle: the most important signal in a MIDI track is the one you can't see.
- **Note effects are impossible.** There is no way for anything to *transform* notes between their
  source and an instrument, so an arpeggiator, a chord generator, a transposer, or a humanizer cannot
  exist — in any format. The host drops CLAP plugin-emitted events at a one-line stub
  (`app/src/audio/clap_host.h:78` — `return true; // ignore plugin-emitted events`) and never
  activates a VST3 event **output** bus at all (`data.outputEvents` is assigned nowhere).
- **Routing is a special case pretending to be a feature.** The one thing you *can* do — a key split —
  exists as a hidden per-node attribute (`GNodeBind::key_lo/key_hi`), not as routing. "Which notes go
  to which instrument" is exactly the question a graph should answer with a wire.

We also just shipped a plugin classifier (A1) that correctly identifies CLAP `note-effect` plugins —
and then has to *disable* them in the chooser, because spawning one today binds it as an audio effect
that receives zero notes and writes silence over the chain. The catalog can name a capability the
engine cannot host.

## Decision

1. **`MidiIn` is a node.** The per-track note stream (clips + live + typing + MCP + preview — exactly
   what fills `Track::nev` today) is produced by an explicit source node, so it is visible, wirable,
   and inspectable like everything else in the graph.

2. **Edges are typed: `Audio` or `Note`.** A node declares which input kinds it accepts. Note edges
   **fan out** (one note stream can feed several nodes); multiple note edges into one node **merge**
   — the note peer of the stereo sum that ADR-0012 chose as its single audio primitive.

3. **Key range becomes a note-edge filter.** The same control, moved from a hidden node attribute onto
   the wire that carries the notes: a key split is then simply two filtered note edges, and the model
   generalizes (velocity, channel, and note-range filters are all the same shape). The node-level API
   and persisted field are retained as back-compat aliases.

4. **Note effects are nodes** — native note operators, CLAP note-effects, and note-emitting VST3
   plugins alike. This requires the host to *read* plugin note output, which it currently discards.

5. **Migration is lossless and bit-identical.** A graph with **no note edges** behaves precisely as
   today: an implicit `MidiIn` broadcast to every source, key-range filtered. Old projects load by
   synthesizing exactly that. **Bit-identical output on the existing demo projects is the gate** — the
   same discipline ADR-0012 imposed on the linear→graph migration, for the same reason: this touches
   the RT render path.

## Consequences

- **Positive:** note effects (arpeggiator, chord, transpose, humanize) become possible at all; the
  hidden note bus becomes an honest, inspectable part of the graph; note routing generalizes
  (key-splits stop being a bespoke feature); the CLAP note-effects we already classify become
  hostable; and the audio graph gains the thing the visuals graph already has — every signal it
  carries is visible.
- **Cost / risk:** this is the second change to the RT render path after ADR-0012, and the sharp edges
  are the same. Two signal classes mean a **preallocated note-buffer pool** (no allocation on the
  audio thread — the existing zero-alloc test extends to cover it), a topo sort that spans both edge
  kinds (a note effect must run before the instrument it feeds), and a compile step that assigns two
  kinds of buffer. Mitigated by landing the typed-edge core behind a bit-identical parity gate before
  any note effect exists.
- **Deliberately not fixed here** (so it isn't silently inherited): **MIDI channel is discarded at
  ingress** (`platform/midi_input.mm` parses only note-on/off; `emit_vst3` hard-zeroes the channel), so
  MPE *input* remains impossible; and CLAP receives no note expression, so painted MPE stays
  VST3-only. Typed note edges make both *addressable* later rather than fixing them now.

## Alternatives considered

- **A `MidiIn` node with no note edges** (a node that merely represents the existing broadcast).
  Rejected: it changes nothing, and a node that doesn't route anything is a decoy — worse than no node
  at all, because it implies a model the engine doesn't have.
- **Keep the implicit broadcast; add note effects as a per-track pre-chain** (a fixed "MIDI FX" slot
  before the instrument, as some DAWs do). Cheaper, and it would deliver arpeggiators. Rejected: it
  re-introduces exactly the fixed linear chain ADR-0012 removed on the audio side, and it can't express
  "this arp feeds only that instrument" on a track with several sources.
- **A session-wide note graph** (cross-track note routing). Deferred, consistent with ADR-0012's
  per-track scope; cross-track note sends can follow the same Send/Return shape later.
