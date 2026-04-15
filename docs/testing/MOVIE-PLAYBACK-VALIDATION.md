# Movie Playback Manual Validation

Manual test checklist for the canonical `MovieFile` playback system.

For release certification, use this together with [MOVIE-PLAYBACK-GO-NO-GO.md](MOVIE-PLAYBACK-GO-NO-GO.md) and the machine-readable runtime gate in [movie-playback-runtime-gate.json](movie-playback-runtime-gate.json).

## Prerequisites

- At least one H.264/HEVC `.mp4` or `.mov` file with audio
- At least one HAP `.mov` file, if available
- A video-only file with no audio track for video-only transport tests
- Vivid running with output window and node graph UI visible

## 1. Video-Only Playback

### 1.1 Loop Mode
- Add `MovieFile`, set `file` to an H.264/HEVC video, `play_mode = Loop`, `speed = 1.0`
- Connect `texture` to `video_out`
- **Expected:** video loops cleanly at the clip boundary
- **Pass:** no freeze, black frame, or multi-frame jump at the loop point
- **Fail:** visible gap, stale frame, or jump near the final second before wrap

### 1.2 Once And Hold Last
- Set `play_mode = Once`, then `play_mode = Hold Last`
- **Expected:** playback stops on the final frame and keeps presenting it
- **Pass:** last frame remains stable
- **Fail:** video restarts, goes black, or presents stale source frames

### 1.3 Speed Adjustment
- Set `speed` to `0.5`, observe, then set to `2.0`
- **Expected:** playback rate changes visually without transport reset storms
- **Pass:** motion speed follows the setting
- **Fail:** no visible speed change, sustained stutter, or repeated diagnostics warnings

## 2. Audio+Video Playback

### 2.1 Basic AV Sync
- Add one `MovieFile`, set `file` to an audio-bearing movie
- Connect `texture` to `video_out` and `audio` to `audio_out`
- **Expected:** audio and video are perceptibly synchronized
- **Pass:** lip sync within about 100 ms
- **Fail:** visible or audible drift

### 2.2 Loop Transition AV Sync
- Set `play_mode = Loop`
- **Expected:** audio and video loop as one session event
- **Pass:** sync remains stable across 5+ loops
- **Fail:** drift grows, audio wraps before video, or video skips near the loop

### 2.3 Sustained Playback
- Let playback run for 5 minutes in Loop mode
- Use `sample_node_outputs` or `introspect_nodes` to monitor `drift_ms`
- **Pass:** `drift_ms` stays below 200 ms throughout normal playback
- **Fail:** `drift_ms` grows unbounded or repeated `movie_seek_churn` appears

## 3. HAP Path

### 3.1 HAP Playback
- Load a HAP `.mov` file in `MovieFile`
- **Expected:** video plays with compressed texture upload
- **Pass:** output displays correctly
- **Fail:** corrupted output, no decode, or missing texture updates

### 3.2 HAP Loop
- Set `play_mode = Loop`
- **Expected:** target-time selection and loop generation match the non-HAP path
- **Pass:** loop boundary is stable
- **Fail:** stale frames or visible jumps at wrap

## 4. Source Changes

### 4.1 Repeated Source Change
- While playing file A, change `file` to file B
- **Expected:** source identity, audio state, video state, loop generation, and telemetry reset together
- **Pass:** new movie appears and sounds without lingering old content
- **Fail:** crash, stale frame, stale audio, or mixed telemetry from the previous source

### 4.2 Clear File During Playback
- While playing, set `file` to empty
- **Expected:** placeholder frame appears and audio becomes silent
- **Pass:** clean transition to placeholder and no runtime error
- **Fail:** crash or stale media

## 5. Window And UI State

- Close and reopen the output window while `MovieFile` plays
- Hide and show the node graph UI while `MovieFile` plays
- **Expected:** playback quality and telemetry are unaffected by UI visibility
- **Pass:** output stays stable and telemetry keeps updating
- **Fail:** cadence changes materially, output freezes, or telemetry stalls

## 6. Telemetry Verification

- Use `sample_node_outputs` or `introspect_nodes` on the `MovieFile` node
- Verify these ports behave during playback:
  - `new_frames` increments when new frames present
  - `reused_frames` may increment for lower-fps content
  - `nil_frames` does not climb steadily in healthy playback
  - `decode_time_us`, `copy_time_us`, and `upload_time_us` report backend work
  - `drift_ms` stays small during audio-bearing playback
  - `seek_corrections` stays low during steady playback
  - `buffered_ms` remains healthy when audio is active
  - `underruns` stays at `0` during normal playback

## 7. Diagnostics And Runtime Gate

- Run `run_diagnostics` on a healthy movie graph
- **Pass:** no movie-related warnings
- Set `file` to empty, run diagnostics again
- **Pass:** `movie_file_path_empty` warning appears
- Load `mfi_av_sync_demo.json`, let playback stabilize, then run `validate_checks` / `run_checks` with [movie-playback-runtime-gate.json](movie-playback-runtime-gate.json)
- **Pass:** all checks pass with no critical failures

## Acceptance Criteria

- [ ] Video-only demos show no obvious cadence instability
- [ ] Audio-bearing demos stay within 200 ms drift budget under normal load
- [ ] `MovieFile/time` follows the session clock and `video_phase_offset_ms` is the only manual presentation offset
- [ ] Loop and source-change behavior are deterministic and visibly stable
- [ ] Non-HAP video does not skip multiple frames in the final second before loop
- [ ] Playback quality does not depend on output window or node graph UI visibility
- [ ] Instrumentation explains late frames, reused frames, nil frames, and correction events
- [ ] The public graph surface is the single `MovieFile` node
- [ ] The runtime gate in [movie-playback-runtime-gate.json](movie-playback-runtime-gate.json) passes on the healthy AV-sync demo graph
