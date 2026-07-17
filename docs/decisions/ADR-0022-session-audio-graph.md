# ADR-0022: The Session Audio Graph — One Rewireable DAG for the Whole Session

Status: **accepted** — the ADR-0017 dependency cleared (PR #31, `14306ec2`) and implementation has
begun. **P0 (`EdgeKind::Control` in the pure core) is landed**; P1–P4 are not started. See "As
built" below for what the code taught us that this ADR did not know.

Date: 2026-07-14 (accepted 2026-07-17)

Supersedes the deferral in [ADR-0012](ADR-0012-per-track-audio-graph.md) ("a session-wide
audio graph … Deferred: per-track graphs match the click-a-track model; cross-track routing
is handled by Send/Return bus nodes"). Extends [ADR-0015](ADR-0015-notes-in-the-audio-graph.md)
(notes as a signal) and [ADR-0002](ADR-0002-session-view-first.md) (session-view-first).

Follows: the undo/redo `EditGateway` ([ADR-0017](ADR-0017-every-edit-is-reversible.md)) — **shipped
(PR #31, `14306ec2`), so this dependency is now cleared.**

Decided: the per-track audio graphs become **one session-wide DAG** in a shared coordinate
space. A **track is defined by a Track-Out node**; the **master is a node**; **modulation is
a first-class signal** (`EdgeKind::Control`); and **clips + generators are first-class
nodes**. Per-track behavior is preserved as the bit-identical migration case.

> ### As built — P0 (2026-07-17, `EdgeKind::Control` in the pure core)
>
> P0 landed as specified: a third `EdgeKind`, control ports mirroring ADR-0015's note ports, a
> `control_out_buf` / `control_in[]` pair on `CompiledStep`, and one control buffer per emitting
> node — `control_buf_count == 0` for any graph without a modulator, so control costs nothing
> until used. `CompiledAudioGraph::run()` is untouched: the core resolves buffer indices and
> nothing else. Three decisions the ADR did not anticipate, each forced by the code:
>
> - **`connect_control()` is a separate entry point.** `connect(from, to, kind)` has nowhere to put
>   a param selector, and its dedup rule is `(from, to, kind)` — which would have rejected one LFO
>   driving *two params of the same node*, the normal case. Control dedups on
>   `(from, to, dest_param)`. `connect(..., Control)` and `disconnect(..., Control)` now refuse
>   outright rather than create or erase an edge that `(from, to)` cannot identify.
> - **`remove_node_bridged` DROPS control edges instead of healing them.** Bridging a removed
>   modulator's own driver onto its targets would aim a param selector that was never meant for
>   them — the same class of error the audio/note split already guards against.
> - **A control edge into an upstream node is a cycle, and P0 rejects it.** Deferred/last-block
>   modulation would need an explicit edge flag; it is a decision to take deliberately, not to
>   fall into.
>
> Also fixed in passing: `compile()`'s empty-graph early return left `note_buf_count` stale from a
> prior compile (`out` is the caller's live plan), so a count could outlive the nodes it was sized
> for. Verified by 7 mutations of the implementation, each confirmed to fail the new tests — the
> first pass of which caught a *vacuous* ordering assertion that passed on insertion-order luck.

> ### Settled — the base/resolved param API (2026-07-17)
>
> Modulation makes one value into two, and every surface has to say which it means. Decided once,
> here, because the UI, MCP, persistence, and undo all read it.
>
> **A param has a BASE (the user's knob) and a RESOLVED value (base + live modulation).** The base
> is authoritative and is what a native op's `pvals` holds; the resolved value is computed per
> block at the point of use and stored nowhere. This mirrors the visuals graph, which has had the
> split since the mapping bridge existed (`op_param_base_at` / `op_param_value_at` /
> `op_param_wired_at`, `ui/node_graph.h:89-93`).
>
> | Consumer | Reads | Why |
> |---|---|---|
> | UI knob handle, drag origin | **base** | must stay draggable; a handle that jitters at block rate is unusable |
> | persist | **base** | saving resolved would bake the modulator's instantaneous phase into the document |
> | undo's canonical projection | **base** | see the hazard below |
> | MCP | **all three** | `base`, `value` (resolved), `wired` — the same field names the visuals dump already emits (`cli/control_handlers_introspection.cpp:229`) |
>
> C API: `session_audio_graph_node_param_get`/`_set` keep meaning **base** (unchanged, so persist +
> undo + the inspector need no edit), joined by `_param_resolved` and `_param_wired`. The C spelling
> differs from the JSON (`_resolved` vs `value`) because `_get` already means base at the C level and
> a `_param_value` beside a `_param_get` would be a coin-flip at every call site.
>
> **The undo hazard, stated precisely** (it is not the one you would guess): `EditGateway` does not
> poll — `commit_frame` early-returns unless an edit set `pending_` — so a moving value cannot mark
> the document dirty or fire ADR-0018's autosave by itself. The real risk is *snapshot poisoning*:
> if the accessor returned resolved, the next **unrelated** edit would capture whatever phase the
> modulator happened to be at and undo would restore that as the user's authored value. And because
> `audio_block_equal` compares whole `tracks` arrays, a drifting param means the `Skip` restore tier
> never hits and every undo silently upgrades to `ParamsOnly`. Base-in-`pvals` makes all of it
> unrepresentable.
>
> **Known gap — plugin nodes have no base (P2 prerequisite).** `session_audio_graph_node_param_get`
> is three accessors in a trenchcoat (`audio/vst3_host.cpp:2883-2894`): native reads `pvals` (base),
> while VST3 reads `controller->getParamNormalized()` and CLAP reads `clap_param_value()` — both
> **plugin-owned current**. For a plugin there is nowhere to *get* a base from. Invisible today
> because nothing modulates a plugin param. So P2 cannot simply push control values into
> `IParameterChanges`: state comes from the plugin's own `getState()`, so saving mid-modulation
> would bake the modulator's phase into the patch (and `persist_undo` strips `state` for being
> non-deterministic, but does **not** strip `audio_graph.nodes[].params`). **P2 must build a
> host-side base for plugin params first.** P0.5 is native-only and does not have this problem.
>
> **Live values reach the UI as one atomic per MODULATOR, not per modulated param.** A modulator
> publishes its current 0..1 output; the UI applies the same `control_resolve()` the audio thread
> uses. N atomics for N LFOs rather than N×params, and no chance of the two sides drifting because
> it is the same pure function. The modulation *range* arc needs no atomic at all — it is
> `control_resolve` evaluated at src=0 and src=1, pure UI-thread math.
>
> **The bridge clobbers the base today, and it is fixed here.** `apply_audio_param_mappings`
> (`app/frame.cpp:155-181`) drives a mapped audio param by calling `param_set` every frame — which
> writes `pvals`. Map a visual source to an audio param and your knob is destroyed while mapped,
> with nothing to restore on disconnect: precisely the design this ADR rejects, shipping today on
> the bridge path. The fix keeps the bridge's value math **byte-identical** — `Mapping` is
> deliberately ABSOLUTE (`out_lo`/`out_hi`), which is right for the bridge — and changes only the
> delivery: an override rather than a write to the base. Switching the bridge to base+offset would
> have been a behavior change to every existing session that maps an audio param. The two models
> stay two models (guardrail 3); they share an apply point, not a representation.

## Context

ADR-0012 made a track's audio a rewireable DAG but kept it **per-track**, and explicitly
deferred a session-wide graph, betting that Send/Return bus nodes would cover cross-track
routing. That bet under-delivered: cross-track routing needs a *shared* graph to live in,
and three capabilities the product wants have no home in the per-track model —

- **Cross-track modulation.** One LFO/envelope driving several tracks is impossible: edges
  don't cross graphs (`AudioGraph::connect` validates both endpoints in the same graph,
  `app/src/audio/audio_graph.cpp`), and modulation isn't even in the audio graph — it lives
  in the out-of-band string-keyed `MappingRegistry` (`app/src/mapping.h`). Audio-graph
  edges are only `Audio` (sum) and `Note` (merge).
- **A real master / buses / sends / sidechain.** "Track out → master" is a hardcoded
  `master += gain * trackL/R` in the audio callback (`app/src/audio/vst3_host.cpp`
  ~2008–2015). Each track has exactly one `Output` node whose buffer is copied to L/R.
  There is no master node, no bus, no send anywhere in the model. **Note for P1:** that mix
  loop and the per-track meter / 3-band / transient publish are *the same loop*
  (`vst3_host.cpp:2008-2030`), so making master a node has to relocate the analysis too — it
  is not a lift of the mix alone.
- **Clips and generators as peers.** Clips are positional `Track::clips[scene]` with no
  identity (one MIDI clip per scene; the grid is a derived immediate-mode view); an
  algorithmic generator (Arp, a `GNKind::NativeNoteFx`) can't sit beside a clip as a
  note-source. **Note for P3:** `ClipScheduler` is **one per `Track`** (`vst3_host.cpp:129`),
  re-pointed on scene switch via `t.sched.reset(&t.clips[q])` (`:1970`) — "each clip node owns
  its own `ClipScheduler`" is a structural change, not a relocation.

The engine is ready for the lift: ADR-0012's compile→execute plan, ADR-0015's typed edges,
the generation-counter edit-mirror, preallocated pools, and now the undo `EditGateway` as a
single edit choke-point. The visuals graph already demonstrates the target — one shared
canvas of nodes with visible wires — which the audio surface should match, and eventually
share.

## Decision

1. **One `SessionGraph`.** The `Session` owns one graph, one global node-id space, one
   compiled plan, one buffer pool, one edit-mirror. A **track is a Track-Out node**; the
   **master is a node** that sums Track-Outs, replacing the callback mix. `Track` is
   retained as the per-track-out **state block** (meters, effective gain, note stream,
   clip-edit mirror), addressed by its Track-Out node id — so the bridge atomics
   (`track_N.level/transient/band_*`) and armed-track resolution are unchanged. The
   `is_output` flag splits into `is_master` (the single sink → `output_id_`) and
   `is_track_out` (interior, defines a Session-View row); every "the Output node" caller
   (`splice_before_output`, `fan_in_to_output`, `session_track_audio_graph_output_id`)
   re-points at the track-out for per-track ops and at master for the sink.

2. **Modulation is a signal — `EdgeKind::Control`.** A control edge orders a modulator
   before its target and writes the target's **param** each block. The pure graph core
   wires only buffers + an opaque param selector (`control_out_buf` mirroring
   `note_out_buf`; `control_in[] = {src_buf, param_selector}` on `CompiledStep`); the host
   applies a scalar to the param per node kind (native field / VST3 `IParameterChanges` /
   CLAP `clap_event_param_value`), alloc-free within reserved capacity and **with no
   lowering pass**. Its scope is **audio-internal / cross-track modulation only.** The
   audio↔visual **`MappingRegistry` stays a separate flat `{source, dest, amount}` table** —
   the two remain **two right-sized models, never folded** (see Relationship to
   vivid-classic). A Control edge carries the same shaper fields as `Mapping`
   (amount/curve/invert/range) because that shape is correct, not to enable a future merge.
   Block-rate initially, shaped as a control buffer so audio-rate is a non-breaking upgrade.

3. **Clips + generators are first-class nodes.** MIDI clips, audio clips, and generators
   present uniformly as **gated sources** feeding a Track-Out through a per-track-out
   **selector node**. A **scene is a named set of {track-out : enabled node} bindings**;
   launch flips a bar-quantized `enabled` atomic on the selector — **never a rewire, never
   a recompile**. Each clip node owns its own `ClipScheduler`, and the selector routes the
   enabled column's notes into the instrument through a single note edge (respecting
   `kMaxNoteInputs = 8`). The Session-View grid becomes a projection over the graph
   (rows = track-outs, cols = scenes); loose clips live in the existing `PoolClip` sidebar.

4. **One coordinate space.** The audio editor becomes a persistent object over the
   `SessionGraph` (replacing the stateless per-frame `AudioNodeGraph`), adopting the shared
   `NodeView` transform + absolute world coords + `node_canvas.h` drawing (the visuals
   graph's model). Track-Out nodes are anchors; selecting a track centers its node. This is
   deliberately the same substrate as the visuals canvas, so a future single-canvas merge
   is a small step.

5. **RT contract holds, upgraded.** One plan is **double-buffered and pointer-swapped** (no
   in-callback copy of steps/binds); the pool is capped at a realistic `kSessionMaxNodes`
   (not `kMaxTracks × kGraphMaxNodes`); solo/mute fold into a UI-thread effective-gain
   atomic per track-out (solo is never a node property); edits are **compile-validated at
   the `EditGateway` before publish** so one bad edit can't silence the whole session. The
   ADR-0015 note fallback is **re-scoped** to each Track-Out's note stream — a bare source
   resolves to the stream of the track-out it transitively feeds, so single-owner nodes
   stay bit-identical and there is no per-track broadcast. `app/docs/thread-safety.md` is
   updated for the pointer-swap contract.

6. **Phased and parity-gated** (per ADR-0005). The undo/redo branch has merged (PR #31), so each phase
   simply routes every topology edit through the now-shipped `EditGateway`:
   - **P0 — `EdgeKind::Control` in the pure core** (de-risk first; no host wiring). ✅ **landed
     2026-07-17** — see "As built" above.
   - **P1 — Unify structure + executor + pool + master node**, topology still per-track
     islands, gated on **bit-identical parity** with today. (Riskiest step; supersedes
     ADR-0012; updates `app/docs/thread-safety.md`.)
   - **P2 — Re-scope note routing + enable cross-track Audio/Control edges**; begin
     serializing Control edges (note-default migration rule).
   - **P3 — Clips + generators as first-class nodes + scene reconciliation**; backward-
     compatible load by synthesizing clip nodes from old `clips[scene]` arrays.
   - **P4 — Collapse the `(track, node)` C API to session-global** via a parallel
     `session_graph_*` shim, migrating MCP + persistence + the 96↔96 parity guard last.

## Consequences

- **Positive:** cross-track modulation, a real master/bus/send/sidechain model, and
  clips-and-generators-as-peers all become natural; the audio surface gains a single
  navigable coordinate space; special cases collapse into the graph model (the hardcoded
  master mix becomes a node) rather than proliferating.
- **Cost / risk:** this is the third change to the RT render path after ADR-0012/0015, and
  the largest. The sharp edges: the double-buffer plan swap (the RT publish contract),
  one-plan fault isolation (one bad edit could block all plan updates — mitigated by
  gateway compile-validation), the clip-node + scene migration with backward-compatible
  load, and collapsing the `(track, node)` C API that is also the MCP surface + parity
  guard + persistence. Mitigated by the per-phase bit-identical parity gates and a
  shim-based C-API migration done last.
- **Undo interaction:** the `EditGateway` is snapshot-based (`session_to_json` /
  `restore()` replays the load path), so every graph edit dirties the whole-session
  projection and an undo triggers a full session-graph rebuild + recompile — which must
  publish via the pointer-swap so audio never sees a half-built plan. Undo of a large
  (200+ node) graph must be verified glitch-free.
- **Deliberately deferred:** merging the audio and visuals *canvases* into one editor
  surface — architected-for here (the shared coordinate space), not done here. Even then the
  `MappingRegistry` stays a separate flat table; no absorption is planned.

## Relationship to vivid-classic

This change converges on the *coherent core* of the predecessor `vivid-classic` — one
session-wide graph, the session as a projection over it, modulation as a graph-level concept
— while **explicitly rejecting** the two things the reboot (ADR-0001) set out to shed: the
graph as the *home surface*, and the heavy compiled-graph runtime around it (classic's
7-pass lane/multiplicity compiler, lane-value model, and subgraph-module *flattening* /
`ModulationLoweringRecord` indirection). Adopting classic's *structure* on vivid-4's
right-sized runtime is the intent; re-adopting classic's *weight* is the failure mode.

To keep faith with the reboot, three guardrails are **binding** — violating any one reopens
this ADR:

1. **The session grid stays home; the graph is a projection/depth, never the mandatory
   authoring surface** (ADR-0002 session-view-first, ADR-0007 graph-is-a-deep-view). Normal
   work must not require the unified canvas.
2. **The executor stays right-sized.** This extends ADR-0012's compile→execute plan; it must
   never become an on-ramp to classic's lane-value 7-pass compiler or lane/multiplicity
   runtime (the line drawn by ADR-0009/0010/0011).
3. **No modulation-lowering.** `EdgeKind::Control` is applied host-side as a scalar per block;
   there is **no** flatten-into-remap+add-nodes pass. Modulation stays **two right-sized
   models** — Control edges (audio-internal) and the flat `MappingRegistry` (the bridge,
   ADR-0010's "keystone" simplification) — which are never folded together.

New node vocabulary (`Track-Out node`, `is_master`/`is_track_out`, `EdgeKind::Control`,
selector node, scene-as-bindings) is named deliberately and hardened into API / MCP tools /
persistence **last** (P4, behind the parity guard) — per the vivid-classic lesson that
vocabulary is architecture.

## Alternatives considered

- **Fold the `MappingRegistry` into `EdgeKind::Control` (one modulation model everywhere).**
  Tempting for uniformity, but rejected: it reverses ADR-0010's celebrated "keystone"
  simplification (one flat `{source, dest, amount}` table) and points straight at classic's
  modulation-lowering machinery (`flatten_subgraphs` / `ModulationLoweringRecord`). Two
  right-sized models — Control edges for audio-internal routing, the flat table for the
  audio↔visual bridge — is leaner and honors the reboot.
- **Editor-only unification** (one canvas, keep per-track RT graphs). Cheaper and low-risk,
  but delivers none of the actual capabilities (cross-track modulation, master/bus/send) —
  it only relocates pixels. Rejected as the destination; the shared-canvas UX is folded in
  as it comes for free with the real graph.
- **Keep per-track graphs; add cross-track Send/Return bus nodes** (ADR-0012's original
  bet). Rejected: a "bus" that lives in no graph is exactly the out-of-band special case
  this ADR removes; modulation and sidechain don't reduce to sends.
- **Launch scenes by wiring/unwiring the active clip.** Rejected: recompiles the graph on
  every launch (RT-unsafe); gated `enabled` atomics + a bar swap achieve it with no
  topology edit.
- **Three separate ADRs up front** (session graph / control / clips). Deferred: one ADR
  states the unified decision while nothing is implemented; sub-ADRs can spin at
  implementation time if a phase's decisions warrant it.
