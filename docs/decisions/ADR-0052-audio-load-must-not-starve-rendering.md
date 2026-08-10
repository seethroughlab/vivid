# ADR-0052: Don't Block the Render Thread on Per-Frame O(N) Work

Status: accepted (corrected)

Date: 2026-08-07

> **Correction (2026-08-07).** This ADR was first written to conclude that hosted-audio CPU load was
> *starving the render thread* via core-scheduling contention, and proposed a plugin voice governor +
> render-thread QoS. **That diagnosis was wrong — it was confounded by transient machine load.** A
> clean measurement on a quiet machine found the real cause: a per-frame O(N) scan of the entire audio
> clip in the *UI thread* (waveform preview). The ADR is retained, corrected, because (a) its audit of
> the audio architecture stands and is worth keeping, and (b) the corrected finding yields a real,
> durable invariant. The original decision (voice governor / QoS) is **withdrawn** — see below.

## Context

The investigation began from: the `grid` demo (2 Surge XT instances) rendered far below the
audio-free `geometry` demo, and "a handful of synth tracks should never cost visual framerate."

**What the audit got right — the audio architecture is sound.** This part holds and is the reason to
keep the ADR:

- Audio runs on **miniaudio's own real-time CoreAudio thread**; the device is started once
  (`app/src/main.cpp:297-335`) and the callback (`app/src/audio/audio_callback.cpp:17-103`) does all
  DSP there. `session_process` (`app/src/audio/vst3_host.cpp:2216`) is never called from the frame loop.
- All plugin `process()` calls run on that RT thread holding **no session lock**; the RT thread only
  `try_lock`s and drops on contention, so heavy DSP **cannot block** the render thread.
- The audio→visual bridge the frame loop reads (`transport.level/transient/band_*`, `beats`) is
  **lock-free relaxed atomics + SPSC rings** (`app/src/transport.h`, read at `app/src/app/frame.cpp`).
- Present is `WGPUPresentMode_Fifo` (vsync); there is no semaphore, condition variable, or audio-buffer
  wait anywhere in the present/frame path.

**What the audit got wrong — the diagnosis.** The original ADR concluded the drop was *soft
CPU/core-scheduling contention* (the RT audio thread preempting the render thread). Every number
behind that conclusion (34/20/18fps, "process at ~74% of one core, descheduled") turned out to be
**transient contention from other processes on the dev machine** (concurrent builds, indexing),
not audio preempting the renderer. Three independent checks falsify the audio-coupling theory:

1. **Grid playing == grid stopped** (~46 ≈ 47fps). If audio DSP were stealing render time, stopping
   playback would recover it. It didn't.
2. **Multi-core audio parallel == serial** (PR #283 A/B, no FPS change). If the single serial audio
   loop were the bottleneck, fanning it across cores would help. It didn't.
3. **Quiet-machine thread sample:** ~99% of the main-thread frame (705/713 samples) was in
   `draw_ui → draw_clip_preview → session_audio_waveform` — a **UI** call, on the **render** thread,
   with playback state irrelevant.

**The real root cause.** `session_audio_waveform()` recomputes 48 peak bins by scanning the **entire**
audio clip (millions of samples) — and the session view called it **every frame** for every audio-clip
cell. The PCM never changes between frames. The frame was **render-work-bound on redundant UI work**,
not vsync- or audio-blocked. Fixed in **PR #284**: cache the peak-per-bin result on `AudioClip`
(`mutable wave_bins_`, invalidated on bin-count or sample-data change), UI-thread only, no sync.
**Grid 46 → ~73fps** (audio-free ceiling ~79); `draw_clip_preview` fell from 705 samples to 3.

## Decision

**Invariant: the render/frame thread must not perform per-frame work whose cost scales with content
size (O(N) over PCM, geometry, file data, …).** Such work must be cached and invalidated on change, or
moved off the render thread. Redundant per-frame recomputation — not audio load — is the demonstrated
threat to framerate.

Concretely:

- Per-frame UI previews (waveforms, spectra, thumbnails) **cache their derived result** and rebuild only
  on a real input change (size/ptr/hash), as PR #284 does for the clip waveform.
- Watch for the same shape elsewhere in the immediate-mode UI: MIDI-clip previews, node-graph
  waveforms, any preview that re-derives from raw content each frame.
- The residual grid gap (73 vs 79fps) is the general immediate-mode `Renderer2D` re-emitting all UI
  vertices per frame — inherent, broad, and low-leverage; not a single pathological scan. Batch/persist
  only if a future profile shows it dominating.

**Withdrawn from the original decision.** The plugin voice/oversampling governor and the render-thread
QoS promotion are **not adopted** — they targeted a mechanism that does not exist here. They would have
changed how patches sound (the governor) or added scheduling complexity (QoS) to fix a non-problem.
Re-propose only if a *measured* audio-preemption case ever appears.

## Related: multi-core audio (PR #283)

The track-parallel audio engine prototyped during this investigation is **valid, but it is not a
render-FPS fix** (see falsification #2). It is retained and merged purely as an **audio-headroom** win:
many-plugin sessions can fan tracks across cores. Kept decoupled from anything render-side.

## Consequences

- **Positive:** the grid FPS problem is actually fixed (46 → 73fps) by a cheap, low-risk cache — no
  plugin behavior change, no RT-thread scheduling change.
- **Positive:** a correct, generalizable invariant (no per-frame O(content) work on the render thread)
  replaces a wrong one (audio starves rendering), so future previews don't reintroduce the bug.
- **Positive/record:** the audio-architecture audit is preserved and confirmed — audio is genuinely
  decoupled from rendering, which is *why* the audio-coupling theory was falsifiable.
- **Process lesson:** the original conclusion rested on FPS numbers taken on a **loaded** machine. Perf
  root-causing requires a quiet machine and a thread sample pointing at a specific frame; A/B toggles
  (play/stop, parallel/serial) are cheap falsification tests — run them before theorizing a mechanism.
