# Phase 2: Realtime Audio And Thread Safety

Status: done (audited 2026-07-31)

## Verdict

**FAIL — 1×P0 release blocker** (+ 1×P2, 4×P3). The realtime *engine* is strong: the
audio side is non-blocking everywhere (every structural handoff uses `try_lock` + a
generation counter; no blocking lock, `new`/malloc/`std::string`, file IO, or UI/GPU call
is reachable from `audio_callback`); all 12 documented cross-thread channels verify
correct and both TSAN gates pass clean; out-of-process plugin **scan** isolation is
robust; all five threads are joined and none mutate session state off the main thread.
**But live plugin execution on the RT thread is unguarded** — a crashing or hanging hosted
VST3/CLAP can take down or stall the host with no fault boundary (P0-01). That blocks the
release candidate until at least crash attribution lands. See §E for the finding table.

## Purpose

Verify that audio execution, transport, plugin hosting, analysis, and shared state satisfy the
release bar for realtime safety and thread correctness.

## User Task

Play, edit, analyze, and recover from audio/plugin activity without dropouts, deadlocks, data races,
or unsafe callback behavior.

## Hypothesis

If realtime boundaries are respected, release users can trust Vivid as an instrument rather than a
fragile demo.

## Pressure Test

Audit audio callback paths, graph mutation, note/event queues, analysis rings, plugin scan/hosting,
transport synchronization, and TSAN-relevant shared state.

## Scope

- Audio callback, audio graph, transport, note/event buses, clip DSP, sample engine, plugin hosting,
  analysis rings, movie audio bus, and UI/control interactions with audio state.
- Thread creation and ownership that can touch audio state.
- Sanitizer builds and stress tests relevant to realtime safety.

Out of scope: subjective audio quality unless a code issue causes instability, clipping hazards, or
incorrect timing.

## Audit Procedure

1. Trace the audio callback from device entry to operator/plugin/sample execution and back.
2. List every operation on the realtime path that could allocate, lock, block, do file IO, call UI
   code, or cross into plugin code.
3. Trace graph mutation and transport edits while playback is running.
4. Review queues, atomics, rings, snapshots, and lifetime ownership for analysis and note/event
   data.
5. Run existing audio tests, production-gate slices, and sanitizer targets where practical; note
   missing coverage explicitly.

## Evidence To Collect

- Realtime path trace with risky operations marked.
- Shared-state inventory: owner thread, reader threads, synchronization primitive, and tests.
- Test/sanitizer command summaries.
- Reproduction steps for any dropout, deadlock, race, or unsafe plugin behavior.

## Deliverables

- Realtime safety report.
- Thread-safety risk table with severity.
- Test gaps that must be closed before release.

## Acceptance Criteria

- Audio callback paths avoid allocation, blocking locks, file IO, and UI dependencies.
- Cross-thread state handoff uses explicit queues, atomics, or owned snapshots.
- Plugin failures cannot crash or corrupt the host project.
- Transport and note timing remain deterministic under ordinary editing.
- TSAN or focused stress tests cover the highest-risk shared-state paths.

## Failure Modes

- UI edits mutate audio-owned state directly.
- Plugin scanning or hosting runs unsafe work on the realtime path.
- Analysis buffers race with render or agent inspection.
- Timing bugs only appear under sustained playback or project reload.

## Evidence Log

Method: three parallel source sweeps (RT-path trace; cross-thread channel-table
verification; thread inventory + plugin isolation + test surface), the two load-bearing
findings re-read directly, then the TSAN gates run locally for fresh evidence. Paths
relative to `app/src/` unless noted.

### A. Realtime-path trace (`audio_callback` → exit)

```
audio_callback(ma_device*, out, in, frames)                 audio/audio_callback.cpp:17
├─ transport beats/bpm/is_playing loads (atomic)            audio_callback.cpp:22-24
├─ movie_audio_begin_block  (lock-free rings)               audio_callback.cpp:34
├─ session_process(session, fout, …)                        audio_callback.cpp:38 → vst3_host.cpp:2125
│   ├─ memset out                                           vst3_host.cpp:2129
│   ├─ tracks_mtx / xctl / xaudio / xnote / gmtx try_lock   vst3_host.cpp:2141,2153,2161,2169,2194
│   ├─ modulator pre-pass → audio_op_process               vst3_host.cpp:2219 → audio_op_runtime.cpp:284
│   ├─ PREP loop: edit_mtx / fx_mtx / op_fx_mtx try_lock    vst3_host.cpp:2250,2272,2280
│   ├─ EXECUTOR loop over session_plan                      vst3_host.cpp:2495
│   │   ├─ native op → audio_op_process → process_audio     audio_op_runtime.cpp:378  [guarded]
│   │   ├─ VST3 inst → processor->process                   vst3_host_render.cpp:111  [UNGUARDED]
│   │   ├─ VST3 fx   → processor->process                   vst3_host_render.cpp:154  [UNGUARDED]
│   │   ├─ CLAP inst → clap_run                             vst3_host_render.cpp:175  [UNGUARDED]
│   │   ├─ CLAP fx   → clap_run                             vst3_host_render.cpp:188  [UNGUARDED]
│   │   └─ Sampler   → render_sampler_block                 vst3_host.cpp:2108
│   └─ Finalize → node_scope/node_an ring taps, meters      vst3_host.cpp:938-1005 (atomics)
├─ movie_audio_mix_master (lock-free rings)                 audio_callback.cpp:49
├─ metronome click synth                                    audio_callback.cpp:53-76
├─ capture_write_interleaved (try_lock) / recording_tap     audio_callback.cpp:79-80
├─ transport->advance (atomics)                             audio_callback.cpp:81
└─ master RMS/band/transient → atomic stores                audio_callback.cpp:82-102
```

**Risky ops ON the path.** No blocking `lock`/`lock_guard` is reachable from
`audio_callback` — every one in `vst3_host.cpp` (`:2574,2584,2610,…`) and `transport.h`
(`:59,81,88`) is on the UI-thread mutator/snapshot side. No `new`/malloc/`std::string`
on the hot path (all scratch is fixed-capacity, reserved to `kGraphMaxNotes`/
`kGraphMaxBlock`). No UI/GPU call. Genuine violations: **(1)** `std::fprintf(stderr,…)`
in `recording_tap_write` (`transport.h:122`, per-block call site `audio_callback.cpp:80`)
— once-per-session + overrun-gated but a syscall on the RT thread (→ P3-01); **(2)**
`t.fxl.resize(frames)` in the VST3/CLAP effect render (`vst3_host_render.cpp:139,182`) —
can heap-alloc if a block grows past the reserve, never fires with a fixed CoreAudio block
(→ P3-02). One `std::sort` on the multi-note-merge branch (`vst3_host.cpp:512`), in-place
and bounded — acceptable.

### B. Cross-thread channel verification (against `app/docs/thread-safety.md`)

All **12 documented rows verified correct** against code: transport scalars
(`transport.h:14-22`), VST3 param SPSC ring (drops on full, `vst3_host_common.h:492`),
the gen-counter/`try_lock` structural edits (`edit_gen`/`fx_gen`, publish
`vst3_host.cpp:2005/2574`, audio copy `2249-2275`, `fx_retired` freed only at shutdown
`:2529`), live-MIDI ring (`vst3_host_internal.h:39-63`), node-analyze mask (ring allocated
before the release-store, `vst3_host_analysis.cpp:70-71`), active-notes bus (`track_id`
published last, `note_bus.cpp:62-64`), spectrum sample ring (`analysis_ring.h`), modulator
control-out, per-track note scalars (held until next note), control-server work, per-node
scope/FFT bank (`node_ring_bank.h`), held-note set (`held_note_set.h`). The 5 ADR-0029
array-snapshot channels are confirmed atomic-element + TSAN-clean.

**Gaps found (→ P3-03):** ≥5 real audio-reachable channels have **no table row** —
note-event ring/bus (`note_event_ring.h`, `note_event_bus.cpp`, double-buffered),
master spectrum band bus (`spectrum_bus.cpp`), movie audio bus (`movie_audio_bus.cpp`,
the only render→**audio** inbound channel, with an epoch-reset guard), recording tap ring
(`transport.h:101-138`), live-capture buffer (`transport.h:27-88`, audio-side `try_lock`);
plus the `op_fx_gen`/`op_fx_mtx` structural mirror is undocumented. All are correctly
implemented — this is a contract-completeness gap against ADR-0029's own "a channel isn't
done until it has a table row" rule.

### C. Thread inventory

Five threads, **all `std::thread` (joined), none detached**, none mutate session/graph
off the main thread:
1. miniaudio device/RT thread (`main.cpp:293/311` → `audio_callback.cpp:17`).
2. Async CLAP loader (`vst3_host_clap_loader.cpp:44`) — builds handles only, under
   `clap_load_mtx`; all binding on main via `session_poll_plugin_loads`. The "worker
   touches only the clap queues, never `s->tracks`" invariant is load-bearing and
   type-unenforced.
3. Plugin-scan worker (`plugin_scan.cpp:147`) — out-of-process probing; results applied
   on the UI thread.
4. Control-server listen thread (`control_server.cpp:49`) — enqueue only; handlers run on
   main.
5. Hot-reload compiler (`packages/hot_reload.cpp:13`) — results applied on main.

### D. Plugin-hosting failure isolation

**Scan-time: robust.** Out-of-process probe (`plugin_probe.cpp:271`): `posix_spawn` child,
fd-3 verdict channel (`kProbeVerdictFd`), reads only the factory descriptor (stops before
`createInstance`), 30 s deadline → `SIGKILL` (`plugin_probe.cpp:339-345`), crash/hang →
`kClassCrashed` cached and never re-opened; plus a `plugin_probe.lock` sentinel
(`plugin_scan.cpp:35-60`) and stateless quarantine (≥3 crashes/24 h, `quarantine.h:17-18`).
A bad plugin cannot take down the app during scan.

**Runtime: unguarded (→ P0-01).** `CrashGuard` (`app/crash_guard.h:44-59`) wraps native
ops (`audio_op_runtime.cpp:377`) and visual ops (`visual_graph.cpp:464`) but **not** the
four hosted-plugin process sites (`vst3_host_render.cpp:111,154,175,188`). And CrashGuard
is attribution-only — it writes a marker then re-raises `SIG_DFL`, so the process still
dies; without it wrapping the plugin path, a plugin crash is an **anonymous** SIGSEGV that
can't be attributed → no quarantine. A C++ exception out of `process` → `std::terminate`.
A hanging plugin → unbounded RT stall, no watchdog. This is the P0.

### E. Sanitizer / test evidence

Ran locally 2026-07-31 (14-core, no in-flight CI):
- **`ctest -L THREAD`** (build-tsan, `VIVID_BUILD_APP=OFF -DVIVID_SANITIZE_THREAD=ON`):
  **4/4 PASS** — `test_note_bus`, `test_analysis_ring`, `test_node_ring_bank`,
  `test_held_note_set`. TSAN-clean.
- **`ctest -L AUDIO_THREAD`** (build-tsan-audio, app ON + TSAN, `halt_on_error=1`):
  **1/1 PASS** — `test_session_concurrency` (render thread vs UI mutator over the
  gen-counter/`try_lock`/SPSC/atomic-ring surface). TSAN-clean.
- Plain functional baseline: the `build/` dir is stale (targets failed to build / "no
  tests found"); the AUDIO_THREAD leg above already exercises the same test functionally,
  so this is non-blocking.

**Coverage caveat (→ P3-04):** per ADR-0029, TSan only finds races on code it runs
*racing*. The passing gates cover the documented channels — they do **not** exercise the
P0-01 plugin path, the P2-01 CLAP param-queue overrun, the CLAP async-loader vs
`session_poll_plugin_loads` race, or the control-server cross-thread handoff. Those are
untested, so "gates green" is not evidence they are safe.

### F. Findings

#### P0-01: Live plugin `process` runs unguarded on the RT thread — BLOCKS RELEASE

- Surface: code — `audio/vst3_host_render.cpp`, `audio/clap_host.h`
- Impact: a crashing hosted VST3/CLAP is an anonymous SIGSEGV that kills the process and
  **cannot be attributed** (empty crash marker → no quarantine); an exception out of
  `process` → `std::terminate`; a hanging plugin → unbounded RT stall with no watchdog.
  Third-party plugin code — the likeliest thing to fault — is the one item on the hot path
  with no fault boundary. Violates "Plugin failures cannot crash or corrupt the host
  project."
- Evidence: `vst3_host_render.cpp:111,154,175,188` (no guard) vs guarded native path
  `audio_op_runtime.cpp:377-378`; `app/crash_guard.h:44-59` (attribution-only).
- Smallest acceptable fix (release-gating): wrap the four plugin-process call sites in a
  `CrashGuard` naming the plugin, so a crash is attributed → existing crash-recovery +
  quarantine can disable it on relaunch. Hang-watchdog / bounded-time processing is the
  larger, non-gating half.
- Owner/status: Unassigned | **blocks RC** | fix + ADR-0045, own gated PR

#### P2-01: CLAP param queue never drops on full → torn read on the RT consumer

- Surface: code — `audio/clap_host.h:44-48`
- Impact: `ClapParamQueue::push` writes `buf[wi % N]` with no capacity check; when the UI
  outruns the audio consumer it laps the ring and `pop` (`:49-54`) can read a `{id,value}`
  slot mid-overwrite — a genuine data race on a non-atomic struct, unlike VST3's
  drop-on-full queue (`vst3_host_common.h:492`). Rare (N=2048) but real UB; the doc's
  "full → drop" invariant does not hold for CLAP.
- Smallest acceptable fix: mirror VST3 — drop (return without advancing) when
  `w - r >= N`.
- Owner/status: Unassigned | P2 (cheap; fix before release recommended) | own gated PR

#### P3-01: `std::fprintf(stderr)` on the RT thread

`transport.h:122` in `recording_tap_write` (per-block call site `audio_callback.cpp:80`).
Once-per-session + overrun-gated, but a `stderr` syscall on the audio thread. Fix: atomic
overrun counter surfaced off-thread. Owner/status: Unassigned | P3.

#### P3-02: On-hot-path `resize()` in effect render

`vst3_host_render.cpp:139,182` `t.fxl/fxr.resize(frames)` can heap-alloc if a block grows
past the reserve (never fires with a fixed CoreAudio block + `kGraphMaxBlock` reserve).
Fix: pre-size at track setup + assert rather than resize on-path. Owner/status:
Unassigned | P3.

#### P3-03: Cross-thread channel table is incomplete

≥5 real channels + the `op_fx_gen` mirror missing from `app/docs/thread-safety.md` (see
§B). All correctly implemented — a contract-completeness gap vs ADR-0029's own rule. Fix:
add the missing rows. Owner/status: Fixed (this pass — verified rows added to
`thread-safety.md`).

#### P3-04: Test/sanitizer coverage gaps

No test drives the P0-01 plugin crash/hang path, the P2-01 CLAP overrun, the CLAP
async-loader race, or the control-server cross-thread handoff (§E). Fix: the P0/P2 fixes
should each ship with a racing/harness test that joins the `THREAD`/`AUDIO_THREAD` legs.
Owner/status: Unassigned | P3 (must-note for release).

## Open Questions (answered / flagged)

- **First-release audio performance budget?** Not defined in code as an explicit budget;
  the implicit contract is "no allocation/blocking on the RT path within a fixed CoreAudio
  block" plus the reserve ceilings `kGraphMaxNodes`/`kGraphMaxNotes`/`kGraphMaxBlock`
  (`vst3_host_common.h`). **Recommend** the release set an explicit budget (max block ms,
  max tracks/graph nodes, xrun tolerance) so P3-02-style growth calls can assert against
  it. Owner decision, not code-derivable — flagged for the release runbook.
- **Which plugin formats are release-supported vs experimental?** VST3 and CLAP are both
  hosted on the RT path with equal reach; the audit found the CLAP param queue weaker than
  VST3's (P2-01). Until P0-01 + P2-01 land, **CLAP is the more fragile of the two**. The
  release must state supported-vs-experimental explicitly; a defensible first-release
  stance is "VST3 + CLAP supported once P0-01 attribution + P2-01 drop-on-full ship;"
  otherwise mark CLAP experimental. Owner decision.
- **Which sanitizer targets gate the RC?** The two ADR-0029 legs are the gate: CI
  `tests (thread-sanitizer)` (`ctest -L THREAD`) and macOS `tests (audio-thread-sanitizer)`
  (`ctest -L AUDIO_THREAD`, `test_session_concurrency`). Both pass today (§E). **They must
  stay green AND grow** to cover the P0-01/P2-01 paths (P3-04) before the RC is approved —
  a green gate over untested code is not sufficient.

## Follow-Up Plans

- **ADR-0045 — Realtime plugin fault isolation** (proposed):
  `docs/decisions/ADR-0045-realtime-plugin-fault-isolation.md`. The P0-01 crash-attribution
  fix as the release-gating minimum + the hang-watchdog / bounded-time / out-of-process
  options as post-release policy.
- **P0-01 fix PR (blocks RC):** `CrashGuard` around the four plugin-process sites, with a
  test that asserts a plugin crash is attributed (closes part of P3-04).
- **P2-01 fix PR:** CLAP param-queue drop-on-full, with a producer/consumer overrun race
  test under the `THREAD` label (closes part of P3-04).
- **P3-01 / P3-02:** small RT-hygiene PRs (atomic overrun counter; pre-size fx scratch).
- **thread-safety.md channel table:** missing rows added this pass (P3-03).
- Sanitizer runs captured in §E (THREAD 4/4, AUDIO_THREAD 1/1, both TSAN-clean).
- Handoff: Phase 4 (persistence/recovery) inherits the crash-attribution → quarantine
  pipeline that P0-01 depends on; Phase 5 (plugin hosting) inherits the CLAP-vs-VST3
  parity gap (P2-01) and the release-supported-format decision.
