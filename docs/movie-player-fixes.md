# Movie File AV Sync — Final Audit

## Context

We fixed two bugs in the AV sync chain: (1) unbounded clock drift in AudioRing, and (2) the cadence bridge writing audio signals to `input_values` which the frame executor zeroed every tick. This audit evaluates the full implementation and identifies remaining issues.

## Overall Assessment

The sync architecture is **sound and production-ready for typical use cases** (24-60fps video, looping files, 1x speed). The dual-head time tracking with snap/slew/deadband reconciliation is the right approach — it matches how VLC, GStreamer, and ffplay handle AV sync. The bridge_input_values mechanism correctly solves the signal-crossing problem.

**Three issues warrant fixing now. The rest are minor or acceptable.**

---

## Issues to Fix (Ranked)

### 1. Bridge zero-check drops legitimate 0.0 values — HIGH

**File:** `src/runtime/frame_executor.cpp:130-133`

```cpp
if (cn.bridge_input_values[p] != 0.0f)  // ← drops real zeros
    cn.input_values[p] = cn.bridge_input_values[p];
```

If an audio node outputs exactly 0.0 (zero-crossing, silence, muted gain), the GPU node never receives it. For MovieFileAudio this is masked by the epsilon floor (`1e-6`), but any other audio→GPU signal connection would break.

**Fix:** Add a `bridge_input_dirty` bitmask to CompiledNode. The bridge sets bits when writing; the frame executor checks bits instead of value != 0.0f; bits are cleared after application.

**Files:** `compiled_graph.h`, `cadence_bridge.cpp`, `frame_executor.cpp`, `graph_compiler.cpp`

### 2. Residual buffer timeline gap in TimePitch decode — MEDIUM

**File:** `operators/shared/movie_audio/avf_audio_extractor.mm:331-338`

When draining residual samples from a previous TimePitch render pass, `media_time_written` is not updated. This creates a discontinuity in `write_head_time` — the write head stalls while residuals are consumed, then jumps when fresh decoding resumes. The drift reconciliation absorbs this (within the 100ms snap threshold), but it introduces unnecessary jitter.

**Fix:** Add `media_time_written += from_residual * speed / target_sample_rate;` after the residual drain memcpy.

### 3. `audio_time > 0.0f` as connection detection — MEDIUM

**File:** `operators/gpu/movie_file_in/movie_file_in.cpp:225`

Using value magnitude as a proxy for "is the port connected" is fragile. The epsilon floor in MovieFileAudio masks this (ensures > 0), but the pattern is wrong in principle. If anyone changes the epsilon or the wrap logic, sync silently breaks.

**Fix:** Check `ctx->input_values != nullptr` AND add a connection flag to the GPU context, or simply change the check to `audio_time > -0.5f` with a sentinel of `-1.0f` for disconnected. The simplest approach: leave as-is but add a comment explaining the contract with MovieFileAudio's epsilon floor.

---

## Accepted Limitations (Not Worth Fixing Now)

### TimePitch latency (~85ms) not compensated
`write_head_pts()` reports decode position, not audible position. The 85ms offset is constant and absorbed by the drift reconciliation. Fixing it would require subtracting `kTimePitchLatency * speed / sample_rate` from the reported time, plus tracking pipeline state across resync. Low ROI since the reconciliation handles it.

### Cooldown uses audio-time instead of wall-clock
The 30ms seek cooldown in MovieFileIn uses monotonic audio time. At non-1x speeds, the effective cooldown duration changes. At 2x, cooldown is 15ms wall-clock; at 0.5x, it's 60ms. This is acceptable — the cooldown just prevents seek thrashing, and the thresholds are generous enough.

### Decoder accessed without lock from GPU thread
`decoder_` is read in `process_gpu()` without synchronization while the loader thread could theoretically replace it. In practice, `apply_ready_load_result()` runs on the main thread (same as process_gpu), so there's no actual race. But it would be safer to use a shared_ptr swap.

### Failed seek silently ignored
If `decoder_->seek()` returns false, `published_local_time` still reports the desired position. The video will be at the wrong time until the next successful seek. This is rare and self-correcting.

### Float precision for >2 hour playback
The double→float cast loses precision for long sessions. The wrapping by duration bounds this by file length, so a 5-minute file stays sub-microsecond forever. Only affects single files >2 hours played without looping.

---

## What We'd Do Differently From Scratch

1. **Connection metadata in the GPU context.** A `bool* input_connected` array (or bitmask) so operators can distinguish "disconnected port" from "connected port sending 0.0". Eliminates the `> 0.0f` sentinel hack and the bridge zero-check problem entirely.

2. **Bridge writes during frame execution, not before.** Instead of a separate `pull_from_audio()` pass + `bridge_input_values` indirection, the frame executor would directly read the audio snapshot when it encounters an `audio_to_frame_edge`. One pass, no intermediate buffer, no zeroing problem.

3. **Unified time type across the signal bridge.** The audio thread tracks time as `double` but the signal port system is `float`. A dedicated `double` channel between MovieFileAudio and MovieFileIn (bypassing the float signal port) would eliminate all precision concerns.

4. **Latency-compensated time output.** `write_head_pts()` should subtract TimePitch pipeline latency from the reported position, so the time signal reflects what's actually audible, not what's been decoded.

5. **Proportional video speed adjustment.** Instead of only seeking when error exceeds a threshold, gradually adjust the video decoder's playback rate to track the audio clock. This is how professional players work (VLC's "adjust latency" mode). It eliminates the sawtooth error pattern between seeks.

---

## Verification

After fixing issues 1-2:
1. `cmake --build build --target test_movie_av_sync && ./build/test_movie_av_sync` — all tests pass
2. Load `mfi_space_cycle_sync_demo.json`, let play for 2+ minutes, verify sync holds
3. Test with speed changes (0.5x, 2x) mid-playback
4. Test spacebar cycling between files — verify seek recovery

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/compiled_graph.h` | Add `bridge_input_dirty` bitmask |
| `src/runtime/graph_compiler.cpp` | Initialize dirty mask |
| `src/runtime/cadence_bridge.cpp` | Set dirty bits when writing bridge values |
| `src/runtime/frame_executor.cpp` | Check dirty bits instead of `!= 0.0f` |
| `operators/shared/movie_audio/avf_audio_extractor.mm` | Update media_time_written for residual drain |
| `tests/test_movie_av_sync.cpp` | Add test for zero-value bridge passthrough |
