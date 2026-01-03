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

```cpp
// Video with synchronized audio output
chain.add<VideoPlayer>("video")
    .file("assets/videos/hap-1080p-audio.mov");

chain.add<VideoAudio>("audio")
    .video("video");
```

## Webcam Input

```cpp
chain.add<Webcam>("cam")
    .device(0);  // First camera

chain.add<Output>("out")
    .input("cam");
```

## API Reference

See [LLM-REFERENCE.md](../../docs/LLM-REFERENCE.md) for complete operator documentation.

## Dependencies

- vivid-core
- Platform video frameworks (AVFoundation on macOS, Media Foundation on Windows)
- FFmpeg (optional, for additional codec support)

## License

MIT
