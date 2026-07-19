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
> **The bridge clobbers the base today — noted, deliberately NOT fixed here.**
> `apply_audio_param_mappings` (`app/frame.cpp:155-181`) drives a mapped audio param by calling
> `param_set` every frame — which writes `pvals`. Map a visual source to an audio param and your
> knob is destroyed while mapped, with nothing to restore on disconnect: precisely the design this
> ADR rejects, shipping today on the bridge path. The clean fix keeps the bridge's value math
> byte-identical (`Mapping` is deliberately ABSOLUTE — `out_lo`/`out_hi` — which is right for the
> bridge) and changes only the *delivery*: an override rather than a write to the base. But the
> override path built here is audio-thread / per-block, fed by control edges; the bridge is
> UI-thread / frame-rate. Making the bridge deliver an override means extending the override
> mechanism to a UI-thread source — a coherent piece of its own, **deferred** so it isn't
> half-done. (An earlier draft of this note claimed the fix shipped in P0.5; it did not.)

> ### As built — P0.5 (2026-07-17): in-track modulation, end to end + visible
>
> An **inserted phase**, not in the original plan. P0 shipped a control core nothing used, and P1
> (the risky executor rewrite) delivers nothing visible — so before P1, make `EdgeKind::Control`
> *do* something in today's per-track graph. The result: an `LFO → filter.cutoff` is audible, the
> modulation is drawn on the knob, and the whole thing is drivable from MCP and reversible. This
> de-risks the entire control model against real DSP before P1 touches the RT structure.
>
> - **The control model resolves live** (`control_resolve()` in `audio/audio_graph.h`):
>   `value = clamp(base + shaped(src)·amount·(hi−lo), lo, hi)`, base read live from `pvals` and
>   never written on the audio thread. Applied at the existing param-copy in `audio_op_process`.
>   This is vivid-classic's *semantics* (`base ± amount`) without its *mechanism* — classic baked
>   the base into a compile-time remap, the sole reason its `ModulationLoweringRecord` existed;
>   keeping base a live value makes guardrail 3 structural, not a discipline.
> - **ABI v13** appends `control_out` to `VividAudioContext` (additive, floor stays 11). The **LFO**
>   is the first modulator — a `GNKind::NativeMod` source that emits control, marked with the same
>   escape hatch note effects use so the instrument picker doesn't offer it. Native only; **VST3/
>   CLAP control-apply is P2** and needs the host-side plugin base named above.
> - **The audio graph now looks and works like the visuals graph:** each node exposes its params as
>   **ports down the left edge** (native = all, plugin = the pinned/curated subset via the existing
>   searchable picker). Dragging a modulator's output onto a param port creates the control edge —
>   the drag-to-a-param gesture; a **magenta arc + live dot** on the knob show the reachable range
>   and the live value; a **shape editor** popover (amount/curve/bipolar/invert/remove) tunes an
>   existing edge. All undoable; `set_control_shape` also exposed over MCP.
> - **Two pre-existing bugs fixed on the way**, both reaching beyond this feature: the audio-graph
>   INPUT handlers built their hit-test graph without the view transform (so clicks missed once the
>   canvas was panned), and `EditGateway::note_edit` dropped a lone structural mouse-gesture edit
>   from undo (`force_close_group` only flushed prior dirt) — every drag-connect (the existing
>   audio-wire connect included) was silently non-undoable.
> - **Reload path — investigated, found stable (an earlier "SIGSEGV" was a misdiagnosis).** During
>   P0.5 a heavy-plugin session reload appeared to crash; a follow-up investigation could **not**
>   reproduce it in 60+ heavy teardown/reload cycles (Pigments + Atoms + Serum2, transport playing,
>   incl. `new_session` full teardown), there is **no crash report and no crash signature in any
>   log**, and the reload path is well-guarded (retire-not-free, null-checked derefs, an RT
>   bail-to-silence net). The apparent crash was the backgrounded-app symptom — the control server
>   only drains MCP from the foreground run loop, so a reload driven while the window is not
>   frontmost returns `http 000` (alive but unresponsive), which read as "dead." **Reload is not a
>   demonstrated P1 blocker.** (A real, separate finding along the way: the undo/dirty *projection*
>   was calling plugin `getState()` only to strip the result — a needless `getState`‖`process`
>   contention on the hot edit path; fixed independently, framed as a perf/correctness win, not a
>   crash fix.)

> ### Design — P2a (2026-07-18): cross-track modulation
>
> The design for the ADR's marquee capability — a modulator on one track driving a param on
> **another** track. P1 (merged, #43–47) made the master a node, consolidated the track-output pool,
> and added solo/mute, but every track's audio graph is still a private island: its control pool
> (`Track::cpool`) and node ids are track-local, and its modulators run *inside* that track's render.
> P2a adds cross-track *control* on **today's per-track render** — deliberately **not** the audio-
> executor unification, which stays deferred until cross-track **audio** routing / note re-scope
> (the rest of the ADR's "P2") genuinely need it. Chosen over the big executor rewrite first: the
> highest-value piece is reachable on the current structure.
>
> - **Two problems to solve.** *Ordering* — tracks render in `tracks_view` order, so a source
>   modulator may render *after* the node that reads it. *Addressing* — a consumer reads
>   `t.cpool[src_buf·kGraphMaxBlock]` (`vst3_host.cpp:684`), an index into its **own** track's pool.
> - **One session control pool (per-track regions) + a modulator pre-pass — Option B, chosen for
>   long-term cleanliness.** Replace the per-track `Track::cpool` with a single `Session`-owned control
>   pool laid out as **per-track regions** — the direct analog of the P1b.3a `track_out_pool` (track
>   *i* owns region *i*; a control value's absolute index = `region_base + local_ctl_index`). In-track
>   control now reads `ctl_pool[my_base + src_buf]` instead of `cpool[src_buf]` — same values, just
>   relocated, so **bit-identical** (exactly the `bl`/`br`→pool relocation P1b.3a proved). A **pre-pass**
>   over `tracks_view` runs every track's `GNKind::NativeMod` steps *before* any audio render, writing
>   each modulator into its region (+ `ctl_pub` for the UI dot, as at `:709-710`); `run_track_graph`'s
>   NativeMod branch (`:698-712`) becomes memset-silence + skip. The per-step "resolve control → run
>   op" logic (`:670-696`) is factored into a shared helper the pre-pass and render loop both use
>   (preserving modulator-drives-modulator + stacking). A modulator runs **once per block** (pre-pass,
>   not inline); the gate is today's `LFO → SVFilter.cutoff` unchanged.
> - **Offset addressing, one source of truth.** With control in one pool of per-track regions, a source
>   is addressed by `src_region + local index` — pure offset math, **no per-modulator id registry**.
>   (The rejected *additive* alternative kept `cpool` AND a second write into a session bus AND a slot
>   table: it stored every control value **twice** — a classic drift hazard — and still did the
>   cross-track addressing work, so it was strictly more machinery for a worse invariant.) One pool =
>   one source of truth for every control value, in-track and cross-track.
> - **Session-level cross-track edges.** `Session` owns a list `{src_track_id, src_node_id,
>   dst_track_id, dst_node_id, dst_param, ControlShape}`; per-track in-track control edges stay in each
>   `agraph`. When a track republishes (or membership changes — where region bases are already
>   recomputed), each cross-track edge is resolved (UI thread) to `{dst_node_local_id, dst_param,
>   src_pool_index = src_region + src_local_index, shape}` and **published** to the audio thread via
>   the gen + `try_lock` handoff (Pattern 3, `thread-safety.md`). `run_track_graph` applies matches
>   with the **same** `control_resolve(base = audio_op_param_get(...), src = ctl_pool[src_pool_index],
>   shape, lo, hi)` (`audio_graph.h:89`), stacking with any in-track override on the same param (`:687-694`).
> - **Guardrail 3 preserved.** The pool carries the modulator's raw 0..1; the dst resolves against its
>   own **live base** (`pvals`). Cross-track is the identical live base+modulation combine as in-track
>   — no lowering, no baked remap. Only the *region* the source value is read from differs.
> - **Constraints.** Cross-track control targets **audio-node params** (0-latency: read after the full
>   pre-pass); a cross-track **modulator→modulator** edge is **rejected** in P2a (it would need cross-
>   track modulator ordering — revisit if wanted). Connect validates the in-track rejects
>   (`audio_graph.cpp:149-160`): duplicate `(src,dst,param)`, unknown node, `param < 0`.
> - **Surface.** Session-level `session_connect_control` / `disconnect` / `set_control_shape`
>   (`src_track/src_node/dst_track/dst_node/param` + shape), paralleling the in-track
>   `audio_graph_connect_control` (`control_handlers_audio.cpp:527`), with matching `mcp/vivid_mcp.py`
>   tools (parity guard stays green) and an `xcontrol` array in `get_audio_graph` (`:613`); persisted
>   as a top-level `xcontrol` JSON array mirroring the per-track control-edge shape (`persist.cpp:182-189`).
> - **Sub-phased, each a PR:** **P2a.1** consolidate `cpool` → one session control pool + pre-pass
>   (bit-identical enabler; gate =
>   in-track LFO unchanged, `ctest` 49/49) → **P2a.2** the cross-track edges + apply/publish + MCP
>   (the capability; gate = a track-A LFO moves a track-B param's resolved value while its base holds,
>   audibly, in-track still bit-identical, parity green) → **P2a.3** persist + editor wires (round-trip
>   + undo). RT contract verified under `-DVIVID_SANITIZE_THREAD=ON` with live cross-track editing.

> ### Design — P2b (2026-07-18): the executor unification + cross-track audio + note re-scope
>
> The ADR's **riskiest step**, deferred through P1/P2a because it delivered nothing until a consumer
> existed — now cross-track **audio** is that consumer. Cross-track control (P2a) worked on the
> per-track engine because a control value is one block-rate number that tolerates a block of latency.
> Cross-track **audio** is a full signal forming a real DAG dependency (track B's node needs track A's
> node's output THIS block), so the source must actually render before the consumer and its buffer
> must be reachable across the track boundary. That is the deferred **single-plan executor / one pool
> / global node-id space** (P1b.3c), and P2b builds it, then adds cross-track audio + note re-scope
> on top. User chose the **full executor unification** over a lean inter-track-routing shortcut (the
> clean end-state; gated bit-identical at every step, like every prior phase).
>
> **Today (the target of the rewrite):** each `Track` owns its graph — `agraph` (authoritative
> topology) → `gcg`/`gbinds` (compiled plan) → `gpool` (node buffer pool) — and `run_track_graph(t)`
> renders that track's plan into the track's `track_out_pool` slot. `session_process` does: plan-swap
> loop (all tracks) → modulator pre-pass (P2a.1b) → per-track render loop (`run_track_graph` per gok
> track) → master sum. Notes are a per-track broadcast (`t.nev`); the control pool is already session-
> wide (P2a.1). No cross-track audio path exists — each track is an independent island.
>
> **The end-state:** `Session` owns ONE graph (every track's nodes **plus a master node**, one global
> node-id space), ONE compiled plan (a single topo-sort across all nodes, so cross-track edges just
> fall out of ordering), ONE buffer pool. A **session executor** replaces the per-track
> `run_track_graph`. `Track` becomes a **state block** (meters/gain/events/clips/note-stream)
> addressed by its **track-out node id**; per-track EVENT PREP + the modulator pre-pass stay per-track
> ahead of the executor. Cross-track audio + note edges are then just edges in the one graph.
>
> **Sub-phased, each a PR, gated bit-identical (except the two new-capability steps):**
> - **P2b.1 — Session-owned node buffer pool (per-track regions).** Consolidate per-track `gpool` into
>   one `Session::node_pool` laid out as per-track regions — the third instance of the P1b.3a /
>   P2a.1 relocation (track-out pool, control pool, now node pool). `run_track_graph` renders into its
>   region via a base offset carried on `blk`. Bit-identical (pure relocation); the session-visible
>   pool cross-track audio will read from.
> - **P2b.2 — One compiled session plan + the session executor.** Factor `run_track_graph`'s per-step
>   body into a `process_step(step, Track&, pool, blk)` and build ONE session plan = every track's
>   steps (session-global buffer indices) **each tagged with its owning Track**, topo-ordered per-
>   track island. The executor iterates the one plan calling `process_step`; the per-track render loop
>   disappears. Per-track islands ⇒ same steps, same order ⇒ bit-identical. THE risky RT rewrite —
>   verify under TSan. (Event prep + pre-pass stay per-track, ahead of the executor.)
> - **P2b.3 — Global node-id space + master-as-a-node.** Assign session-global node ids (cross-track
>   edges address nodes across tracks — the scoped P1b.3b, now with a consumer). Make **master a real
>   step** in the session plan that sums the track-out buffers (replacing the hardcoded master sum);
>   split `is_output` → `is_master` / `is_track_out`. Bit-identical.
> - **P2b.4 — Cross-track AUDIO edges (new capability).** A session-level audio edge from a node on
>   one track to a node on another; the single topo-sort orders it; the executor routes the buffer
>   through the one pool. `session_connect_audio` / MCP / persist / report — parallels the P2a
>   cross-track-control surface. Gate: track A's signal audibly feeds track B; a cross-track cycle is
>   rejected at compile (kept in the caller's last good plan).
> - **P2b.5 — Note re-scope + cross-track note edges (new capability).** Re-scope the ADR-0015 note
>   routing (today a per-track `t.nev` broadcast) onto the one graph so a note edge can cross tracks
>   (an arpeggiator on one track driving an instrument on another). Note-default migration rule for
>   old projects. Gate: cross-track note routing works; existing per-track note routing unchanged.
>
> **Risks / invariants:** every sub-PR gates on **bit-identical parity** (same session renders sample-
> for-sample; meter atomics match) verified live via MCP + by ear, plus `ctest` + `-DVIVID_SANITIZE_
> THREAD=ON` (esp. P2b.2's executor). Guardrails hold: the executor stays **right-sized** (per-step
> dispatch, NOT classic's lane-value compiler — guardrail 2); pool capped at a realistic
> `kSessionMaxNodes`; **compile-validate at the EditGateway** before publish (one bad edit can't
> silence the session); `app/docs/thread-safety.md` updated for the unified publish. The
> `(track, node)` C-API collapse to session-global (ADR P4) stays deferred — P2b keeps the existing
> per-(track,node) surface, adding session-level `connect_audio` alongside it.

> ### Design — P3 (2026-07-19): clips + generators as first-class nodes + scene reconciliation
>
> The ADR's **deepest migration** (pillar 3), deferred until the session graph + note model existed. It
> touches four subsystems at once — the note path, the scene/launch path, `ClipScheduler` ownership, and
> the grid UI — so it is decomposed into bit-identical steps like every prior phase, and it has a hard
> **prerequisite: P2b.5 (note re-scope)**.
>
> **The precedent already in the tree.** The audio **Sampler is already a clip-as-node**: `GNKind::Sampler`
> carries NO clip pointer in its binding (`vst3_host.cpp:93`); `render_sampler_block` (`:2638`) reads
> `t.active` each block and renders `t.aud_clips[sc]` under a `try_lock` — the scene→clip resolution lives
> *inside the node's render*, not baked into a binding or a held raw pointer. P3 is, in one line:
> **generalize that pattern to MIDI clips, then split the per-track clip stack into per-clip nodes fronted
> by a selector.**
>
> **Today (the target of the rewrite).** MIDI clips are positional `Track::clips[scene]` (`:191`) with no
> identity; the grid is a derived immediate-mode view. One **`ClipScheduler` per Track** (`:192`) is
> re-pointed at a bar boundary via `sched.reset(&clips[q])` (`:2810`) — the scheduler is playhead-*stateless*
> (recomputed from transport each block; only `active[]` held-notes carried, `midi_clip.h:76`), non-owning,
> and depends on the `kMaxScenes=8` reserve (`vst3_host.h:29`) to keep `&clips[q]` valid across scene
> appends. Its notes reach an instrument through the **per-track `t.nev` broadcast** — a source with no note
> edge falls back to `t.nev` at `graph_note_input` (`:779`, `s.n_note_in <= 0 => return t.nev`). Note EDGES
> exist (ADR-0015: `MidiIn`/`NativeNoteFx` nodes, the `t.npool` buffers, `kMaxNoteInputs=8`) but nothing
> wires them by default. Launch just sets `t.queued` (`:1723`), applied to `t.active` at the next bar. **No
> selector/mux node exists anywhere** — `graph_note_input` *merges* note inputs (`:785`), it never *selects*.
>
> **The end-state.**
> - A **MidiClip node** (a new gated note *source*, `GNKind::MidiClip`) owns one `MidiClip` + its own
>   `ClipScheduler` and emits on a note-out buffer (`t.npool`) — never the broadcast. Like the Sampler it
>   holds no cross-vector raw pointer: the node owns its clip, so the fragile `&clips[scene]` + kMaxScenes
>   reserve contract is *replaced* by a per-node clip + edit-mirror (cleaner — no shared-vector realloc, no
>   `invalidate_active_src` dance).
> - A **per-track-out Selector node** (the second new kind) takes the scene-clips as note inputs + an
>   `active_scene` atomic; it passes through only the active scene's clip to its single note-out → the
>   instrument's one note edge, so instrument note fan-in stays 1 regardless of scene count (respects
>   `kMaxNoteInputs=8`). Only the active clip node runs its scheduler (the rest gate to silence) — one live
>   scheduler per track-out, exactly today's cost.
> - A **scene is a named set of `{track-out : enabled clip/generator node}` bindings.** Launch flips the
>   selector's `active_scene` atomic, bar-quantized — **never a rewire, never a recompile** (the ADR
>   explicitly rejects launch-by-rewiring). The grid (rows = track-outs, cols = scenes) becomes a
>   *projection* over these bindings; loose clips stay in the `PoolClip` sidebar.
> - **Generators are peers**: a generator (euclidean, chord, …) is just another gated note-*source* node in
>   a scene column. Only `Arp` exists natively today (a note *effect* — notes in→out, `builtin_audio_ops.cpp:489`);
>   the generator *ops* are separate content work — P3 provides the SLOT, not the ops.
> - **Audio clips** already resolve `t.active` inside the Sampler, so they fold into the selector model last
>   (or the Sampler-as-selector is confirmed sufficient as-is); MIDI leads.
>
> **Sub-phased, each a PR, gated bit-identical (except the two new-capability steps):**
> - **Prereq status (refined 2026-07-19).** P2b.5 SHIPPED as cross-track *note edges* (the note-domain peer
>   of P2b.4 audio) — NOT a broadcast kill. In the current per-track model the `t.nev` broadcast already IS
>   the single track-out's stream, so a global "kill the broadcast" buys nothing standalone and is pervasive;
>   it therefore **folds into P3.1** below (the derived graph builds the note sub-graph, and the broadcast
>   fallback simply stops being reached for those tracks). No separate prereq PR.
> - **P3.1 — note production becomes graph nodes (the re-scope + clip-as-node, together).** The wrinkle: the
>   instrument reads `t.nev` = **four** sources (clip scheduler + scene-switch releases + live-MIDI + editor
>   preview), so routing only "clips" to it would drop live/preview and break bit-identity. Resolution: the
>   derived graph builder (`rebuild_track_graph`) constructs a small note sub-graph feeding the instrument
>   via note edges instead of the broadcast — a **MidiClip** source node (the clip scheduler + scene releases,
>   reading `t.active`; the MIDI mirror of the Sampler) **plus** a **MidiIn** node (live-MIDI + preview, which
>   `GNKind::MidiIn` already emits from `t.nev`) — both merged into the instrument's note-in (`kMaxNoteInputs`
>   allows it). Bit-identical: MidiClip-clips + MidiIn-live/preview == the original `t.nev`, same offsets;
>   `graph_note_input`'s broadcast fallback is untouched (just no longer reached once the instrument has note
>   edges). Pervasive but uniform (every derived track builds the same sub-graph) and now safe — the derived
>   rebuild re-resolves cross-track edges (the #80 fix), so the added nodes' index shift is handled. THE
>   pivotal step; split it further if needed (MidiIn-only re-scope first, then add MidiClip).
> - **P3.2 — split into per-clip nodes + a Selector.** One MidiClip node per scene slot (each its own clip +
>   scheduler) feeding a per-track-out Selector that passes the active scene. Bit-identical (the active clip
>   plays identically); clips are now first-class, individually addressable nodes.
> - **P3.3 — scenes as bindings + launch-as-atomic + backward-compat load (new capability).** Reify scenes
>   as `{track-out : enabled node}` bindings; the grid becomes a graph projection; synthesize clip nodes
>   from old `clips[scene]` arrays on load so old projects round-trip. Generators slot in here.
> - **P3.4 — audio clips fold into the selector model** (or confirm the existing Sampler-as-selector
>   suffices); persist + MCP + the grid-as-projection UI, gated where they are surface.
>
> **Risks / invariants.** Per-clip `ClipScheduler` ownership is a *structural* change, not a relocation —
> each clip node owns its clip + scheduler, so the audio-thread clip-pointer / `invalidate_active_src`
> contract (`:2772`) is re-derived per node, not shared. One scheduler runs per track-out (gated), keeping
> today's cost. Every non-capability sub-PR gates on **bit-identical parity** (same session renders
> sample-for-sample; note on/off offsets identical) verified live via MCP + by ear, plus `ctest` +
> `-DVIVID_SANITIZE_THREAD=ON`; edits compile-validate at the `EditGateway`; `kSessionMaxNodes` must budget
> for scenes × tracks clip nodes. The grid UX is preserved as a projection, so the whole migration is
> invisible to a user who never opens the graph.

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
   - **P0.5 — in-track modulation, end to end + visible** (inserted; not in the original plan).
     ✅ **landed 2026-07-17** — host-side control apply (native), ABI v13 + the LFO, params-as-ports
     UI, knob arc/dot, the shape editor; MCP + persist + undo. See "As built — P0.5" above.
   - **P1 — Unify structure + executor + pool + master node**, topology still per-track
     islands, gated on **bit-identical parity** with today. (Riskiest step; supersedes
     ADR-0012; updates `app/docs/thread-safety.md`.)
   - **P2 — Re-scope note routing + enable cross-track Audio/Control edges**; begin
     serializing Control edges (note-default migration rule). **P2a (cross-track *control*) ✅ SHIPPED**
     (#50/#51/#52/#54 — engine, control, persist, introspection; editor wires deferred). **P2b
     (executor unification + cross-track *audio* + note re-scope) is designed — see "Design — P2b"
     above** (the deferred single-plan executor / one pool / global-id space, now that cross-track
     audio is its consumer).
   - **P3 — Clips + generators as first-class nodes + scene reconciliation**; backward-
     compatible load by synthesizing clip nodes from old `clips[scene]` arrays. **Designed — see
     "Design — P3" above** (generalize the Sampler's clip-as-node pattern to MIDI, then split into
     per-clip nodes + a per-track-out selector; hard prereq = P2b.5 note re-scope).
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
