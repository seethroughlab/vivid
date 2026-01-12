# Lesson 06: Video Input

Use video files and webcam as texture sources for your effects.

## What You'll Learn

- Capturing webcam with the `Webcam` operator
- Processing live video with effects
- Real-time video manipulation
- Video as texture source

## Prerequisites

- Completed Lesson 05: Audio-Reactive
- A webcam connected to your computer

## Run It

```bash
./build/bin/vivid projects/getting-started/06-video-input
```

You should see your webcam feed with effects applied!

## Walkthrough

### Capturing Webcam

```cpp
#include <vivid/video/video.h>

auto& cam = chain.add<vivid::video::Webcam>("cam");
cam.setResolution(1280, 720);  // Requested resolution
cam.setFrameRate(30.0f);       // Requested frame rate
```

The webcam automatically selects your default camera.

### Applying Effects to Video

Just like with images, you can chain effects:

```cpp
auto& cam = chain.add<vivid::video::Webcam>("cam");

auto& pixelate = chain.add<Pixelate>("pixelate");
pixelate.input("cam");
pixelate.size = 8.0f;
```

### The Displacement Effect

A powerful technique is using noise to displace the video:

```cpp
auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;
noise.speed = 0.5f;

auto& displace = chain.add<Displace>("displace");
displace.source("cam");    // What to distort
displace.map("noise");     // What controls the distortion
displace.strength = 0.05f; // How much to displace
```

This creates organic, wavy distortions in real-time.

## Try It

1. **Adjust displacement**: Change strength from 0.01 (subtle) to 0.15 (extreme)
2. **Change noise scale**: Larger scale = broader waves, smaller = finer detail
3. **Add color effects**: Try HSV after the displacement
4. **Mirror yourself**: Add a Mirror operator

## Multiple Cameras

If you have multiple cameras:

```cpp
cam.setDeviceIndex(0);  // First camera (default)
cam.setDeviceIndex(1);  // Second camera
```

## Video Files

For video files instead of webcam:

```cpp
auto& video = chain.add<vivid::video::VideoPlayer>("video");
video.setFile("assets/myvideo.mp4");
video.play();
video.loop = true;
```

Supported formats depend on platform (MP4, MOV, HAP).

## Performance Tips

- Lower resolution = faster processing
- Fewer effects = higher frame rate
- GPU effects are fast; CPU effects can be slow

## Next Steps

- **Lesson 07**: Introduction to 3D rendering
- **Deep dive**: `modules/vivid-video/examples/` for time effects, HAP video, and more
