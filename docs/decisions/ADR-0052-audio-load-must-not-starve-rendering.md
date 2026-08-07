# ADR-0052: Audio Load Must Not Starve Rendering

Status: proposed

Date: 2026-08-07

> **Origin.** Raised by a live performance investigation: the `grid` demo rendered at ~34fps while the
> visually-equivalent, audio-free `geometry` demo ran at the 59fps vsync cap. Every claim below was
> measured on a running app (`get_perf`, thread sampling) and cross-checked against the code by three
> independent explorations of the audio thread model, the plugin path, and the render↔audio coupling.

## Context

A handful of synth tracks should never cost visual framerate. In Vivid they do:

- `geometry` demo (real wireframe geometry, **0 audio tracks**): **59fps** (60Hz FIFO/vsync cap).
- `grid` demo (the *same* kind of geometry + **2 Surge XT** synth instances): **34fps**.

The visuals are not the cause — identical geometry with no audio hits the vsync cap, and output
resolution (2× supersample → 0.5×) made no difference, confirming the render is cheap and GPU-bound.

**The audio architecture is correct.** The investigation found no structural coupling to remove:

- Audio runs on **miniaudio's own real-time CoreAudio thread**; the device is started once
  (`app/src/main.cpp:297-335`) and the callback (`app/src/audio/audio_callback.cpp:17-103`) does all
  DSP there. `session_process` (`app/src/audio/vst3_host.cpp:2216`) is never called from the frame
  loop.
- All plugin `process()` calls (`app/src/audio/vst3_host_render.cpp` — VST3 `:121/:172`, CLAP
  `clap_run` `:201/:222`) run on that RT thread, holding **no session lock** (`process_step` at
  `vst3_host.cpp:2599` is outside every lock region).
- The audio→visual bridge the frame loop reads (`transport.level/transient/band_*`, `beats`) is
  **lock-free relaxed atomics + SPSC rings** (`app/src/transport.h:17-22`, read at
  `app/src/app/frame.cpp:257-265`). The RT thread only ever `try_lock`s the session mutexes and drops
  on contention (`vst3_host.cpp:2202/2232/2244/…`), so heavy DSP can never block the render thread.
- Present is `WGPUPresentMode_Fifo` (vsync). There is no semaphore, condition variable, or
  audio-buffer wait anywhere in the present/frame path.

**So the coupling is soft: CPU / core-scheduling contention, not a lock or a sync gate.** The
mechanism:

1. `34fps ≈ 29ms/frame` is **not** a clean vsync divisor (30 would be), so the frame is
   **wall-time-bound**, not vsync-bound — ~12ms of extra wall time lands on the render thread per
   frame when the plugins run.
2. Live sampling showed the process at only **~74% of one core** (not saturated) with the main thread
   hot in UI rendering + **per-frame GPU buffer creation** (`wgpu StagingBuffer::new` / `create_buffer`
   in `Renderer2D`). A 74%-of-one-core process running at 34fps means the render thread is being
   **descheduled**, not out-computed.
3. The two Surge instances have **spiky, uncapped DSP** on the **high-priority** RT audio thread. The
   audio period was deliberately raised to **1024 frames (~23ms)** (`app/src/main.cpp:309`) to absorb
   Surge voice-stacking cost spikes. When that RT thread bursts, the OS preempts the normal-priority
   render/main thread mid-frame, stretching the frame from ~16ms to ~29ms → missed vsync deadlines.

The **root architectural gap**: Vivid imposes **no polyphony, voice, oversampling, or CPU governor on
hosted plugins**. Plugin creation passes only sample rate and max block size
(`app/src/audio/vst3_host_clap_loader.cpp:56-116`); Surge runs at whatever its patch specifies (default
16 voices, often + unison + higher-quality oscillators). Nothing scales that load down, and the render
thread has no scheduling protection from it. As the tool takes on real songs (many tracks, rich
patches), rendering will degrade unboundedly — the perception loop, live authoring, and showcase
capture all depend on a stable framerate.

## Decision

**Adopt the invariant: hosted-audio CPU load must not be able to starve the render thread.** Enforce it
on two independent axes so neither is a single point of failure:

1. **A hosted-plugin performance governor (audio side).** Cap and, under sustained overrun, scale down
   the DSP cost of hosted synths so no patch can monopolize the RT thread:
   - Set a **voice ceiling** and disable oversampling on load where the plugin exposes it via CLAP/VST3
     params (Surge XT does), at `vst3_host_clap_loader.cpp` load time — a sane default (e.g. 8 voices,
     no oversampling) with a per-track override.
   - Extend the existing over-budget watchdog (ADR-0045) from *fault isolation* to *graceful
     degradation*: when a plugin trends over its RT budget, first **reduce its voice cap** (a param
     push, RT-safe) before the hard disable, and surface it as a toast.

2. **Render-thread scheduling protection (render side).** Give the frame/present thread a QoS that the
   RT audio thread cannot indefinitely preempt on contention — set the main/render thread to
   `USER_INTERACTIVE` QoS (macOS `pthread_set_qos_class_self_np`) at frame-loop start, so the scheduler
   keeps a render slice under audio bursts. Validate that this does not regress audio (the audio thread
   remains real-time priority; this only raises render from default).

Additionally, **remove the per-frame GPU buffer churn** the profile exposed (reuse persistent
uniform/vertex/staging buffers in `Renderer2D` and any op that recreates buffers each frame) so the
render frame has budget headroom — with margin under 16ms, an occasional preemption no longer tips the
frame past the vsync deadline.

These are complementary: (1) bounds the aggressor, (2) protects the victim, (3) widens the margin.

## Alternatives Considered

- **Tighten / remove locks between audio and render.** Rejected — the investigation proved there is no
  lock coupling: the RT thread never blocks on a session mutex during DSP, and the bridge is lock-free.
  There is no lock to remove.
- **Lower the audio period (1024 → 512/256).** Reduces burst length and preemption impact, but the
  period was raised to 1024 *specifically* to stop Surge voice-stacking spikes from overrunning the
  deadline and crackling. Lowering it trades framerate for audio glitches. Worth testing as a tunable,
  but not the primary fix.
- **Just ship lighter demo patches.** Fixes the `grid` demo but not the class of problem — the next
  real song with rich patches regresses again. It is a good *immediate* mitigation for the current
  demos (and pairs with replacing the heavy bass patch), not the architectural answer.
- **Cap the frame rate to 30fps and stop fighting it.** Unacceptable: the perception/eval loop, live
  authoring feel, and 1080p showcase capture all want a stable 60.
- **Do nothing / accept it.** Explicitly rejected by the product need ("a few tracks can't tank the
  fps").

## Consequences

- **Positive:** framerate becomes resilient to audio load — the defining property we want. A single
  heavy patch, or many tracks, can no longer starve rendering; the perception loop and showcase capture
  stay stable.
- **Positive:** the buffer-reuse cleanup is a straight render-thread win independent of audio.
- **Tradeoff:** a voice/oversampling cap changes how the heaviest patches *sound* (fewer simultaneous
  voices, no oversampling). Mitigated by a sane default + per-track override, and by the fact that the
  degradation is graceful (reduce before disable) and surfaced.
- **Tradeoff:** raising render-thread QoS must be validated against audio — done wrong it could steal
  from the RT thread and cause dropouts. Requires measuring both framerate *and* audio-callback overrun
  after the change.
- **Follow-up:** prototype the highest-leverage lever first (voice-cap governor **or** render QoS) and
  measure the actual FPS recovery on the `grid` demo before committing to both; then the buffer-reuse
  pass. Consider exposing an app-level "performance profile" (voice ceiling, oversampling, target fps)
  once the mechanism exists. Update the stale `audio_callback.h:4-6` doc (still references the removed
  test-tone path) while in this code.
