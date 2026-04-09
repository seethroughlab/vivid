# Movie Playback Go / No-Go

Release gate for declaring the `MovieFile*` system production-ready on macOS.

This is the certification checklist for the current operator-owned movie playback architecture. A single failure in AV sync, audio cleanliness, or bridge integrity is a **No-Go**.

## Automated Gate

The following tests must pass on the current branch:

- `test_audio_frame_bridge`
- `test_movie_transport`
- `test_movie_playback_modes`
- `test_video_decode_worker`

Run:

```bash
ctest --test-dir build --output-on-failure -R "test_(audio_frame_bridge|movie_transport|movie_playback_modes|video_decode_worker)"
```

## Runtime Diagnostics Gate

Load `mfi_av_sync_demo.json`, let playback stabilize, then run the bundled `run_checks` payload in [movie-playback-runtime-gate.json](movie-playback-runtime-gate.json).

The runtime gate is **Pass** only if all of the following remain absent in a healthy synced graph:

- `movie_audio_bridge_mismatch`
- `movie_seek_churn`
- `movie_sustained_large_drift`
- `movie_sustained_nil_frames`

And the live state checks also pass:

- `nodes.vid.outputs.drift_ms.scalar <= 100`
- `nodes.audio.outputs.underruns.scalar == 0`

## Manual Gate

Run extended manual playback on:

- `graphs/io/movie_file/mfi_video_only.json`
- `graphs/io/movie_file/mfi_av_sync_demo.json`
- `graphs/filters/scanlines_demo.json`

Required outcomes:

- `mfi_video_only.json`: no visible cadence instability during a sustained run
- `mfi_av_sync_demo.json`: no audible glitching at `speed=1`, no visible AV desync
- `scanlines_demo.json`: no extra sync instability relative to the video-only baseline

Use the full procedure in [MOVIE-PLAYBACK-VALIDATION.md](MOVIE-PLAYBACK-VALIDATION.md), including:

- loop, once, and hold-last checks
- repeated source change and clear-file checks
- output window open/closed checks
- UI visible/hidden checks
- telemetry inspection and diagnostics

## Go / No-Go Decision

**Go** only if all of the following are true:

- all four automated tests pass
- the runtime diagnostics gate passes on a healthy AV-sync demo graph
- all three demo graphs pass the manual validation checklist
- `MovieFileAudio/time` and bridged `MovieFileIn/audio_time` stay aligned within the `50 ms` tolerance documented in [MOVIE-PLAYBACK-VALIDATION.md](MOVIE-PLAYBACK-VALIDATION.md)
- no audible glitches occur at unity speed with `pitch_preserve=1`
- no movie-specific diagnostic warnings appear in healthy playback

**No-Go** if any of the following are observed:

- audible glitches at unity speed
- visible AV desync during steady playback
- repeated seek corrections after startup
- bridge mismatch above the documented tolerance
- loop instability or stale frames after source change
- playback quality changes materially when the output window or node UI visibility changes

## Notes

- Scope is macOS-first production readiness.
- The public graph surface stays unchanged: `MovieFileIn`, `MovieFileAudio`, `audio_time`, `video_phase_offset_ms`.
- Failures against this gate should feed a separate remediation plan rather than ad hoc release exceptions.
