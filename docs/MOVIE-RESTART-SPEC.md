# Movie Restart Spec (Long-Run Stable, Vivid-Native, Package-Portable)

## Summary
Rebuild movie playback around operator-owned media logic plus minimal generic runtime primitives.

Locked decisions:
- No media-specific runtime manager.
- Runtime adds only generic primitives:
  - typed shared-handle registry
  - generic cross-domain `VIVID_PORT_DATA` bridge
- Sync authority is audio-master.
- Operator topology:
  - `MovieLoaded` (session source/control)
  - `MovieVideoOut` (GPU consumer)
  - `MovieAudioOut` (audio consumer)
- Existing legacy movie operators are removed; no compatibility wrappers are retained in core.

## Public Interfaces / Contracts
1. `media_stream_v1` data contract
- Opaque handle payload for shared media session.
- Fields:
  - `handle_id`
  - `source_generation`
  - `schema_version`

2. `media_clock_v1` contract (authoritative)
- Published by `MovieLoaded`, derived from audio-master timeline.
- Fields:
  - `local_time_s`, `duration_s`, `speed`, `playing`, `loop_enabled`
  - `loop_epoch`
  - `monotonic_time_s`
  - `source_generation`

3. Operator port contracts
- `MovieLoaded` outputs:
  - `media_stream` (`VIVID_PORT_DATA`, `media_stream_v1`)
  - `media_clock` (`VIVID_PORT_DATA`, `media_clock_v1`)
  - optional convenience `texture`
- `MovieVideoOut` inputs:
  - `media_stream` (required)
  - optional `media_clock`
- `MovieAudioOut` inputs:
  - `media_stream` (required)
  - optional `media_clock`

4. Runtime primitive contracts (generic)
- Handle registry API:
  - create/register handle
  - retain/release
  - resolve typed snapshot/read view
  - invalidate by generation/unload token
- No media-specific runtime APIs.

## Implementation Phases
1. Generic runtime primitives
- Implement typed handle registry with refcount + generation invalidation.
- Generalize cross-domain data bridge from special-case `media_clock_v1` to typed `VIVID_PORT_DATA` rules.
- Add deterministic validation errors for type mismatch/invalid handle.
- Extend control-server/MCP introspection for data ports (`data_type`, `handle_id`, generation, validity).

2. Internal `vivid-video` architecture (in core repo first)
- Implement media session internals as operator-layer module (not runtime service).
- Session internals own decode threads, transport command queue, audio ring buffer, and video timestamp queue.
- Keep codec routing modular (AVF/HAP/NotchLC adapters).

3. New operator trio
- Implement `MovieLoaded`.
- Implement `MovieVideoOut` (pull by presentation time, drop/dupe policy, no audio correction).
- Implement `MovieAudioOut` (RT-safe ring reads only, no AVFoundation in callback, underrun-safe silence + diagnostics).

4. Compatibility + migration
- Temporary wrappers:
  - old movie operators forward into new session contracts where possible.
- Graph migration tool:
  - detect legacy pair patterns
  - auto-rewrite to trio where possible
  - emit actionable warnings for non-rewritable graphs
- Define pre-1.0 deprecation/removal checkpoint.

5. Hardening + rollout
- Add stress instrumentation (queue depth, drop/dup, underruns, generation transitions).
- Validate with `assets/sync` corpus + long-loop playback.
- Enable by default once acceptance criteria pass.

## Acceptance Criteria
1. Architecture integrity
- No media-specific runtime service exists.
- Built-in movie operators only use generic handle registry + generic data bridge.
- Same operator stack can move to external package with no runtime media changes.

2. Sync stability
- No recurring `locked <-> hold` chatter in steady playback.
- No near-threshold per-callback hard mute/hold loop.
- Loop transitions do not produce repeated audible crackle in normal playback.

### Measured Thresholds (validated against `assets/sync` corpus)

| Metric | Green (pass) | Yellow (warn) | Red (fail) |
|--------|-------------|---------------|------------|
| `audio_underrun_frames` per loop cycle | 0 | ≤512 (~10 ms) | >512 or progressive |
| `sync_resync_applied` per 60 s steady state | 0–1 | 2–5 | >5 |
| `video_payload_dropped` per loop cycle | 0 | 1–2 | >2 or progressive |
| Loop boundary settle time | <0.5 s | 0.5–2 s | >2 s |

Validated codecs: HAP, HAPQ, HAP-alpha, H.264, HEVC.

Runtime validation protocol: launch Vivid with stderr captured (`./build/vivid 2>&1 | tee /tmp/vivid_loop_validation.log`), open `graphs/io/movie_file/mfi_space_cycle_sync_demo.json`, let each fixture loop for ~5 minutes, then grep for `movie_audio_out.*stats` log lines. Delta counters on `underrun_frames` and `video_drop` should be zero in steady state; `resync_apply` should tick once per loop cycle.

3. Threading correctness
- Audio callback performs no blocking decode/AVFoundation operations.
- Session teardown/switch/seek is crash-free.
- Generation-tagged commands prevent stale seek/resync effects.

4. Cross-domain data correctness
- `media_stream_v1` and `media_clock_v1` flow correctly across control/audio/GPU via generic bridge.
- Type mismatch and stale-handle errors are deterministic and inspectable.

5. Codec/regression behavior
- HAP direct BC path remains functional.
- Non-HAP AVF path remains functional.
- Non-movie graph behavior remains unaffected.

6. Migration outcome
- Legacy demo graphs auto-migrate or fail with actionable warnings.
- New canonical demos use `MovieLoaded` + `MovieVideoOut` + `MovieAudioOut`.

## Test Matrix
1. Unit tests
- Handle registry lifecycle (retain/release/invalidate).
- Cross-domain data bridge type checks + transfer semantics.
- Transport queue ordering + stale command rejection.

2. Integration tests (`assets/sync`)
- Startup/loop/seek/rate across HAP/HAPQ/HAP-alpha/H264.
- Rapid source switching (latest request wins, no stale playback).
- Long-run loops (stable sync, no correction chatter).

3. Real-time safety tests
- Audio callback timing under stress (no blocking calls).
- Deterministic underrun behavior.
- Safe teardown with active sessions (no crash/leak/thread orphan).

4. Manual verification
- Perceptual A/V alignment on transient clips.
- Loop boundary behavior without crackle.
- Package-portability check using same contracts.
