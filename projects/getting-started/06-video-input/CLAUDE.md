# Lesson 06: Video Input

This lesson introduces video and webcam as texture sources.

## Lesson Objectives

1. Capture webcam video
2. Apply effects to live video
3. Use displacement for creative distortion
4. Understand video as a texture source

## Key Concepts

- **Webcam**: Live camera input operator
- **VideoPlayer**: File-based video playback
- **Displace**: Spatial displacement using a map texture
- **Real-time processing**: Effects applied every frame

## What the Code Demonstrates

- Basic webcam capture
- Noise-based displacement effect
- Post-processing with vignette
- Interactive parameter control

## Suggested Modifications

1. **Add more effects**:
   ```cpp
   auto& mirror = chain.add<Mirror>("mirror");
   mirror.input("displace");
   mirror.axis = MirrorAxis::Horizontal;
   ```

2. **Try different displacement maps**:
   ```cpp
   // Use a gradient instead of noise
   auto& gradient = chain.add<Gradient>("gradient");
   gradient.mode = GradientMode::Radial;
   displace.map("gradient");
   ```

3. **Add color processing**:
   ```cpp
   auto& hsv = chain.add<HSV>("hsv");
   hsv.input("cam");
   hsv.saturation = 1.5f;
   ```

4. **Use video file**:
   ```cpp
   auto& video = chain.add<vivid::video::VideoPlayer>("video");
   video.setFile("assets/clip.mp4");
   video.play();
   video.loop = true;
   displace.source("video");
   ```

## Video Operators Reference

| Operator | Purpose |
|----------|---------|
| `Webcam` | Live camera capture |
| `VideoPlayer` | File playback |
| `TimeMachine` | Frame buffer effects |

## Common Issues

- **No webcam image**: Check camera permissions, ensure webcam is connected
- **Low frame rate**: Reduce resolution or effect count
- **Wrong camera**: Use `cam.setDeviceIndex(N)` to select different camera

## Next Lesson

07-3d-basics: Introduction to 3D rendering with Scene3D
