# Movie Playback Target Architecture

Status: target-architecture recommendation only. This document describes the reliability-oriented design that follow-up implementation work should aim toward on macOS.

See also:

- [Movie Playback Audit Findings](movie-playback-audit-findings.md)
- [Movie Playback Reliability Implementation Plan](movie-playback-implementation.md)

## Goal

Define a macOS-first playback architecture for Vivid that:

- keeps Apple media frameworks as the underlying substrate
- preserves the current public `MovieFileIn` + `MovieFileAudio` operator model unless implementation proves otherwise
- keeps the movie system self-contained in operator/shared-operator code rather than promoting it into runtime core
- removes frame-thread cadence fragility from ordinary video playback
- gives Vivid a clear transport clock, queue model, sync policy, and telemetry story

## Design Principles

### 1. Vivid owns playback policy

Apple frameworks should provide decode primitives, not the overall playback contract.

Within Vivid, that ownership should live in the movie operators and their shared support code, not in runtime core.

That means Vivid, not AVPlayer behavior, should define:

- the authoritative clock
- queue sizes
- late-frame policy
- drift budgets
- loop semantics
- seek semantics

### 2. Decode and presentation must be separated

The render/UI thread should not be the core place where decode availability is discovered. It should consume the best available frame for the current presentation time from a Vivid-managed queue.

### 3. Reliable playback needs explicit queues

The system should maintain bounded queues for:

- decoded video frames ready for upload or presentation
- decoded audio ready for playback

Each queue needs explicit observability and policy when producer and consumer rates differ.

### 4. Sync correction should be bounded and steady-state friendly

Steady-state playback should prefer:

- frame drop
- frame repeat
- small bounded correction windows

It should avoid frequent exact seeks as a normal correction mechanism.

## Recommended Architecture

### Playback Session Layer

Add an internal playback-session abstraction behind `MovieFileIn` and `MovieFileAudio`.

Responsibilities:

- own the media asset lifecycle
- own the transport clock
- coordinate video and audio decode workers
- manage bounded audio and video queues
- publish playback telemetry
- expose current presentation state to the two public operators

This should be an internal implementation detail, not a public graph surface change.

Placement constraint:

- this playback-session layer should live under `operators/shared/movie_*`
- it should be owned by the movie operators, not by `src/runtime/`
- it should not become a runtime-global media service

### Clock Model

Use an explicit transport clock with mode-dependent authority:

- **Video-only mode:** transport clock is self-clocked from monotonic host time plus playback rate.
- **Audio+video mode:** audio playback remains the authority for user-visible sync, but the session exposes a stable transport time instead of requiring the video operator to infer one from a bridged scalar plus ad hoc seek policy.

Required rule:

- every playback mode must have exactly one authoritative transport time
- every correction policy must be defined relative to that transport time

### Video Decode Model

For non-HAP video:

- move frame acquisition off the frame/UI thread
- decode into a bounded queue of timestamped frames
- present by selecting the best frame for the current transport time

Recommended behavior:

- if the ideal frame is late or unavailable, repeat the most recent acceptable frame
- if decode is ahead, keep only the bounded queue depth needed for jitter absorption
- if decode falls persistently behind, surface telemetry and degraded-mode behavior rather than issuing frequent exact seeks

For HAP:

- keep the direct compressed upload path
- align its timing and telemetry surfaces with the new session model so HAP and non-HAP share the same higher-level playback contract

### Upload Model

Keep GPU upload on the frame/render side, but only for frames already selected by the playback session.

Required properties:

- the render thread never blocks on decode
- upload cost is measurable per frame
- "no new frame available" is a normal state with explicit accounting

Core boundary:

- runtime core continues to provide the same generic operator process hooks, texture outputs, and audio/frame execution plumbing it already provides
- movie-specific frame selection, queue ownership, and transport logic remain in operator-owned code

### Audio Decode Model

Keep the dedicated audio decode thread/ring-buffer approach, but make it session-owned rather than operator-owned in isolation.

Required properties:

- session-level visibility into buffered duration
- session-level loop and seek coordination
- explicit underrun and starvation accounting
- transport-time publication from the session rather than from loosely coupled operator state

That session ownership should still be within the movie operator layer, not in runtime core.

### Sync Policy

Recommended steady-state policy:

- audio+video mode uses audio-backed transport time
- video presents the best frame for that time from the decoded queue
- correction uses drop/repeat within a defined drift window
- exact seek is reserved for:
  - user-driven seek/scrub
  - source swap/reset
  - large unrecoverable drift
  - loop transition

Recommended initial drift budgets:

- small drift window where no correction is needed
- medium drift window where drop/repeat is allowed
- large drift window where explicit repositioning is allowed

The exact numbers should be validated during implementation, but the policy shape should not be deferred.

### Presentation Ownership

The frame/render thread owns:

- choosing the frame to upload/present from the already-decoded session state
- GPU upload timing
- final blit/presentation

It does not own:

- discovering new decodes
- deciding sync authority
- deciding whether a late frame should trigger a seek

## Public API Position

Keep the public graph/operator model unchanged in the first implementation pass:

- keep `MovieFileIn`
- keep `MovieFileAudio`
- keep `audio_time` compatibility initially

The implementation should, however, be allowed to internally de-emphasize `audio_time` as the source of truth once a session-owned transport clock exists.

Internal boundary to preserve:

- no new core-level movie manager
- no runtime-owned playback registry keyed by movie nodes
- no movie-specific transport abstraction in `src/runtime/`
- any shared state between `MovieFileIn` and `MovieFileAudio` should be created and owned from the movie operator layer

Recommended follow-up decision after stabilization:

- reevaluate whether `audio_time` remains a first-class public sync input or becomes a compatibility surface over a stronger internal transport model

## Failure-Mode Policy

The implementation should be explicit about the following cases:

- **Video decode late:** repeat the last good frame, increment late/reuse counters.
- **Video decode starved:** hold last frame, increment starvation counters, do not thrash the player with repeated exact seeks.
- **Audio underrun:** surface underrun telemetry, preserve deterministic recovery behavior.
- **Loop boundary:** perform one explicit transition policy, not a series of reactive corrections.
- **Seek/scrub:** flush or invalidate queues deterministically, then refill around the new target time.
- **Source swap:** invalidate the old session cleanly and prevent stale frame/audio reuse.

## Telemetry Requirements

The target architecture should expose session-level counters and timings for:

- decoded video frames
- presented video frames
- repeated/held frames
- dropped frames
- nil frame fetches or decode misses
- decode latency
- upload latency
- audio buffered duration
- audio underruns
- drift magnitude
- correction events by type

These should be available to tests and optionally surfaced in diagnostics/logging.

## Acceptance Thresholds

The implementation should not be considered complete until it can demonstrate:

- smooth baseline playback in video-only movie demos
- bounded AV drift during sustained playback
- deterministic loop transitions
- stable behavior with UI visible and hidden
- stable behavior with output window closed and open
- no main-thread movie decode stalls in the ordinary non-HAP path
