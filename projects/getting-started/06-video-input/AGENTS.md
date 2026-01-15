# Lesson 6: Video Input

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Video: Webcam, VideoPlayer
- Core: Noise, Displace, Vignette

## Lesson Focus
Video and webcam as texture sources with real-time effect processing.

## Key Concepts
- **Webcam**: Live camera input operator
- **VideoPlayer**: File-based video playback
- **Displace**: Spatial displacement using a map texture
- **Real-time processing**: Effects applied every frame

## Video Operators Reference
| Operator | Purpose |
|----------|---------|
| `Webcam` | Live camera capture |
| `VideoPlayer` | File playback |
| `TimeMachine` | Frame buffer effects |

## Suggested Modifications

1. **Add more effects**:
   ```cpp
   auto& mirror = chain.add<Mirror>("mirror");
   mirror.input("displace");
   mirror.axis = MirrorAxis::Horizontal;
   ```

2. **Use video file**:
   ```cpp
   auto& video = chain.add<vivid::video::VideoPlayer>("video");
   video.setFile("assets/clip.mp4");
   video.play();
   video.loop = true;
   ```

3. **Try different displacement maps**: Use Gradient instead of Noise

## Troubleshooting
- **No webcam image**: Check camera permissions, ensure webcam is connected
- **Low frame rate**: Reduce resolution or effect count
- **Wrong camera**: Use `cam.setDeviceIndex(N)` to select different camera

## Next
07-3d-basics: Introduction to 3D rendering with Scene3D
