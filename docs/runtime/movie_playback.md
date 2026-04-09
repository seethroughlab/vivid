# Movie Playback Architecture

## Overview

The movie playback system lives entirely in operator-layer code. It does not extend runtime core with movie-specific services, managers, or transport abstractions. The runtime provides generic operator execution (frame/audio cadences), cadence bridging, and the diagnostics framework; movie-specific decode, timing, and sync logic are owned by the operators and their shared support code.

## Operator Model

Two independent operators provide the public graph surface:

- **MovieFileIn** (GPU cadence, ~60 Hz) -- decodes video, uploads textures, publishes `time` and `duration` outputs
- **MovieFileAudio** (Audio cadence, ~48 kHz) -- decodes audio into a ring buffer, publishes `time` and `duration` as SIGNAL outputs

The operators are separate `.dylib` modules loaded by the graph runtime. They communicate via the `audio_time` port bridge: MovieFileAudio's `time` output crosses the cadence bridge (~60 Hz snapshot) and arrives at MovieFileIn's `audio_time` input.

The production AV-sync contract is:

- `MovieFileAudio/time` is the authoritative movie clock in synced playback
- the cadence bridge must deliver that scalar to `MovieFileIn/audio_time` without port-index drift or stale-value skew
- `MovieFileIn` may apply a bounded internal steady-state phase calibration to absorb fixed system latency, but it must not invent an independent transport clock

## Shared Session Dylib

Both operator dylibs link against `libmovie_session.dylib`, deployed to `Vivid.app/Contents/Frameworks/`. The dylib is loaded automatically by `dyld` when either operator is `dlopen`'d (via `@loader_path/../Frameworks` RPATH). No graph loader changes are needed.

### Components (`operators/shared/movie_session/`)

| File | Purpose |
|------|---------|
| `movie_transport.h/.cpp` | `MovieTransport` -- transport time computation, seek/correction policy, source generation |
| `playback_session.h/.cpp` | `PlaybackSession` -- per-operator session wrapping a MovieTransport |
| `session_registry.h/.cpp` | `PlaybackSessionRegistry` -- global singleton, operator-id-keyed session cache with ref counting |
| `decoded_frame_queue.h/.cpp` | `DecodedFrame` struct + bounded `DecodedFrameQueue` (max 3 frames) |
| `video_decode_worker.h/.cpp` | `VideoDecodeWorker` -- background thread for AVF pixel copy, direct queue push for HAP |

Sessions are keyed by the runtime graph node id exposed to operators through `VividFrameContext`, `VividAudioContext`, and `VividGpuContext` as `node_id`. Two nodes that happen to reference the same media file remain independent; they do not share transport state implicitly.

## Decode Pipeline

### Decoder Selection (`operators/shared/movie_decode/`)

`DecoderFactory` probes the file's codec:
- **HAP** (BC1/BC3/BC4) -- uses `HAPDecoder` (AVAssetReader-based, no main-thread requirement)
- **H.264/HEVC/other** -- uses `AVFDecoder` (AVPlayer-based, main-thread AVF API calls)

### Async Decode (AVF Path)

AVFoundation's `copyPixelBufferForItemTime:` must run on the main thread. The expensive row-by-row CPU copy runs on a background worker thread:

1. **Main thread:** `acquire_pixel_buffer()` -- fast AVF API calls, returns retained CVPixelBuffer
2. **Worker thread:** `copy_pixel_buffer()` -- lock, row-by-row memcpy, unlock, release
3. **Frame thread:** `pop_latest()` from `DecodedFrameQueue`, upload to GPU

`AcquiredPixelBuffer` is a move-only RAII wrapper around the retained `CVPixelBufferRef`. Replaced work items, flushes, and worker shutdown all release retained buffers automatically when pending lambdas are discarded.

### HAP Path

HAP decode is fast (Snappy decompress) and doesn't need the main thread. `decode_frame()` runs synchronously, then `make_decoded_frame()` packs the compressed data into a `DecodedFrame` for direct queue push via `submit_decoded()`.

### Unified Consumption

Both paths converge at `pop_latest()` + format-driven upload:
- `compressed == true` -- `movie_upload_compressed()` (BC format)
- `compressed == false` -- `movie_upload_bgra()` (BGRA8)

## Clock Modes

Selected automatically based on whether the `audio_time` input port is connected:

| Mode | Authority | Behavior |
|------|-----------|----------|
| **Self-clock** | AVPlayer's internal clock | MovieFileIn reads decoder position, no seeks issued |
| **Audio-master** | Audio pipeline time | MovieFileIn computes desired position from audio_time, corrects via three-tier policy |

In audio-master mode, `MovieTransport` now carries a bounded internal phase calibration (`±250 ms max`). Persistent medium drift of one sign is treated as a stable offset and slowly absorbed into that internal bias. This keeps audio authoritative while preventing steady-state device/bridge latency from producing endless drop/repeat churn. Large drift, source changes, explicit seeks, and loop resets still bypass calibration and use the normal seek path.

## Three-Tier Correction Policy

Defined in `MovieTransport::evaluate_correction()`:

| Tier | Condition | Action |
|------|-----------|--------|
| **None** | drift ≤ 2 frame durations | No correction -- normal jitter |
| **DropRepeat** | 2 frames < drift ≤ 200ms | Handled implicitly by queue frame selection |
| **Seek** | drift > 200ms or source change | Explicit AVPlayer repositioning + queue flush |

Seek rate limiters:
- **Cooldown:** 150ms minimum between seeks
- **Budget:** max 4 seeks per second (rolling window)
- **Source change:** always seeks (bypasses cooldown and budget)

When a Seek is needed but budget/cooldown prevents it, the decision degrades to DropRepeat with `budget_exhausted = true`.

`VideoDecodeWorker::flush()` is generation-based. It clears pending work, empties the ready queue, and bumps a monotonic generation counter so any already-running pre-flush decode copy is discarded instead of repopulating the queue with stale frames.

## Telemetry

### MovieFileIn Analysis Ports (13)

| Port | Type | Measures |
|------|------|----------|
| `new_frames` | counter | Frames decoded and uploaded |
| `reused_frames` | counter | Ticks where no new frame was available |
| `nil_frames` | counter | Decoder returned nil (stall/error) |
| `decode_time_us` | EMA | Main-thread acquire time (us) |
| `copy_time_us` | EMA | Background copy time (us) |
| `upload_time_us` | EMA | GPU upload time (us) |
| `drift_ms` | EMA | AV sync drift magnitude (ms) |
| `seek_corrections` | counter | Corrective seeks issued |
| `seek_budget_exhausted` | counter | Seeks suppressed by budget |
| `drop_repeat_corrections` | counter | Medium-drift corrections handled by queue |

### MovieFileAudio Analysis Ports (2)

| Port | Measures |
|------|----------|
| `buffered_ms` | Audio ring buffer fullness (ms) |
| `underruns` | Audio buffer underrun events |

All analysis ports are tagged `"analysis"` and bridged across cadences by the runtime.

### Diagnostic Checks

`run_diagnostics` includes three movie-specific findings:
- `movie_file_path_empty` -- no file path set
- `movie_sustained_nil_frames` -- >50% nil frames over 60+ frames
- `movie_sustained_large_drift` -- drift_ms exceeds 200ms
- `movie_seek_churn` -- repeated AV correction seeks during steady playback
- `movie_audio_bridge_mismatch` -- `MovieFileAudio/time` does not match bridged `audio_time`

## Source Lifecycle

1. File param changes -- `on_source_changed()` fires
2. Session released from registry, new per-node session acquired for the current operator id
3. `VideoDecodeWorker` queue flushed, `last_decoded_frame_` cleared
4. Async decoder load begins (background thread for AVF, sync for HAP)
5. On load completion: `session.transport().set_source(duration)` called
6. First `evaluate_correction()` returns Seek (source-generation mismatch forces initial positioning)
