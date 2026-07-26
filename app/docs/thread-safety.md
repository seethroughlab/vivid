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
| Structural edits (MIDI clips, FX chain, graph binds) | UI → audio | generation counter + `try_lock` copy | edit under the mutex, then `gen.fetch_add(release)`; audio `acquire`-loads `gen`, `try_lock`-copies or retries next block |
| Live MIDI ring (`LiveMidi`) | UI + CoreMIDI → audio | SPSC ring; producers serialize on `push_mtx`, consumer lock-free | `head`/`tail` release/acquire; full → drop |
| Node-analyze mask (gated per-node FFT) | UI → audio | `std::atomic<uint64_t>`, release/acquire | ring allocated on the UI thread **before** the release-store; audio `acquire`-loads the mask, then reads the now-stable ring |
| Active-notes bus (`note_bus`) | frame → render-thread op | atomic note slots + `count`/`track_id` release/acquire | `track_id` published **last** gates `count`+notes; each note is one atomic word, so a torn snapshot mixes whole notes (well-defined — ADR-0029) |
| Spectrum sample ring (`MeterState::an_ring`) | audio → frame | `AnalysisRing` — atomic-float slots + `pos` release/acquire | push writes the slot (relaxed) then `pos` (release); snapshot loads `pos` (acquire) then the slots — a lapped slot tears benignly, well-defined (ADR-0029) |
| Modulator control-out (`ctl_pub[]`) | audio → frame | `std::atomic<float>`, relaxed per node | single writer + reader |
| Per-track note scalars (`note_pitch`/`note_vel`/`note_gate`) | audio → frame | `std::atomic<float>`, relaxed | single writer + reader; values HELD until the next note |
| Control-server work | server thread → UI | enqueue + `std::promise`, applied on the main loop | **all** session/graph mutation happens on the UI thread |
| Per-node scope / FFT rings (`node_an_ring`, `node_scope`) + held-note set (`held_[]`) | audio → frame | plain array snapshot + atomic `count`/`pos`/mask | **benign torn read** — not yet atomic-hardened; see below |

### Benign torn reads — and hardening them

A few *array-snapshot* channels let the consumer read a buffer the producer may be mid-writing (the
per-track/master analysis rings, the per-node scope/FFT rings, and the held-note array). A torn read is a
1-frame visual glitch, explicitly acceptable. But when the array is **plain memory**, that concurrent
read/write is a technical data race (UB) that ThreadSanitizer rightly flags.

Two channels are now hardened this way (ADR-0029): the **active-notes bus** (each note slot a
`std::atomic<uint64_t>`, pitch+velocity packed) and the **spectrum sample ring** (`MeterState::an_ring`,
an `AnalysisRing` of atomic-float slots — see `audio/analysis_ring.h`). Both torn reads are now
well-defined (whole values from adjacent frames, never a half-written one) and TSan-clean, each covered by
a portable concurrent test (`test_note_bus`, `test_analysis_ring`) on the `THREAD` leg. Extend the same
pattern — atomic array elements, not a lock — to the **remaining** plain-memory channels: the per-node
`node_an_ring` / `node_scope` rings and the `held_[]` note set. These are the fiddly ones — the per-node
rings are lazily-allocated `std::vector`s (so they need a `unique_ptr<atomic<float>[]>` rather than
`vector<atomic>`, which isn't movable), and `held_[]` is a 12-byte-element set (no single-word pack).

## ThreadSanitizer is a gate, not a ritual (ADR-0029)

TSan only finds a race on code it actually **runs with threads racing**, so the model is verified by tests
that drive a channel from two threads, not by reading the code.

- **CI leg `tests (thread-sanitizer)`** builds with `-DVIVID_SANITIZE_THREAD=ON` (`VIVID_BUILD_APP=OFF`)
  and runs the **`THREAD`-labelled** tests (`ctest -L THREAD`) under `TSAN_OPTIONS=halt_on_error=1` — TSan's
  value is racing tests, and the package/dlopen tests SEGV under it (loading a non-instrumented dylib), so
  the leg opts in the concurrency tests rather than the whole suite. `test_note_bus` races a publisher
  against a reader over the active-notes bus, so a regression that drops the bus's atomics (or its
  ordering) turns the leg red. Give a new threaded test the `THREAD` label to join.
- **Next phase:** the full audio engine is macOS + heavy-dep (`VIVID_BUILD_APP=ON`), so its concurrency
  lives in `test_session_executor`. Running *that* under TSan (and hardening the remaining benign-torn
  rings above) is the follow-up that closes the loop on the audio thread itself; it needs a curated
  suppression list for the third-party libraries (miniaudio/wgpu/GLFW) first.

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
