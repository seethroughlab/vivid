# Movie Playback Manual Validation

Manual test checklist for the movie playback system. Covers the full validation matrix from `docs/plans/movie-playback-implementation.md`.

## Prerequisites

- At least one H.264/HEVC `.mp4` or `.mov` file with audio
- At least one HAP `.mov` file (if available)
- A video-only file (no audio track) for video-only tests
- Vivid running with output window and node graph UI visible

## 1. Video-Only Playback (H.264/HEVC)

### 1.1 Loop Mode
- Add `MovieFileIn`, set file to H.264 video, play_mode = Loop, speed = 1.0
- Connect to `video_out`
- **Expected:** video loops seamlessly at end of clip
- **Pass:** no freeze, no black frame at loop boundary
- **Fail:** visible gap (>500ms) or glitch at loop point

### 1.2 Once Mode
- Set play_mode = Once
- **Expected:** video plays once and stops on last frame
- **Pass:** playback stops, last frame stays displayed
- **Fail:** video restarts or goes black

### 1.3 Hold Last Mode
- Set play_mode = Hold Last
- **Expected:** video plays once and holds final frame (same as Once for video-only)
- **Pass:** last frame stays on screen indefinitely

### 1.4 Speed Adjustment
- Set speed to 0.5x, observe, then set to 2.0x
- **Expected:** playback rate changes visually
- **Pass:** motion speed matches setting
- **Fail:** no visible change, or stuttering

## 2. Audio+Video Synced Playback (H.264/HEVC)

### 2.1 Basic AV Sync
- Add `MovieFileIn` + `MovieFileAudio`, same file, connect `audio_time` bridge
- Connect audio to `audio_out`, video to `video_out`
- **Expected:** audio and video are perceptibly synchronized
- **Pass:** lip sync within ~100ms
- **Fail:** visible/audible drift

### 2.2 Loop Transition AV Sync
- Set both to Loop mode
- **Expected:** both loop together without drift accumulation
- **Pass:** sync maintained across 5+ loops
- **Fail:** drift grows with each loop

### 2.3 Sustained Playback (5 minutes)
- Let play for 5 minutes in Loop mode
- Use `sample_node_outputs` to monitor `drift_ms`
- **Pass:** `drift_ms` stays below 200ms throughout
- **Fail:** `drift_ms` grows unbounded or exceeds 200ms

### 2.4 Audio Clock Bridge Integrity
- Inspect both `MovieFileAudio` and `MovieFileIn` while synced playback is running
- Compare `audio/time` against the frame-side `vid/audio_time` input
- **Pass:** the values track within ~50ms during steady playback
- **Fail:** the values diverge materially or one value appears pinned/stale

## 3. HAP Path

### 3.1 HAP Playback
- Load a HAP `.mov` file in `MovieFileIn`
- **Expected:** video plays with compressed texture upload
- **Pass:** video displays correctly (no garbled output)
- **Fail:** corrupted display or no decode

### 3.2 HAP Loop
- Set Loop mode with HAP file
- **Expected:** seamless loop (same quality as H.264 loop)

### 3.3 HAP AV Sync (if file has audio)
- Connect `MovieFileAudio` for the same HAP file
- **Expected:** same sync behavior as H.264 path

## 4. Seeks and Scrubbing

### 4.1 Repeated Source Change
- While playing file A, change `file` param to file B
- **Expected:** transition within ~500ms, no stale frames from file A
- **Pass:** new video appears, no lingering old content
- **Fail:** black screen, crash, or stale frames

### 4.2 Clear File During Playback
- While playing, set `file` param to empty
- **Expected:** placeholder frame appears
- **Pass:** clean transition to placeholder
- **Fail:** crash or stale video

## 5. Window and UI State

### 5.1 Output Window Closed
- Close the output window while movie plays
- **Expected:** no crash, playback continues internally
- **Pass:** reopening window shows current frame
- **Fail:** crash or frozen output

### 5.2 Output Window Open
- Open the output window and observe playback
- **Expected:** no performance degradation
- **Pass:** same cadence as without window

### 5.3 UI Hidden
- Hide the node graph UI panel while movie plays
- **Expected:** playback unaffected
- **Pass:** showing UI again shows current telemetry values
- **Fail:** performance change or freeze

### 5.4 UI Visible
- Keep UI visible during sustained playback
- **Expected:** telemetry ports update each frame in inspector

## 6. Telemetry Verification

### 6.1 MovieFileIn Analysis Ports
- Use `sample_node_outputs` or `introspect_nodes` on MovieFileIn
- Verify these ports produce non-zero values during playback:
  - `new_frames` (incrementing)
  - `reused_frames` (non-zero for sub-60fps content)
  - `decode_time_us` (non-zero EMA)
  - `upload_time_us` (non-zero EMA)
- With audio connected:
  - `drift_ms` (small value during sync)
  - `seek_corrections` (should be low/zero during steady state)
  - `drop_repeat_corrections` (may be non-zero)
  - `seek_corrections` should not climb steadily during stable `speed=1` playback

### 6.2 MovieFileAudio Analysis Ports
- Verify `buffered_ms` shows ring buffer fullness (~2000ms when healthy)
- Verify `underruns` stays at 0 during normal playback

### 6.3 Diagnostics Report
- Run `run_diagnostics` via MCP on a healthy movie graph
- **Pass:** no movie-related warnings
- Set file param to empty, run diagnostics again
- **Pass:** `movie_file_path_empty` warning appears

## Acceptance Criteria

All of the following must be true:

- [ ] Video-only demos show no obvious cadence instability
- [ ] AV-synced demos stay within 200ms drift budget under normal load
- [ ] `MovieFileAudio/time` and bridged `MovieFileIn/audio_time` stay within 50ms during steady playback
- [ ] Loop and seek behavior are deterministic and visibly stable
- [ ] Playback quality does not depend on whether the node graph UI is visible
- [ ] Playback quality does not materially degrade when the output window is open
- [ ] Instrumentation can explain late frames, reused frames, and correction events
- [ ] The public MovieFileIn + MovieFileAudio graph surface remains intact
