# Plan: Pitch-preserving audio speed with AVAudioUnitTimePitch

## Problem

MovieFileAudioIn gets crackly audio when the video is sped up. The audio extractor always decodes at 1x rate. When video speeds up, the sync logic drops samples to catch up, causing clicks/pops.

## Current architecture

- `AVFAudioExtractor` uses `AVAssetReader` + `AVAssetReaderTrackOutput` to decode audio into a lock-free SPSC ring buffer (48000 samples / ~1s at 48kHz)
- `MovieFileAudioIn::process()` (audio thread) reads from the ring buffer
- Sync logic in `process()` detects drift between `video_time` and `read_head_pts()`:
  - <50ms: tolerate
  - 50ms–500ms: skip/pad samples (causes crackle)
  - >500ms: full resync via new AVAssetReader
- No speed awareness — audio is always consumed at 1x

## Proposed approach: AVAudioUnitTimePitch

Use Apple's `AVAudioUnitTimePitch` (part of AVFoundation/AVFAudio) to do pitch-preserving time-stretch natively. This avoids external dependencies while providing high-quality audio speed changes.

### Architecture

Replace the current `AVAssetReader` → ring buffer → `read_samples()` pipeline with:

```
AVAssetReader → AVAudioPCMBuffer → AVAudioEngine pipeline:
  AVAudioPlayerNode → AVAudioUnitTimePitch → output tap → ring buffer
```

Or alternatively, process decoded PCM through `AVAudioUnitTimePitch` offline:

```
AVAssetReader → decode to PCM buffer → AVAudioUnitTimePitch (offline render) → ring buffer
```

### Key components

1. **AVAudioEngine**: Hosts the processing graph
2. **AVAudioPlayerNode**: Feeds decoded PCM into the engine
3. **AVAudioUnitTimePitch**: Apple's built-in time-pitch unit
   - `rate` property: playback speed (0.25x to 4.0x)
   - `pitch` property: pitch shift in cents (set to 0 for pitch-preserving)
4. **Install tap on output**: Capture processed audio into the existing ring buffer

### Changes needed

#### MovieFileIn: Add speed output port

In `movie_file_in.cpp`, add a `speed` output port so the audio operator can receive it:
- Add `{"speed", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT}` to `collect_ports`
- Output `speed.value` via `ctx->output_values[2]` in `process()`

#### MovieFileAudioIn: Add video_speed param

Add `vivid::Param<float> video_speed {"video_speed", 1.0f, 0.0f, 4.0f}` to receive speed from MovieFileIn via wire.

#### AVFAudioExtractor: Replace with AVAudioEngine-based approach

**Option A: AVAudioEngine with tap (recommended)**

```objc
// Setup
AVAudioEngine* engine = [[AVAudioEngine alloc] init];
AVAudioPlayerNode* playerNode = [[AVAudioPlayerNode alloc] init];
AVAudioUnitTimePitch* timePitch = [[AVAudioUnitTimePitch alloc] init];

[engine attachNode:playerNode];
[engine attachNode:timePitch];
[engine connect:playerNode to:timePitch format:format];
[engine connect:timePitch to:engine.mainMixerNode format:format];

// Install tap to capture output into ring buffer
[engine.mainMixerNode installTapOnBus:0 bufferSize:4096 format:format
    block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
        // Copy buffer contents into ring buffer
    }];

[engine startAndReturnError:&error];
[playerNode play];
```

- Feed decoded PCM buffers from AVAssetReader into the playerNode via `scheduleBuffer:completionHandler:`
- Update `timePitch.rate` when speed changes
- Tap captures time-stretched output into the existing ring buffer
- Audio thread reads from ring buffer as before

**Option B: Offline rendering**

Use `AVAudioEngine` in manual rendering mode to process blocks of audio on demand rather than real-time. This gives more control but is more complex.

### Speed propagation

Wire in graph JSON:
```json
{ "from": "vid/speed", "to": "audio/video_speed" }
```

### Sync adjustments

- The fine-sync sample-dropping logic should be removed entirely — the time-pitch unit handles speed
- Coarse sync (>500ms drift → resync) should remain for seeks
- The ring buffer fill rate will naturally match consumption since the time-pitch unit adjusts output rate

### Threading considerations

- `AVAudioEngine` runs its own real-time thread internally
- The tap callback runs on the engine's thread — must be lock-free when writing to ring buffer
- Speed changes (`timePitch.rate = newRate`) are thread-safe per Apple docs
- The existing atomic extractor pointer handoff pattern can remain

### Demo graph updates

Demo graphs using MovieFileAudioIn should add the speed wire connection.

### Estimated scope

- `avf_audio_extractor.mm`: Major rewrite (~150 lines changed)
- `avf_audio_extractor.h`: Minor interface additions (set_speed method)
- `movie_file_audio_in.cpp`: Add video_speed param, remove fine-sync logic (~20 lines)
- `movie_file_in.cpp`: Add speed output port (~5 lines)
- Demo graph JSONs: Add speed wire connections
