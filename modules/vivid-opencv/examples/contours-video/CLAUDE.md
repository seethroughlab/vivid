# Contours - Video

Contour detection on video files with overlay blending.

## Features Demonstrated

- **VideoPlayer** - Video file playback
- **Contours** - OpenCV contour detection
- **Composite** - Additive blending of contours on video

## Key Concepts

### Blending Contours with Video

The Contours operator outputs on a transparent background, making it easy to overlay:

```cpp
// Detect contours
auto& contours = chain.add<vivid::opencv::Contours>("contours");
contours.input("video");

// Blend with source using additive mode
auto& composite = chain.add<Composite>("composite");
composite.inputA("video");
composite.inputB("contours");
composite.mode = BlendMode::Add;  // Contours glow on top
```

### Other Blend Modes

```cpp
// Screen blend - lighter result
composite.mode = BlendMode::Screen;

// Multiply - darker, only where both have content
composite.mode = BlendMode::Multiply;

// Over - standard alpha compositing
composite.mode = BlendMode::Over;
```

### Dynamic Threshold Control

Adjust thresholds based on video content:

```cpp
// Mouse control
glm::vec2 mouse = ctx.mouseNorm();
contours.threshold1 = mouse.x * 255.0f;
contours.threshold2 = mouse.y * 255.0f;
```

### Video Playback Control

```cpp
auto& video = chain.get<vivid::video::VideoPlayer>("video");

// Pause/resume
video.pause();
video.play();

// Seek
video.seekToTime(5.0f);  // Jump to 5 seconds

// Loop control
video.setLoop(true);
```

## Controls

- **Mouse X**: Canny threshold 1 (0-255)
- **Mouse Y**: Canny threshold 2 (0-255)
- **Space**: Pause/play video
- **B**: Toggle blend mode (overlay vs contours only)
- **C**: Cycle contour colors

## Assets

Place your video file as `assets/video.mp4` in the project directory.

Supported formats depend on platform:
- macOS: MOV, MP4, M4V (H.264, HEVC, ProRes)
- Windows: MP4, WMV, AVI (H.264, HEVC)
- Linux: Depends on FFmpeg installation
