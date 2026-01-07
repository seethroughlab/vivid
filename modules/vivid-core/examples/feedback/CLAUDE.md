# Feedback

Demonstrates temporal feedback trails with zoom, rotation, and decay. Shows how Feedback preserves state across frames to create persistent motion blur and spiral effects.

## Vision

Noise particles drift across the screen leaving colorful trails that zoom and rotate, creating mesmerizing spiral patterns. The feedback effect accumulates previous frames with controllable decay.

## Operators

| Operator | Purpose |
|----------|---------|
| Noise | Source particles/spots |
| Feedback | Accumulate trails with transform |
| Ramp | Radial HSV color gradient |
| Composite | Multiply feedback with color |

## Interaction

- **Mouse X** - Rotation speed (-0.02 to 0.02 rad/frame)
- **Mouse Y** - Trail decay (0.85 to 0.98)

## Key Parameters

- `decay` - How much each frame fades (0.92 = 8% per frame)
- `mix` - Blend ratio of new input vs feedback (0.3 = 30% new)
- `zoom` - Scale per frame (1.002 = slight zoom out)
- `rotate` - Rotation per frame in radians
