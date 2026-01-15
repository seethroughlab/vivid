# Vision

A temporal displacement effect that bends time across space, letting different parts of the frame show different moments from the past, creating slit-scan, time-tunnel, and temporal smearing visuals.

## Aesthetic Goals
- Otherworldly time distortion with ghostly trails of motion
- Multiple displacement patterns: vertical/horizontal slit-scan, radial tunnels, turbulent noise
- Surreal, dreamlike quality where movement leaves temporal echoes

## Techniques Demonstrated
- FrameCache storing ~2 seconds of webcam history
- TimeMachine operator sampling cached frames via grayscale displacement maps
- Multiple gradient types (linear, radial) as temporal selectors
- Interactive mouse control for depth and offset parameters
