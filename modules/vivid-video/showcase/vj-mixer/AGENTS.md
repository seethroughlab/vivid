# VJ Mixer

A live visual performance tool demonstrating multi-layer video mixing, geometry flashes, bold typography, and real-time effects.

## Vision

Create a club VJ / concert visual style application that showcases Vivid's compositing and effect capabilities. High energy, bold graphics, constantly evolving compositions with interactive control.

## Features

### Video Mixing
- 4 video layers with individual enable/disable
- Switchable blend modes (Add, Screen, Multiply, Difference)
- Crossfade control between layer pairs
- Fallback to generated noise when videos aren't available

### Geometry Flashes
- Circle, Triangle, Rectangle, Ring shapes
- Random colors from a curated palette
- Scale-up and fade-out animation
- Triggered on keypress (G)

### Typography
- Large bold text flashes
- Word bank: DROP, BASS, VIVID, BEAT, FLOW, etc.
- Scale and fade animation
- Requires TTF font at assets/fonts/

### Effects Pipeline
- Feedback trails (toggle with F)
- Bloom for glow
- Chromatic aberration on flash hits
- Continuous color cycling

## Controls

| Key | Action |
|-----|--------|
| 1-4 | Toggle video layers on/off |
| Q | Blend mode: Add |
| W | Blend mode: Screen |
| E | Blend mode: Multiply |
| R | Blend mode: Difference |
| SPACE | Trigger flash effect |
| T | Flash random text |
| G | Flash 2D geometry |
| F | Toggle feedback trails |
| UP/DOWN | Crossfade between layer pairs |
| TAB | Parameter controls |

## Required Assets

Users should provide their own video content:
- `assets/videos/loop1.mov`
- `assets/videos/loop2.mov`
- `assets/videos/loop3.mov`
- `assets/videos/loop4.mov`

Font for typography (included):
- `assets/fonts/space age.ttf`

## Technical Approach

### Operator Chain
```
VideoPlayers/Noise → HSV coloring → Composite mixers → Canvas (shapes)
    → Canvas (text) → Feedback → Bloom → ChromaticAberration → HSV → Output
```

### Key Patterns Used
- Multiple Composite operators for layered mixing
- Canvas for immediate-mode 2D drawing (shapes + text)
- Feedback for trail effects
- State variables for animation (alpha, scale decay)

## Operators Used

| Operator | Purpose |
|----------|---------|
| VideoPlayer | Video playback (4 instances) |
| Noise | Fallback content when videos unavailable |
| HSV | Color adjustment and cycling |
| Composite | Multi-layer blending |
| Canvas | 2D shapes and text rendering |
| Feedback | Trail/echo effects |
| Bloom | Glow effect |
| ChromaticAberration | RGB separation on hits |

## Extension Ideas

- Add audio reactivity (BeatDetect triggers flashes)
- MIDI controller support for real-time mixing
- More geometry types (stars, polygons)
- Video scrubbing controls
- Preset system for different visual styles
