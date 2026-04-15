# Movie Playback Architecture

## Overview

Movie playback is a single mixed-domain operator: `MovieFile`. One graph node owns the media source, transport, loop state, audio output, video texture output, and diagnostics. The runtime stays generic; movie-specific decode, timing, buffering, and sync policy remain in operator-layer code.

The important runtime contract is that mixed-domain operators participate in both executors. The frame/GPU executor uses the frame instance for video presentation and texture upload. The audio executor uses a separate audio instance for realtime-safe audio rendering. Both instances share an operator-owned session keyed by `node_id`.

## Operator Surface

`MovieFile` exposes:

- Params: `file`, `play_mode`, `speed`, `volume`, `pitch_preserve`, `video_phase_offset_ms`
- Outputs: `texture`, `audio`, `time`, `duration`
- Diagnostics: `new_frames`, `reused_frames`, `nil_frames`, `decode_time_us`, `copy_time_us`, `upload_time_us`, `drift_ms`, `seek_corrections`, `seek_budget_exhausted`, `drop_repeat_corrections`, `buffered_ms`, `underruns`

There is no separate `MovieFileAudio` node, no `audio_time` input, and no graph-level AV sync bridge. Synced playback is internal to the one `MovieFile` session.

## Transport

`MovieFile` uses one authoritative transport per node:

- If the source has audio and the `audio` output is active, the audio read head is authoritative.
- If the source has no audio, the session uses video/self-clock timing.
- `video_phase_offset_ms` is a manual presentation offset applied only to video frame selection.
- Hidden learned phase correction is not part of the contract.

Looping is a session event. Audio and video wrap together through the same duration and generation state, stale queued frames are invalidated, and decode resumes around the wrapped target time.

## Decode Pipeline

Decoder selection is still format-driven:

- HAP uses the HAP decoder and compressed GPU upload path.
- Other movie formats use the AVFoundation decoder path.
- Static images still load through the image path and produce a texture without audio.

For AVFoundation video, requested frame times come from the session transport. AVFoundation may be used to acquire frames, but it is not the independent presentation clock for synced playback. The AVFoundation backend uses `AVQueuePlayer` + `AVPlayerLooper` for loop mode so non-HAP video stays pre-rolled across wrap instead of seeking to zero at end-of-item. The main thread does the small pixel-buffer acquisition step; CPU pixel copies run on the video decode worker; the GPU side uploads the newest ready decoded frame.

For HAP, frame selection follows the same session target time and uses direct compressed upload.

## Correction Policy

`MovieTransport::evaluate_correction()` provides a bounded three-tier policy:

| Tier | Condition | Action |
|------|-----------|--------|
| None | drift within normal frame jitter | No correction |
| DropRepeat | medium drift | Select/drop/repeat queued frames |
| Seek | source change, large drift, or persistent unresolved medium drift | Explicit reposition + queue flush |

Seeks are not the steady-state sync mechanism. Persistent medium drift escalates only after repeated unresolved drop/repeat decisions and still respects cooldown and seek budget.

## Diagnostics

`run_diagnostics` includes movie findings for:

- empty `file` params
- sustained nil frames
- sustained large drift
- repeated correction seek churn

There is no bridge-mismatch diagnostic because the public movie sync bridge no longer exists.

## Runtime Notes

Mixed-domain operator support is intentionally generic. A descriptor with both audio and GPU/frame capability gets cadence-specific instances in the compiled graph. The audio instance must remain realtime-safe: no AVFoundation calls, no synchronous decode, no allocation-heavy source changes, and no blocking seeks from the audio callback.
