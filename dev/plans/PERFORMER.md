# Performer Features

Vivid's built-in UI serves the performer — someone running visuals live, whether at a show, an installation, or a rehearsal. They need large readable controls, physical hardware integration, and reliability under pressure. Development happens outside Vivid; performance happens inside it.

---

## Shipped

### Preset / Snapshot System
Save and recall named parameter snapshots with hard cut or crossfade interpolation. Organized into setlists for cue-based show flow. Triggered via keyboard or MIDI.

### Chain Health Monitoring
Visual indicators on chain visualizer nodes: thumbnail border colors (green = signal, yellow = dark/silent, red = NaN/static), inline VU meters on audio nodes, and frame delta sparklines for activity tracking.

---

## Planned

### Parameter Control Surface
Evolve the Inspector into a performer-oriented control surface. The current Inspector shows every parameter on every operator — useful for development, overwhelming for performance.

**What it looks like:**
- Authors mark parameters as "performable" — only those appear on the surface
- Parameters grouped by purpose (Color, Motion, Audio Reactivity) rather than by operator
- Large, high-contrast sliders and labels designed for dark rooms and distance viewing
- Minimal chrome — the surface is the entire panel, no tree navigation

**Live use cases:**
- DJ/VJ adjusts "intensity" and "color shift" mid-set without hunting through operator trees
- Installation artist exposes 3-4 knobs for gallery visitors to play with
- Theater tech has a clean surface matching their cue sheet: "Scene Warmth", "Fog Density", "Strobe Rate"

### MIDI Controller Mapping
Bind physical MIDI knobs, faders, and buttons to any parameter or preset trigger.

**What it looks like:**
- Learn mode: click a parameter, move a MIDI knob, done
- Mappings saved per-project (alongside chain.cpp)
- Supports CC messages for continuous controls, note messages for triggers/toggles
- Works with any class-compliant MIDI controller (Akai APC, Novation LaunchControl, Korg nanoKONTROL, etc.)

**Live use cases:**
- Performer maps 8 faders on a nanoKONTROL to their most-used parameters — no screen interaction needed during the set
- Pad grid triggers preset snapshots: tap pad 1 for "verse look", pad 5 for "drop look"
- Knob-per-parameter muscle memory — performer knows fader 3 is always "bloom amount" without looking

### Live Streaming
Stream Vivid's output over the network for remote viewing or distributed setups.

**What it looks like:**
- WebRTC or RTMP output from the render pipeline
- Headless mode for dedicated render servers
- Configurable resolution and bitrate independent of the local window

**Live use cases:**
- Remote collaboration: artist performs locally, creative director watches the stream and gives feedback in real time
- Multi-room installation: one Vivid instance drives displays in several rooms via network stream
- Live event: Vivid output streams directly to a platform (Twitch, YouTube) without external capture software
