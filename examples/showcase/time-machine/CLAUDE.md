# Time Machine

## Vision

Create a temporal displacement effect inspired by TouchDesigner's TimeMachine TOP. Using a webcam feed and a frame cache, different parts of the image show different moments in time based on a displacement map. This creates mesmerizing slit-scan effects, time echoes, and temporal smearing.

The effect should feel like bending time itself - faces elongate and trail, movement leaves ghostly echoes, and reality becomes fluid and dreamlike.

## Key Features

- Frame cache storing ~2 seconds of video frames
- Multiple displacement pattern presets:
  - Vertical slit-scan (classic photo finish effect)
  - Horizontal slit-scan
  - Radial time tunnel (center is present, edges are past)
  - Diagonal wipe
  - Turbulent noise (organic, unpredictable)
- Webcam input for live video
- Mouse control for depth and offset
- Invert option for reversed time direction

## Technical Approach

- Webcam operator captures live video at 1280x720
- FrameCache stores 64 frames (~2 seconds at 30fps)
- Various Gradient operators create displacement patterns
- Noise operator for organic displacement
- TimeMachine samples from cache based on displacement brightness
- Bloom for subtle polish

## Operators to Use

| Operator | Purpose |
|----------|---------|
| Webcam | Live video input |
| FrameCache | Store recent frames |
| Gradient (x4) | Displacement patterns (vertical, horizontal, radial, diagonal) |
| Noise | Organic displacement pattern |
| TimeMachine | Temporal sampling based on displacement |
| Bloom | Subtle glow |

## Interaction

- **Mouse X**: Time depth (how far back to reach)
- **Mouse Y**: Offset bias
- **1**: Vertical slit-scan pattern
- **2**: Horizontal slit-scan pattern
- **3**: Radial time tunnel pattern
- **4**: Diagonal wipe pattern
- **5**: Turbulent noise pattern
- **I**: Invert displacement direction
- **Space**: Reset frame cache

## Visual Style

- Full resolution webcam feed
- Smooth temporal transitions
- Subtle bloom for polish
- Patterns create distinct visual effects:
  - Slit-scan: Photo finish racing effect, stretched faces
  - Radial: Time tunnel, center is "now"
  - Noise: Organic, unpredictable temporal distortion
