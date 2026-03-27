# Cadence-Native Movie File System

## Context

The current MovieFile system (MovieLoaded + MovieAudioOut + MovieVideoOut) communicates through a `MediaSession` side-channel — a shared struct with its own ring buffer, transport command queue, video payload queue, preroll gate, and generation tracking. This is a parallel runtime that bypasses vivid's cadence bridge. Every other operator in vivid routes data through declared ports and the double-buffered snapshot system; movie operators are the exception.

**Goal:** Replace the side-channel architecture with two self-contained operators that communicate through normal graph edges and the cadence bridge.

---

## Design

### MovieFileIn (GPU operator, frame-rate)

Replaces `MovieLoaded`. Drops all MediaSession/SharedHandle machinery. Adds an input for audio sync.

| | Name | Type | Notes |
|---|---|---|---|
| **Params** | `file` | FilePath | Video or image path |
| | `speed` | float 0-4 | Playback rate |
| | `play_mode` | int | 0=Loop, 1=Once, 2=Hold Last |
| | `video_phase_offset_ms` | float -250..250 | Fine AV sync trim |
| **Inputs** | `audio_time` | SIGNAL (optional) | Monotonic playback seconds from audio op (via cadence bridge) |
| **Outputs** | `texture` | TEXTURE | Decoded video frame |
| | `time` | SIGNAL | Current playback seconds |
| | `duration` | SIGNAL | Media duration |

**Sync logic:**
- If `audio_time > 0`: audio-master — sync video to that position (seek if drift > 2 frame durations, accounting for ~16ms bridge latency)
- If `audio_time == 0` or not connected: self-clock from `ctx->time * speed`, handle looping internally

**Keeps:** Background video decoder thread, decoded frame buffer, BC/HapQ/YCoCg pipelines, image file support, placeholder frame, `MovieLoadCoordinator` generation tracking.

**Removes:** `MediaSession` creation, `SharedHandleRegistry` registration, `MediaStreamV1` custom ref output port, transport command queue, video payload queue.

### MovieFileAudio (Audio operator, audio-rate)

Replaces `MovieAudioOut`. All buffering is operator-private — no shared session.

| | Name | Type | Notes |
|---|---|---|---|
| **Params** | `file` | FilePath | Same file as MovieFileIn |
| | `speed` | float 0-4 | Playback rate |
| | `volume` | float 0-2 | Linear gain |
| | `pitch_preserve` | int | 0=rate-only, 1=TimePitch |
| | `play_mode` | int | 0=Loop, 1=Once, 2=Hold |
| **Outputs** | `output` | AUDIO (stereo) | Decoded audio samples |
| | `time` | SIGNAL (float output) | Monotonic playback seconds — crosses to frame via analysis snapshot |
| | `duration` | SIGNAL (float output) | Audio track duration |

**Internal architecture:**
- Private ring buffer (~5s @ 48kHz, same capacity as current)
- Private fill thread (~200Hz pump, same pattern as current `AudioFillThread`)
- Internal preroll gate: silence until >= 0.5s buffered
- `main_thread_update()`: file loading, extractor lifecycle, speed/pitch/loop changes
- `process_audio()`: read ring -> write output buffers, apply volume, publish `output_float_values[0]` = monotonic time

### Graph wiring

**Video + Audio (AV sync):**
```
movie_file_audio/time   -> movie_file_in/audio_time    (audio->frame via cadence bridge)
movie_file_in/texture   -> video_out/input
movie_file_audio/output -> audio_out/input
```

**Video only:**
```
movie_file_in/texture -> video_out/input
```

**Audio only:**
```
movie_file_audio/output -> audio_out/input
```

---

## Key design decisions

**Sync precision.** The `audio_time` signal crosses the cadence bridge at ~60Hz (one snapshot per frame tick). At 24fps video this is ~38% of a frame; at 60fps it's nearly a full frame. Using a 2-frame-duration seek threshold (instead of the current 1.5) absorbs bridge latency cleanly. Observable sync quality is identical because the display refresh is the bottleneck.

**Duplicated params vs signal wiring for speed/play_mode.** Both operators declare their own `speed`, `play_mode`, `file` params rather than wiring from one to the other. This keeps each operator independently usable (video-only, audio-only) and follows vivid's operator-declares-its-own-params convention. Users wire the same upstream source to both `file` params.

**Fill thread stays internal.** The `AudioFillThread` pattern moves inside `MovieFileAudio` as private state. This eliminates the `pump_mu_`/`quiesce()` dance, the `std::atomic<MediaSession*>` coordination, and the deferred-delete machinery. Fill thread lifecycle is tied to operator lifecycle.

**No transport command queue.** Speed/play_mode changes arrive as parameter updates through the cadence bridge's `ParamSnapshot` — the standard vivid mechanism. No custom transport protocol needed.

---

## What gets removed

| File/Concept | Reason |
|---|---|
| `src/operator_api/media_stream.h` | Custom ref type replaced by normal SIGNAL port |
| `src/operator_api/media_clock.h` | Clock struct replaced by float signal outputs |
| `operators/shared/media_session/media_session.h` | Shared session replaced by private operator state |
| `operators/gpu/movie_video_out/` | Unnecessary — MovieFileIn outputs texture directly |
| `SharedHandleRegistry` usage for media | No shared handles needed |
| `VIVID_DESCRIBE_REF_TYPE(MediaStreamV1)` | Custom ref type eliminated |

---

## Implementation order

### Phase 1: Create MovieFileAudio
- New file: `operators/audio/movie_file_audio/movie_file_audio.cpp`
- Extract `AudioFillThread` from `movie_audio_out.cpp`, operate on private ring buffer
- Declare params (`file`, `speed`, `volume`, `pitch_preserve`, `play_mode`) and ports (`output`, `time`, `duration`)
- `main_thread_update()`: file loading, extractor lifecycle, speed/pitch sync
- `process_audio()`: ring read, volume, publish float outputs
- CMakeLists.txt for new operator

### Phase 2: Refactor MovieLoaded -> MovieFileIn
- Modify `operators/gpu/movie_loaded/movie_loaded.cpp`
- Remove: MediaSession, SharedHandle, MediaStreamV1 output, transport queue, video payload queue
- Add: `audio_time` SIGNAL input port
- Modify sync: read `input_values[audio_time_idx]`, seek video if drift > 2 frames
- Preserve: background loader, video decoders, BC/YCoCg, image support, `MovieLoadCoordinator`

### Phase 3: Update demo graphs
- All graphs in `graphs/io/movie_file/` and `graphs/gpu/movie_loaded_demo.json`
- Replace node types + rewire connections

### Phase 4: Delete legacy
- Remove `movie_video_out/`, `media_session.h`, `media_stream.h`, `media_clock.h`
- Remove `MovieAudioOut` operator
- Clean up CMakeLists, port type registrations

### Phase 5: Tests
- Update `test_movie_load_async.cpp`, `test_movie_load_generation.cpp`, `test_movie_decode_route.cpp`
- Remove or rewrite `test_movie_long_loop_sync.cpp` (sync model changed)
- Remove `test_media_clock.cpp`, `test_media_session_queue.cpp`
- New tests: standalone video playback, standalone audio playback, AV sync via cadence bridge, rapid source changes, speed modulation

---

## Verification

1. **Video-only**: Load `.mp4`, confirm texture output, looping, speed control
2. **Audio-only**: Load `.mp4`, confirm stereo audio output through effects chain
3. **AV sync**: Wire `time -> audio_time`, confirm lip-sync with `mfi_av_sync_demo` assets
4. **Source switch**: Rapidly change file path, confirm no crashes or stale state
5. **Speed**: Modulate speed 0-4x, confirm both operators track together
6. **Formats**: Test HAP/BC compressed, H.264, static images (.jpg/.png)
7. **Preroll**: Confirm video waits for audio buffer before playing when `audio_time` is wired
8. **Run existing tests**: `ctest --test-dir build` to catch regressions
