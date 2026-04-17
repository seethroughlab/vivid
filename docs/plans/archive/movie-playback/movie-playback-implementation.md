# Movie Playback Reliability Implementation Plan

Status: implementation plan only. This follows the movie playback audit and target-architecture recommendation; it does not by itself change runtime behavior.

See also:

- [Movie Playback Audit Findings](movie-playback-audit-findings.md)
- [Movie Playback Target Architecture](movie-playback-target-architecture.md)

## Goal

Deliver a macOS-first movie playback system that is solid enough for Vivid's core demos and live-use expectations without changing the public `MovieFileIn` + `MovieFileAudio` graph model in the first pass.

Implementation boundary:

- keep the movie system self-contained under the movie operators and `operators/shared/movie_*`
- avoid turning movie playback into a runtime-core subsystem

## Scope

Primary targets:

- `operators/gpu/movie_file_in/movie_file_in.cpp`
- `operators/audio/movie_file_audio/movie_file_audio.cpp`
- `operators/shared/movie_decode/*`
- `operators/shared/movie_audio/*`

Core/runtime touch policy:

- do not add a runtime-owned movie manager, transport owner, or media-session service
- do not move playback policy into `src/runtime/`
- only allow core changes when they are generic, reusable hooks that are not movie-specific
- prefer zero core changes where possible

Public behavior to preserve in the first pass:

- existing operator names
- existing file/speed/play mode parameters
- existing `time`/`duration` outputs
- existing ability to use video-only or audio-only independently

## Staged Plan

### Stage 1: Instrument The Current System

Add low-risk telemetry before structural changes.

Required counters and timings:

- video new-frame uploads
- video reused-frame presentations
- AVFoundation nil/missed frame fetches
- video decode acquisition time
- video CPU-copy time where applicable
- GPU upload time
- audio buffered duration
- audio underruns/starvation
- AV drift magnitude
- correction events by type

Deliverables:

- structured debug counters accessible in tests and logs
- one or more targeted diagnostics dumps for movie playback

Boundary rule:

- prefer telemetry owned by the movie operator layer
- if any generic diagnostics hook is needed from core, keep it capability-style and reusable by non-movie operators

Exit criteria:

- we can distinguish decode starvation from upload slowness from sync correction churn

### Stage 2: Stabilize The Current Non-HAP Path Enough To Measure

Before deeper refactoring, make the current path easier to reason about:

- isolate frame selection policy from raw AVPlayer polling
- make repeated-frame behavior explicit instead of accidental
- reduce or remove steady-state seek thrash where possible
- document current clock ownership and correction thresholds in code comments and docs

Exit criteria:

- current-path behavior is instrumented and deterministic enough for A/B comparison with the refactor

### Stage 3: Introduce An Internal Playback Session

Add a private playback-session layer shared by `MovieFileIn` and `MovieFileAudio`.

Responsibilities:

- own source generation and lifecycle
- own transport time
- coordinate audio and video workers
- own bounded decode queues
- centralize loop, seek, and reset behavior

Constraints:

- do not change public graph JSON
- do not require users to replace `MovieFileIn` or `MovieFileAudio`
- implement the session layer under `operators/shared/movie_*`
- do not introduce a runtime-core playback service
- session ownership must remain with the movie operators/shared movie layer

Exit criteria:

- both operators consume session state rather than independently inventing timing policy

### Stage 4: Move Non-HAP Video Decode Off The Frame/UI Thread

Refactor the ordinary AVFoundation video path so that:

- decode/frame acquisition happens away from the frame/UI thread
- decoded frames are timestamped and queued
- the frame thread selects from the queue rather than polling AVPlayer as its primary behavior

Preferred behavior:

- frame thread uploads the best queued frame for the current transport time
- if no newer frame is ready, it repeats the last acceptable frame and records that fact

Exit criteria:

- video-only demos no longer depend on frame-thread decode polling for cadence

### Stage 5: Replace Seek-Led Steady-State Sync With Bounded Correction

Refactor AV sync policy so that:

- exact seek is no longer the default steady-state correction mechanism
- small and medium drift are handled through drop/repeat policy against transport time
- explicit repositioning is reserved for large drift, loop, and user-initiated seek/scrub

Decision to lock during implementation:

- audio remains authoritative in AV-synced mode for v1 of the refactor

Exit criteria:

- sustained AV playback avoids oscillatory correction behavior

### Stage 6: Normalize HAP And Non-HAP Under One Playback Contract

Keep backend-specific decode implementations, but align them under the same session contract:

- same transport-time semantics
- same telemetry surface
- same loop and seek semantics
- same late-frame policy vocabulary

Exit criteria:

- HAP and non-HAP differ in backend cost, not in user-visible playback contract

### Stage 7: Validation And Hardening

Run the full validation matrix and tighten docs/tests.

Required updates:

- refresh architecture/runtime docs only as needed to describe unchanged generic runtime responsibilities plus the operator-owned movie model
- codify acceptance thresholds in tests and manual validation docs
- ensure diagnostics/checks can expose movie-playback health without introducing movie-specific core orchestration

## Validation Matrix

The implementation must be validated against all of the following:

- video-only playback
- audio+video synced playback
- loop mode
- once mode
- hold-last mode
- repeated seeks/scrubbing
- source changes while playing
- output window closed
- output window open
- UI visible
- UI hidden
- H.264/HEVC path
- HAP path

## Testing

### Automated

Extend or add tests for:

- session lifecycle and generation invalidation
- drift classification and correction policy
- loop transition behavior
- queue flush/refill on seek
- repeated-frame and dropped-frame accounting
- output-window-independent playback state

Keep existing tests for:

- decode routing
- texture upload layout/alignment
- async load generation safety
- AV sync math helpers

### Manual

Create a playback-focused manual checklist for:

- baseline movie demos
- long-running AV sync
- visible cadence comparison before/after refactor
- output window open/closed behavior
- UI interaction during playback

## Acceptance Criteria

The work is complete when all of the following are true:

- baseline video-only movie demos show no obvious cadence instability
- AV-synced demos stay within the agreed drift budget under normal load
- loop and seek behavior are deterministic and visibly stable
- playback quality does not depend on whether the node graph UI is visible
- playback quality does not materially degrade when the output window is open
- instrumentation can explain late frames, reused frames, and correction events
- the public `MovieFileIn` + `MovieFileAudio` graph surface remains intact

## Public Interface Position

No public graph/operator API changes are required in the audit implementation phase.

The implementation should, however, explicitly reevaluate after stabilization:

- whether the `MovieFileIn` + `MovieFileAudio` split remains the right public model
- whether `audio_time` remains a first-class sync contract or becomes compatibility scaffolding over a session transport
- whether any internal shared playback/session object should eventually surface as a first-class runtime concept

That reevaluation should assume the default answer is "no" unless a strong implementation blocker appears. The preferred architecture is a private shared movie-layer object, not a first-class runtime-core concept.

## Risks

- AVFoundation may constrain how far the non-HAP path can be improved without deeper backend changes.
- Session-sharing between the public operators must not introduce stale-source or lifecycle bugs.
- Queue-based playback can improve cadence while masking underlying decode starvation unless telemetry is mandatory.
- HAP and non-HAP must converge on behavior without losing the performance advantage of the HAP direct-compression path.
