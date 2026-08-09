# Audio-thread safety — rules & patterns

**Read this before touching `audio/` or anything the audio callback reaches.** The
audio callback runs on a real-time thread: it must never block, allocate, or take a
contended lock. Vivid's engine keeps that guarantee with three patterns. Verify
changes under ThreadSanitizer: `cmake -S app -B build -DVIVID_BUILD_APP=ON -DVIVID_SANITIZE_THREAD=ON`.

## The three threads

| Thread | Runs | May it block/alloc? |
|---|---|---|
| **Audio** (`audio/audio_callback.cpp`, miniaudio) | render session, advance transport, publish analysis | **No** — RT-critical |
| **UI / main** (`app/frame.cpp`, CFRunLoopTimer) | all rendering + all edits + applied MCP commands | yes |
| **Control server** (`cli/control_server.cpp`, cpp-httplib) | accept HTTP, enqueue, await promise | yes (its own thread) |

Invariant: **every mutation of the session/graph happens on the UI thread.** The
audio thread only *reads* (via the channels below); the control-server thread only
*enqueues* — the main loop applies the work (`control.process_pending`).

## Pattern 1 — Transport atomics (audio → frame)

`Transport` exposes `beats`, `bpm`, `level`, `band_low/mid/high`, `transient` as
`std::atomic` (relaxed). The audio callback `store()`s them per block; the frame loop
`load()`s them to drive the visuals. No locks, single producer/consumer per field.

## Pattern 2 — SPSC param queue (UI → audio)

Plugin param changes use a lock-free single-producer/single-consumer queue per VST3
handle: the UI thread `param_q.push(id, value)` (e.g. from a knob drag or an MCP
`set_param`); the audio thread `param_q.pop()` at the top of `process` and applies
them. The UI thread also mirrors the value into the plugin's `IEditController` for its
GUI — that's UI-thread-only.

## Pattern 3 — Generation counter + non-blocking `try_lock` copy (UI → audio)

Used for **structural** edits the audio thread can't take piecemeal — MIDI clips and
the FX chain. The shape (see `audio/vst3_host.cpp`):

```
// UI thread (an edit):
{ std::lock_guard lk(edit_mtx); edit_clips[sc] = …; }   // mutate the EDIT copy
edit_gen.fetch_add(1, std::memory_order_release);        // publish "there's a change"

// Audio thread (per process block):
if (edit_gen.load(acquire) != edit_gen_seen) {           // cheap: usually false
    if (edit_mtx.try_lock()) {                           // NON-blocking — skip if UI holds it
        … copy edit_clips → live clips element-wise …    // addresses stay stable
        edit_gen_seen = edit_gen.load(acquire);
        edit_mtx.unlock();
    }                                                    // else: try again next block
}
```

Key points:
- The audio thread **never waits**: `try_lock` fails → it keeps using the current
  live data and retries next block. A dropped frame of latency on an edit is fine;
  a blocked audio thread is not.
- `fx_mtx`/`fx_gen` mirror this for the effect chain; removed effects go to a
  `fx_retired` list and are freed at shutdown (never in `process`).
- The `release`/`acquire` pair is what makes the edited data visible to the audio
  thread when it observes the new generation.

## Cross-thread channels (the enumerated contract)

Every channel that crosses a thread boundary, with the ordering invariant it relies on. The three
patterns above are the *mechanisms*; this is the *inventory* — review a change against it, and add a row
when you add a channel (ADR-0029).

| Channel | Producer → Consumer | Mechanism | Ordering invariant |
|---|---|---|---|
| Transport scalars (`beats`/`bpm`/`level`/bands/`transient`) | audio → frame | `std::atomic`, relaxed per field | single writer + reader per field; no cross-field consistency needed |
| Plugin param queue (per VST3/CLAP handle) | UI → audio | lock-free SPSC ring | producer `push`, consumer `pop` at top of `process`; full → drop (never blocks audio) |
| Structural edits — four independent per-`Track` mirrors: MIDI clips (`edit_gen`/`edit_mtx`), VST3 FX chain (`fx_gen`/`fx_mtx`, retired→`fx_retired`), native-op FX chain (`op_fx_gen`/`op_fx_mtx`), graph plan/binds (`ggen`/`gmtx`) | UI → audio | generation counter + `try_lock` copy | edit under the mutex, then `gen.fetch_add(release)`; audio `acquire`-loads `gen`, `try_lock`-copies or retries next block |
| Live MIDI ring (`LiveMidi`) | UI + CoreMIDI → audio | SPSC ring; producers serialize on `push_mtx`, consumer lock-free | `head`/`tail` release/acquire; full → drop |
| Node-analyze mask (gated per-node FFT) | UI → audio | `std::atomic<uint64_t>`, release/acquire | ring allocated on the UI thread **before** the release-store; audio `acquire`-loads the mask, then reads the now-stable ring |
| Active-notes bus (`note_bus`) | frame → render-thread op | atomic note slots + `count`/`track_id` release/acquire | `track_id` published **last** gates `count`+notes; each note is one atomic word, so a torn snapshot mixes whole notes (well-defined — ADR-0029) |
| Spectrum sample ring (`MeterState::an_ring`) | audio → frame | `AnalysisRing` — atomic-float slots + `pos` release/acquire | push writes the slot (relaxed) then `pos` (release); snapshot loads `pos` (acquire) then the slots — a lapped slot tears benignly, well-defined (ADR-0029) |
| Modulator control-out (`ctl_pub[]`) | audio → frame | `std::atomic<float>`, relaxed per node | single writer + reader |
| Per-track note scalars (`note_pitch`/`note_vel`/`note_gate`) | audio → frame | `std::atomic<float>`, relaxed | single writer + reader; values HELD until the next note |
| Control-server work | server thread → UI | enqueue + `std::promise`, applied on the main loop | **all** session/graph mutation happens on the UI thread |
| Per-node scope / FFT ring bank (`node_scope`, `node_an`) | audio → frame | `NodeRingBank` — atomic-float slots + release/acquire per-node head | lapped slot tears benignly, well-defined (ADR-0029) |
| Held-note set (`held`) | audio → frame | `HeldNoteSet` — atomic packed {pitch,vel} slots + release/acquire `count` | swap-remove rewrites a slot; a torn snapshot mixes whole notes (ADR-0029) |
| Note-event ring + bus (`note_event_ring.h` → `note_event_bus.cpp`) | audio → frame → render op | SPSC `NoteEventRing` drained on the frame, republished into a **double-buffered** `EvChannel` | ring `w`/`r` release/acquire, full → drop-newest; bus writes buf+`count` then flips `active` (release) then `track_id` **last** — reader gates on `track_id`+`count` (ADR-0029) |
| Master spectrum band bus (`spectrum_bus.cpp`) | frame → render-thread op | `std::atomic<uint32_t>` band slots (float-bits) + `count` release/acquire | bands written (relaxed) then `count` (release); reader loads `count` (acquire) then bands — distinct from the audio→frame `an_ring` |
| Movie audio bus (`movie_audio_bus.cpp`) | render → **audio** | SPSC stereo ring + `epoch` reset guard | the only inbound-to-audio channel; `write_pos`/`read_pos` release/acquire; `read()` re-checks `epoch` to drop a block if a reset raced |
| Recording tap ring (`Transport::rec_ring_`) | audio → main | SPSC ring + `rec_active_` gate | ring allocated **before** the `rec_active_` release-store; `rec_write_`/`rec_read_` release/acquire; full → drop (once-per-session `stderr` overrun log — RT hygiene TODO) |
| Live output capture (`Transport::capture_l_/capture_r_`) | audio ↔ main | `capture_enabled_` gate + `capture_mtx_` **`try_lock`** | audio thread `try_lock`s to fill the buffer (skips if UI holds it — never blocks); main reads under `lock_guard` |

### Benign torn reads — and hardening them

A few *array-snapshot* channels let the consumer read a buffer the producer may be mid-writing (the
per-track/master analysis rings, the per-node scope/FFT rings, and the held-note array). A torn read is a
1-frame visual glitch, explicitly acceptable. But when the array is **plain memory**, that concurrent
read/write is a technical data race (UB) that ThreadSanitizer rightly flags.

All five such channels are now hardened this way (ADR-0029), each a header-only atomic-slot type with a
portable `THREAD`-leg race test:
- **active-notes bus** — each note slot a `std::atomic<uint64_t>` (pitch+velocity packed). `note_bus.cpp`.
- **spectrum sample ring** — `MeterState::an_ring`, an `AnalysisRing` of atomic-float slots. `analysis_ring.h`.
- **per-node scope + FFT rings** — `node_scope` / `node_an`, a `NodeRingBank` of per-node atomic-float
  rings over `unique_ptr<atomic<float>[]>` (since `vector<atomic>` isn't movable). `node_ring_bank.h`.
- **held-note set** — `held`, a `HeldNoteSet` of atomic packed-{pitch,vel} slots. `held_note_set.h`.

Every torn read is now well-defined (whole values from adjacent frames, never a half-written one) and
TSan-clean. Add a new such channel the same way — atomic array elements, not a lock — with a `THREAD`
race test.

## Realtime health counters (ADR-0031)

The audio thread also **reports** when realtime went wrong, so the failure is visible instead of merely
avoided. `audio/audio_health.h` (`vivid::audio::health`) holds process-global relaxed-atomic counters —
callbacks, render bail-to-silence (oversized blocks), over-budget callbacks, skipped `try_lock` handoffs —
plus last/max callback-µs gauges. Single-writer (the RT callback, and the `session_process` it calls on
that thread) / multi-reader (the frame thread). Relaxed atomics are enough: these are monotonic tallies,
not a synchronization channel, so every writer path stays lock/alloc/syscall-free, exactly like the
snapshot channels above. Callback timing uses `std::chrono::steady_clock` (`mach_absolute_time` — no
syscall), the same RT-safe timing the plugin watchdog already relies on.

The one subtlety is the **RT-scope gate**: the offline bounce calls `session_process` directly, off the RT
thread, so the in-`session_process` handoff-skip counter is gated on a `thread_local` set only by
`audio_callback` (`health::RtScope` / `in_rt()`). A bounce never pollutes the realtime-contention metric.
`collect_health` (`app/runtime_health_collect.cpp`) rolls per-frame deltas into `HealthSnapshot`, so the
counters surface through the existing ADR-0019 health path (diagnostics panel + `get_health`) with no new
surface. Budgets (max block, callback multiplier, bailout Error threshold) live in `audio/audio_budgets.h`,
read once and warmed on the main thread like `watchdog_config()`. Add a new RT counter the same way —
a relaxed atomic in `audio_health.h`, incremented on the RT thread — and if it needs a threshold, put it
in `audio_budgets.h` so tests and health reporting read the same value.

## ThreadSanitizer is a gate, not a ritual (ADR-0029)

TSan only finds a race on code it actually **runs with threads racing**, so the model is verified by tests
that drive a channel from two threads, not by reading the code.

- **CI leg `tests (thread-sanitizer)`** builds with `-DVIVID_SANITIZE_THREAD=ON` (`VIVID_BUILD_APP=OFF`)
  and runs the **`THREAD`-labelled** tests (`ctest -L THREAD`) under `TSAN_OPTIONS=halt_on_error=1` — TSan's
  value is racing tests, and the package/dlopen tests SEGV under it (loading a non-instrumented dylib), so
  the leg opts in the concurrency tests rather than the whole suite. `test_note_bus` races a publisher
  against a reader over the active-notes bus, so a regression that drops the bus's atomics (or its
  ordering) turns the leg red. Give a new threaded test the `THREAD` label to join.
- **macOS CI gate `tests (audio-thread-sanitizer)`** closes the loop on the audio thread itself. The
  engine is macOS + heavy-dep, so this runs on the self-hosted runner: it TSan-builds only
  `test_session_concurrency` (`-DVIVID_SANITIZE_THREAD=ON`, app-ON for the audio deps) and runs
  `ctest -L AUDIO_THREAD` under `halt_on_error=1`. That harness races a **render thread** (looping
  `session_process`, in `health::RtScope` so it plays the RT role) against a single **UI/mutator thread**
  (add/remove tracks, edit clips, mutate the graph, set params, launch scenes, connect/disconnect audio +
  control graph edges, remove nodes, and read the published snapshots) — the actual gen-counter / `try_lock`
  / SPSC / atomic-ring surface (ADR-0031 §1 added the graph-edge churn). No third-party suppression list is
  needed: `session_process` starts no
  device / GPU / CLAP thread, so the only threads are the harness's two. `test_session_executor` (its
  single-threaded sibling — one thread, so TSan finds nothing) stays on the plain `audio-engine-tests` job.
  A surviving report is triaged per decision #4 (real → harden; benign → a named suppression + a table row).

## Checklist for new audio-reachable code

1. Does the audio thread only read atomics / pop SPSC / `try_lock`-copy? (no `new`,
   no blocking `lock`, no `std::string`/container growth in `process`).
2. Are UI-side edits done under the matching mutex, then a `gen.fetch_add(release)`?
3. New MCP/handler mutations: route through the **main-thread** apply path, not the
   server thread.
4. Run the change under `-DVIVID_SANITIZE_THREAD=ON` and exercise live editing.
5. **A new cross-thread channel isn't done until** it has a row in the channel table above (with its
   ordering invariant) **and** a headless test that drives it concurrently, so the TSan CI leg exercises
   it (ADR-0029). If it's an array snapshot, make the elements atomic — don't ship a plain-memory race.
