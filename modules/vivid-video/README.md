# vivid-video

Video playback with HAP, H.264, HEVC, ProRes, and other codecs via AVFoundation/FFmpeg.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

| Operator | Description |
|----------|-------------|
| `VideoPlayer` | Video file playback to texture |
| `VideoAudio` | Extract audio from video |
| `AudioPlayer` | Standalone audio file playback |
| `Webcam` | Live webcam input |

## Supported Codecs

| Codec | macOS | Windows | Linux |
|-------|-------|---------|-------|
| H.264/AVC | AVFoundation | Media Foundation | FFmpeg |
| HEVC/H.265 | AVFoundation | Media Foundation | FFmpeg |
| ProRes | AVFoundation | - | FFmpeg |
| HAP | Native | Native | Native |
| HAP-Q | Native | Native | Native |
| HAP Alpha | Native | Native | Native |
| Motion JPEG | AVFoundation | Media Foundation | FFmpeg |

## HAP Codec

HAP is recommended for high-performance playback. It uses GPU-accelerated DXT compression for:
- Low CPU usage during playback
- High resolution (4K+) without stuttering
- Alpha channel support (HAP Alpha)
- Higher quality option (HAP-Q)

## Examples

See `tests/fixtures/video-demo` for codec testing and comparison.

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/video/video.h>

using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Play a video file
    chain.add<VideoPlayer>("video")
        .file("assets/videos/my-video.mov")
        .loop(true);

    chain.add<Output>("out")
        .input("video");
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

## Video with Audio

There are two modes for playing video with audio:

### Internal Audio (default)

Uses the platform's native player for perfect audio/video synchronization. Best for simple playback without effects.

```cpp
auto& video = chain.add<VideoPlayer>("video");
video.setFile("assets/videos/movie.mov");
video.setInternalAudioEnabled(true);  // Default - uses AVPlayer/Media Foundation
video.play();

chain.output("video");
// Audio plays automatically through system audio
```

### Chain Audio

Routes audio through Vivid's audio chain for effects processing. May introduce slight A/V sync drift due to processing latency.

```cpp
auto& video = chain.add<VideoPlayer>("video");
video.setFile("assets/videos/movie.mov");
video.setInternalAudioEnabled(false);  // Disable native audio
video.play();

// Extract audio from video
auto& videoAudio = chain.add<VideoAudio>("videoAudio");
videoAudio.setSource("video");

// Add effects
auto& delay = chain.add<Delay>("delay");
delay.input("videoAudio");

auto& reverb = chain.add<Reverb>("reverb");
reverb.input("delay");

// Output through audio chain
auto& output = chain.add<AudioOutput>("out");
output.setInput("reverb");

chain.output("video");
chain.audioOutput("out");
```

**When to use each mode:**

| Mode | A/V Sync | Audio Effects | Use Case |
|------|----------|---------------|----------|
| Internal Audio | Perfect | None | Simple video playback |
| Chain Audio | May drift | Yes (delay, reverb, etc.) | Audio-reactive visuals, DJ apps |

See `modules/vivid-video/examples/video-audio/` for a complete example demonstrating both modes.

## Webcam Input

```cpp
chain.add<Webcam>("cam")
    .device(0);  // First camera

chain.add<Output>("out")
    .input("cam");
```

## API Reference

See the examples in `modules/vivid-video/examples/` for usage patterns.

## Dependencies

- vivid-core
- Platform video frameworks (AVFoundation on macOS, Media Foundation on Windows)
- FFmpeg (optional, for additional codec support)

## License

MIT
