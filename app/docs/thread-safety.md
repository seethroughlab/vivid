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

## Checklist for new audio-reachable code

1. Does the audio thread only read atomics / pop SPSC / `try_lock`-copy? (no `new`,
   no blocking `lock`, no `std::string`/container growth in `process`).
2. Are UI-side edits done under the matching mutex, then a `gen.fetch_add(release)`?
3. New MCP/handler mutations: route through the **main-thread** apply path, not the
   server thread.
4. Run the change under `-DVIVID_SANITIZE_THREAD=ON` and exercise live editing.
