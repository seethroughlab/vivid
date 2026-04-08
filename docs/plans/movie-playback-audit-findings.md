# Movie Playback Audit Findings

Status: audit findings only. This document describes the current `MovieFile*` stack, identifies reliability risks, and records the recommended problem framing for follow-up implementation work.

See also:

- [Movie Playback Target Architecture](movie-playback-target-architecture.md)
- [Movie Playback Reliability Implementation Plan](movie-playback-implementation.md)

## Goal

Evaluate the full movie loading and playback system as a single reliability surface for macOS-first Vivid:

- `operators/gpu/movie_file_in/`
- `operators/audio/movie_file_audio/`
- `operators/shared/movie_decode/*`
- `operators/shared/movie_audio/*`
- frame/audio bridge interaction
- render-loop and presentation interaction in the main app

Priority order for this audit:

1. Stable video cadence in video-only graphs.
2. Stable synchronized AV playback without oscillatory correction.
3. Predictable behavior during loop, seek, source swap, pause/resume, and output-window changes.
4. Clear instrumentation and acceptance criteria for regressions.

## Current System Snapshot

### Public model

The current public graph model exposes two operators:

- `MovieFileIn` for video frames plus `time`/`duration`.
- `MovieFileAudio` for audio output plus `time`/`duration`.

The two operators are intentionally independent at the graph surface. When both are used together, `MovieFileAudio/time` crosses the `AudioFrameBridge` and can drive `MovieFileIn/audio_time`.

### Current backend split

- HAP-family video files use `HAPDecoder` in `operators/shared/movie_decode/hap_decoder.mm`.
- Non-HAP video files such as H.264 and HEVC use `AVFDecoder` in `operators/shared/movie_decode/avf_decoder.mm`.
- Audio extraction uses `AVFAudioExtractor` in `operators/shared/movie_audio/avf_audio_extractor.mm`.

### Current non-HAP playback shape

For ordinary `.mp4`/`.mov` content, Vivid currently does the following:

1. `MovieFileIn` calls `decode_frame()` from the frame/main thread.
2. `AVFDecoder` polls `AVPlayerItemVideoOutput` for a pixel buffer.
3. The pixel buffer is copied row-by-row into CPU memory.
4. `MovieFileIn` uploads the copied frame with `wgpuQueueWriteTexture`.
5. The frame is then blitted through the normal GPU graph/output path.

The audio side is separate:

1. `MovieFileAudio` uses a fill thread and ring buffer.
2. `AVFAudioExtractor` decodes audio with `AVAssetReader`.
3. `MovieFileAudio/time` is published across the `AudioFrameBridge`.
4. `MovieFileIn` optionally seeks toward the bridged audio time when drift exceeds a threshold.

## Findings

### 1. The ordinary video path is frame-thread driven and likely cadence-fragile

`AVFDecoder::decode_frame()` is called from the frame thread and is explicitly main-thread only. That path performs:

- frame polling through `AVPlayerItemVideoOutput`
- player state nudging (`play`, `rate`)
- pixel-buffer acquisition
- full-frame CPU copy

This means decode availability, CPU copy cost, and render cadence are coupled to the same thread that also owns:

- UI work
- graph execution
- output blit/presentation
- capture and analysis scheduling

That is not the shape most reliability-first playback systems use.

### 2. The current non-HAP path has no bounded video frame queue

The AVFoundation video path does not maintain a decoded frame queue with explicit late-frame policy. Instead it polls for "the frame for now" each render tick. When no frame is returned, the caller effectively reuses the previous texture.

Missing capabilities in the current shape:

- explicit new-frame versus reused-frame accounting
- bounded decode backlog accounting
- deterministic drop/repeat policy
- explicit ownership of "late frame" behavior

This makes cadence problems visible but hard to reason about.

### 3. AV sync correction currently relies on seek-based correction

`MovieFileIn` computes drift against bridged audio time and issues corrective seeks when drift exceeds approximately two frame durations.

This is workable as a bootstrap strategy, but it has weaknesses:

- exact seek operations are relatively expensive correction events
- repeated small seeks can create visible jerkiness
- the policy is asymmetric: video is corrected by repositioning, not by a bounded presentation adjustment policy
- the bridged clock is not the same thing as a dedicated transport clock with explicit jitter budgeting

This is especially risky when paired with the frame-thread-polled AVPlayer video path.

### 4. Video-only and AV-synced playback currently use different timing ideas without a shared transport abstraction

Today the system effectively has several clocks:

- AVPlayer current time on the video side
- audio ring/write-head time on the audio side
- frame-thread tick cadence
- display presentation cadence

The current implementation works by stitching those clocks together procedurally rather than by routing them through a single playback-session abstraction with a declared authority and correction policy.

That makes it harder to answer:

- which clock is authoritative in each mode
- where drift should be measured
- what should happen when decode falls behind
- which corrections are legal in steady state

### 5. The HAP path is architecturally healthier than the ordinary AVPlayer path

The HAP decoder path already looks closer to Vivid's needs:

- decode is pull-based and explicit
- frame timing is tracked locally
- uploads can use compressed GPU-friendly textures
- there is less hidden player behavior

This does not make HAP the universal answer, but it is a useful reference point. The non-HAP path currently leans more on AVPlayer behavior than on a Vivid-owned playback policy.

### 6. Existing tests cover safety and routing more than playback quality

Current coverage is meaningful but incomplete:

- `tests/media/test_movie_av_sync.cpp` covers sync math and audio-ring behavior.
- `tests/media/test_movie_decode_route.cpp` covers HAP versus AVF route choice.
- `tests/media/test_movie_decode_upload.cpp` covers texture upload row alignment.
- `tests/media/test_movie_load_async.cpp` and `tests/media/test_movie_load_generation.cpp` cover async load/generation safety.
- `tests/media/test_media_headless.cpp` exercises media graphs in a headless integration setting.

Key gaps:

- no cadence-quality assertions
- no drift-budget assertions under sustained playback
- no explicit video-only frame-delivery metrics
- no loop-boundary quality tests
- no seek/scrub stress tests
- no UI-visible versus UI-hidden playback comparison
- no output-window-open versus output-window-closed comparison

### 7. Instrumentation is not yet good enough for playback triage

The current code has a few helpful logs, but not a coherent playback telemetry story. There is not yet a standard set of counters for:

- new frames delivered
- reused frames
- nil frame fetches
- decode latency
- upload latency
- correction events by type
- drift magnitude distribution
- underrun/starvation events

Without those, playback work will continue to rely too heavily on "looks choppy" reports.

## Comparison Baseline: Common Reliable Playback Patterns

This audit should treat the following as the baseline patterns used by reliability-first playback systems across media players, NLEs, and live-visual tools:

- one authoritative transport clock per playback mode
- asynchronous decode away from the UI/render thread
- bounded frame queues and audio queues
- explicit late-frame policy: drop, repeat, or hold
- AV sync via bounded correction, not frequent exact seeks
- presentation timing separated from decode timing
- quality telemetry built into the player, not bolted on afterward

Vivid's current non-HAP path does not fully meet that bar yet.

## Current Recommendation

Treat the present non-HAP movie path as acceptable for prototyping but not yet acceptable as Vivid's long-term "solid playback" architecture.

The follow-up work should:

- keep Apple media frameworks as the substrate for macOS
- move toward an operator-owned playback session model implemented under `operators/shared/movie_*`
- move decode and buffering policy out of the frame/UI thread
- replace steady-state seek correction with bounded drop/repeat or rate-correction policy
- preserve the existing public `MovieFileIn` + `MovieFileAudio` graph model unless the implementation work proves that split unworkable

Architectural boundary to preserve:

- the movie system should remain self-contained in operator code and shared operator support code
- follow-up work should not introduce a runtime-core movie manager, transport owner, or media-session service
- core may continue to provide generic facilities already available to all operators, but it should not gain movie-specific orchestration responsibilities

## Questions This Audit Resolves

This audit resolves the following high-level decisions:

- Yes, this should be treated as a system audit, not a single-demo bug.
- Yes, the current non-HAP path should be assumed insufficient for Vivid's reliability bar.
- No, this phase should not change public graph/operator APIs.
- Yes, future work should remain Apple-framework-based on macOS unless a hard blocker is discovered during implementation.
- Yes, the movie system should remain operator-owned rather than becoming a runtime-core subsystem.

## Questions Deferred To Implementation

The audit intentionally leaves these to the implementation plan:

- the exact transport/session object shape
- whether audio remains the universal master clock or only the AV-synced clock
- whether bounded video-rate correction is preferable to occasional seek correction
- how much telemetry is always-on versus debug-only
