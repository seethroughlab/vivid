# ADR-0032: Audio I/O, Latency, and Export Are Product Surfaces

Status: accepted (all six decisions implemented across Phases A–E, merged to main 2026-08-10)

Date: 2026-07-26

## As built — complete (2026-08-10)

All six decisions are implemented and merged. Summary of where each landed:

- **#1 Device model** — `AudioDeviceManager` (PIMPL over miniaudio): enumerate/select/persist the output
  device, safe fallback + ADR-0019 health surfacing. Phase A (#320–#322).
- **#2 Input routing** — a duplex device with an `AudioInput` graph source op: hardware input is a
  monitored, reactive source (mic/line-in drives visuals). Phase D1/D2 (#328). *Deferred:* recording
  input to a clip (D3) — the input ring + op are exactly what it will record from.
- **#3 Latency reporting** — device output latency + VST3/CLAP plugin latency, published honestly through
  `get_health` (says "unknown" when a plugin doesn't report). Phase B (#323).
- **#4 Latency compensation** — opt-in playback plugin-delay compensation (PDC), default off (a live
  instrument stays low-latency). Compensable linear paths are aligned exactly; unknown-latency,
  live-input-monitored, and cross-track-routed tracks are honestly left live. Phase E1 (#330).
  *Deferred:* record-offset compensation (lands with D3) and dynamic plugin latency-change re-read.
- **#5 AV export** — extended the offline bounce to a deterministic audiovisual render locked to a
  synthetic clock with sample-accurate PTS; the offline visuals react to the bounced audio. Phase C
  (#325–#327). (The WAV half shipped first as Phase 1 below.)
- **#6 Failures visible** — device open/fallback/unsupported-rate + plugin-latency uncertainty + PDC
  state all surface through ADR-0019 health/log and the diagnostics panel (clip/peak shipped in Phase 1).

Deferred, non-blocking follow-ups (tracked, not part of this ADR's acceptance): audio-input
record-to-clip (D3), record-offset latency compensation, and dynamic plugin `kLatencyChanged` handling.

## As built — Phase 1: offline master bounce to WAV (2026-08-08)

Decision **#5** (offline export/bounce) and the clipping/peak half of **#6** (failures visible)
shipped as the ADR's stated first slice — "export the master mix to WAV first" — because a
deterministic offline render is the simpler proof of graph/transport correctness.

- **Offline render, not capture.** `bounce_session_to_wav` (`app/src/audio/audio_bounce.{h,cpp}`)
  loops `session_process` with a hand-advanced *local* beat clock — the same path the headless
  `test_session_executor` already drives — so the bounce uses the identical session graph + transport
  semantics as realtime playback, faster than realtime, with no device. Output is written via
  miniaudio's `ma_encoder` (32-bit float WAV; encoding was already compiled in — no new dependency).
- **Device-paused orchestration.** `run_audio_bounce` (`audio_bounce_app.cpp`) pauses the audio device
  (`ma_device_stop` blocks until the RT callback has returned, so the main thread owns the session with
  no race), bounces, sends an all-notes-off to reset voices the offline pass advanced, then resumes.
  The device handle reaches the app as an opaque `App::audio_device` (keeps `miniaudio.h` out of
  `app.h`).
- **Surfaces.** `File > Export Audio…` (30 s default) and the `export_audio` / `audio_export_status`
  MCP tools (`control_handlers_audio_export.cpp`). A bounce that exceeds 0 dBFS logs an
  `export clipped` warning through ADR-0019 and returns `clipped:true`. Length is an explicit
  `seconds` (primary) or `bars` (derived from tempo).
- **Range semantics (Phase-1 scope).** Renders the *current* arming from beat 0 for the requested
  length — like pressing play from the top; it does not replay a timeline of scene-launch events.
  Deterministic for native-op sessions; third-party VST3/CLAP plugins may render slightly differently
  offline. Verified end-to-end: unit test (`test_audio_bounce`, valid WAV / frame count / byte-identical
  determinism / clip flag / path safety) + a live-app MCP bounce (device pause/resume clean, valid file).

**Still open (later phases):** device model + selection + fallback (#1), input routing / recording
(#2), latency reporting + compensation (#3, #4), and audiovisual offline export (the visual graph is
still wall-clock-driven).

Extends [ADR-0003](ADR-0003-master-musical-transport.md),
[ADR-0004](ADR-0004-plugin-first-music-authoring.md), and
[ADR-0019](ADR-0019-nothing-fails-silently.md).

## Context

Vivid's audio engine is currently strongest as an internal session renderer: tracks, scenes, clips,
plugins, native ops, graph routing, analysis, mappings, and the master mix all exist. That is enough
to author and perform inside Vivid. It is not enough for a professional audio tool.

Professional-grade audio software has to treat the outside world as part of the product:

- selectable audio devices and stable device failure handling
- audio input routing, monitoring, and recording
- latency reporting and compensation
- clear sample-rate/block-size behavior
- export/bounce of the work product
- diagnostics when any of the above cannot be honored

Some of these are less central to Vivid's audiovisual thesis than graph routing or mappings, but they
are the features that decide whether someone can trust Vivid in a show, a studio handoff, or an
installation.

## Decision

Make audio device I/O, latency, and export explicit product surfaces rather than incidental details
of the miniaudio callback.

1. **Add an audio device model.** Persist the selected output/input device identity, requested sample
   rate, buffer size, and fallback policy. If the chosen device is unavailable, open a safe fallback
   and surface the reason through health/log UI.

2. **Expose input routing.** Add input channels as graph-addressable sources with monitoring controls.
   Recording from hardware input should be a first-class path for audio tracks, not a special case
   hidden under the device callback.

3. **Track latency honestly.** Publish device latency, plugin-reported latency where available, and
   host-added buffering. If a latency number is unknown, say unknown. Do not fabricate precision.

4. **Compensate where the graph contract allows it.** Start with record-offset compensation and
   plugin-delay compensation for linear paths. For arbitrary graph routing, define which edges can be
   compensated exactly, which need conservative alignment, and which are intentionally left live.

5. **Add offline export/bounce.** Export the master mix to WAV first, then extend to audiovisual
   export. Offline render must use the same session graph and transport semantics as realtime render,
   with deterministic scene/clip launch behavior.

6. **Make export and device failures visible.** Failed device open, fallback selection, unsupported
   sample-rate change, clipped export, and plugin latency uncertainty all report through ADR-0019's
   health/log surfaces.

## Alternatives Considered

- **Leave I/O and export out of scope because Vivid is not a traditional DAW.** Rejected. Vivid is
  not trying to become Logic or Pro Tools, but live audiovisual work still needs reliable I/O and a
  way to produce an artifact.
- **Build export only as screen/audio capture.** Rejected as the first step. Capture is useful, but
  deterministic offline audio export is the simpler proof of graph/transport correctness.
- **Promise full plugin-delay compensation immediately.** Rejected. Arbitrary graph routing and live
  performance have real constraints. The product should be honest about which paths are compensated.

## Consequences

- **Positive:** Vivid becomes usable in more real production contexts: performance rigs,
  installations, and handoff/export workflows.
- **Positive:** Device and latency behavior stop being hidden runtime trivia and become inspectable
  state agents can reason about.
- **Tradeoff:** Audio I/O settings introduce platform-specific complexity and more failure modes.
- **Follow-up:** Add device-enumeration MCP tools, an audio settings panel, loopback latency tests,
  offline-render tests, and a clipping/peak report for exported files.

