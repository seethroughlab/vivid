# Video Audio Sync - Architecture Options

## Problem Statement

When routing video audio through Vivid's effects chain (VideoAudio → Delay → Reverb → AudioOutput), audio drifts out of sync with video because:
- **AVPlayer** controls video playback with its own internal clock
- **AVAssetReader** extracts audio independently (no shared clock)
- These two timelines drift apart over time

TouchDesigner solves this by using a single decoder with a shared playback clock for both video and audio.

---

## Option 1: MTAudioProcessingTap (AVPlayer Audio Tap)

**Concept:** Intercept AVPlayer's audio output via Apple's MTAudioProcessingTap API. AVPlayer keeps A/V sync internally, we just process the audio it gives us.

### Pros
- **Perfect A/V sync** - By definition, since AVPlayer provides both
- **Ultra-low latency** - Direct access to decoded PCM
- **No buffer copying** - Process in-place
- **Minimal code changes** - AVPlayer continues to handle video

### Cons
- **Real-time constraints** - Callback runs on audio thread with strict requirements:
  - No locks, no allocations, no blocking
  - Must process exact number of frames requested
  - Violating these causes audio dropouts
- **Can't buffer for chain processing** - Must process immediately
- **No HLS/streaming support** - Only works with file-based assets
- **Complex integration** - Vivid's block-based processing model doesn't fit well
- **One tap per track** - Can't combine with other taps

### Implementation Complexity
- **Effort:** 2-3 weeks
- **Risk:** HIGH - Real-time audio thread constraints are unforgiving

### Technical Details
```objc
// Attach tap to AVPlayer's audio track
MTAudioProcessingTapCallbacks callbacks = {
    .init = tapInit,
    .prepare = tapPrepare,
    .process = tapProcess,  // Real-time callback
    .unprepare = tapUnprepare,
    .finalize = tapFinalize
};
MTAudioProcessingTapCreate(kCFAllocatorDefault, &callbacks,
                           kMTAudioProcessingTapCreationFlag_PreEffects, &tap);
```

### Recommendation
**Best for:** Real-time audio analysis (FFT, levels) where you need perfect sync but don't need to route through effects chain. **Not recommended** for full effects chain integration due to real-time constraints.

---

## Option 2: Custom AVAssetReader Decoder

**Concept:** Replace AVPlayer entirely. Use AVAssetReader for both video and audio with a custom shared playback clock.

### Pros
- **Full control** - Complete ownership of timing
- **Shared clock** - Both video and audio sync to same reference
- **TouchDesigner model** - How TD and professional tools do it
- **No OS limitations** - Not constrained by AVPlayer's API

### Cons
- **Massive undertaking** - Essentially rewriting the video player
- **Lose AVPlayer features:**
  - Hardware-optimized seeking
  - Seamless looping (AVPlayerLooper)
  - Buffering/streaming intelligence
  - Rate control and time pitch
- **Seek latency** - 100-400ms (AVAssetReader requires full recreation)
- **Loop gaps** - No gapless playback (unavoidable with AVAssetReader)
- **Clock drift** - Must implement drift correction manually

### Implementation Complexity
- **Effort:** 20-30 developer hours (4-6 weeks part-time)
- **Risk:** HIGH - Many edge cases, potential for subtle bugs

### Breakdown
| Component | Effort | Risk |
|-----------|--------|------|
| Basic playback | 3-4 hrs | Low |
| Clock sync | 6-8 hrs | **High** |
| Seeking | 4-5 hrs | Medium |
| Looping | 5-7 hrs | **High** |
| Testing | 8-10 hrs | - |

### Recommendation
**Not recommended** unless you need features AVPlayer can't provide. The effort-to-benefit ratio is poor, and you lose significant OS-level optimizations.

---

## Option 3: Timestamp-Based Sync (Recommended)

**Concept:** Keep current architecture but add Presentation Timestamp (PTS) tracking. Audio samples are timestamped when extracted, and the audio callback syncs output to the current video time.

### Pros
- **Minimal changes** - Builds on existing code
- **Preserves AVPlayer benefits** - Seeking, looping, hardware accel
- **Incremental implementation** - Can ship in phases
- **Low risk** - Each phase is independently testable
- **Already partially designed** - `readAudioSamplesForPTS()` exists in API but unimplemented

### Cons
- **Not perfect sync** - Correction happens reactively, not proactively
- **Potential audio artifacts** - Skip/silence during aggressive resync
- **Latency spikes** - During seek recovery

### Implementation Complexity
- **Effort:** 8-12 hours (1-2 weeks part-time)
- **Risk:** LOW-MEDIUM

### Implementation Phases

**Phase 1: Timestamp Infrastructure (2-3 hours)**
```cpp
// In avf_playback_decoder.mm readMoreAudio():
CMTime pts = CMSampleBufferGetOutputPresentationTimeStamp(sampleBuffer);
double ptsSeconds = CMTimeGetSeconds(pts);
// Store with samples instead of discarding
```

**Phase 2: Video Time Communication (1-2 hours)**
```cpp
// Atomic for thread-safe communication
std::atomic<double> lastVideoTimeSeconds{0.0};

// In update() - publish video time
impl_->lastVideoTimeSeconds.store(CMTimeGetSeconds(currentTime));

// In audio callback - read video time
double videoPTS = impl_->lastVideoTimeSeconds.load();
```

**Phase 3: Drift Detection & Correction (3-4 hours)**
```cpp
// In readAudioSamples():
double syncError = videoPTS - audioBufferPTS;
if (syncError > 0.5) {
    // Audio behind - skip samples
} else if (syncError < -0.5) {
    // Audio ahead - insert silence
}
```

**Phase 4: Seek Resync (2-3 hours)**
```cpp
// In seek():
audioBuffer.clear();
setupAudioReaderWithTimeRange(asset, seekSeconds);
```

### Sync Thresholds
| Drift | Action |
|-------|--------|
| < ±100ms | None (imperceptible) |
| 100-500ms | Log warning, gradual resync |
| > 500ms | Aggressive resync (skip/silence) |

### Recommendation
**Recommended approach.** Best balance of effort, risk, and quality. Can be implemented incrementally with each phase providing value.

---

## Comparison Summary

| Aspect | Audio Tap | Custom Decoder | Timestamp Sync |
|--------|-----------|----------------|----------------|
| **Sync Quality** | Perfect | Perfect | Good (±100ms) |
| **Implementation** | 2-3 weeks | 4-6 weeks | 1-2 weeks |
| **Risk** | High | High | Low-Medium |
| **Preserves AVPlayer** | Yes | No | Yes |
| **Effects Chain** | Difficult | Yes | Yes |
| **Seek/Loop** | Works | Degraded | Works |

---

## Files to Modify (Option 3)

| File | Changes |
|------|---------|
| `modules/vivid-video/src/avf_playback_decoder.mm` | Add PTS extraction, atomic time, sync logic |
| `modules/vivid-video/include/vivid/video/avf_playback_decoder.h` | Add timestamped buffer struct |
| `modules/vivid-video/src/video_audio.cpp` | Pass video PTS to readAudioSamples |

## Verification

1. Play video with visible sync reference (clapper, speech)
2. Route through effects chain with effects bypassed
3. Verify A/V sync within ±100ms
4. Test seeking forward/backward
5. Test looping
6. Enable effects and verify sync maintained
