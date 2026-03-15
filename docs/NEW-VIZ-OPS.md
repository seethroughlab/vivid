# New Visualization Operators

Proposed operator packages and capabilities to expand Vivid's visual platform, organized into logical implementation groups by priority.

---

## Group 1 — Projection Mapping (`vivid-map`)

Table stakes for installation and live show deployment. Without this, Vivid can't serve the professional AV market.

### Quad Warp

Per-surface perspective correction with 4-corner pin control.

- **Parameters:** corner positions (8 floats), interpolation mode
- **Use case:** Mapping textures onto angled surfaces, screens, buildings

### Mesh Warp

Arbitrary mesh-based surface deformation with bezier grid control.

- **Parameters:** grid resolution, control point positions, blend mode
- **Use case:** Complex curved-surface mapping, organic shapes

### Edge Blend

Soft-edge blending for multi-projector overlap regions.

- **Parameters:** blend width, gamma, edge curve
- **Use case:** Seamless multi-projector setups

### Calibration Overlay

Test pattern generator for projector alignment.

- **Parameters:** pattern type (grid, crosshatch, color bars, gradient), line width, opacity
- **Use case:** Setup and alignment workflow

### Multi-Output

Route textures to specific display outputs with per-output crop/transform.

- **Parameters:** display target, crop rect, transform, resolution override
- **Use case:** Multi-projector installations with independent content per output

---

## Group 2 — Output Protocols (`vivid-stage`)

Small effort, big reach. Syphon-only locks Vivid to macOS-to-macOS workflows.

### NDI Output

Network Device Interface output for cross-platform texture sharing over IP.

- **Parameters:** stream name, resolution, frame rate, compression
- **Use case:** Send visuals to any NDI-compatible device on the network

### NDI Input

Receive NDI streams as texture sources.

- **Parameters:** source name/discovery, resolution
- **Use case:** Multi-machine setups, remote camera feeds

### Spout Output (Windows)

Windows-native GPU texture sharing (equivalent to Syphon).

- **Parameters:** sender name, resolution
- **Use case:** Inter-app texture sharing on Windows (Resolume, OBS, etc.)

### Spout Input (Windows)

Receive Spout textures from other applications.

- **Parameters:** sender name
- **Use case:** Compositing with other Windows visual tools

### DMX Output

ArtNet/sACN output for controlling stage lighting from the graph.

- **Parameters:** universe, channel map, protocol (ArtNet/sACN), IP
- **Use case:** Unified visual + lighting control from one graph

### Timecode Sync

SMPTE / MTC timecode input for show synchronization.

- **Parameters:** source (LTC audio, MTC MIDI), frame rate, offset
- **Use case:** Sync visuals to pre-programmed show timelines

---

## Group 3 — Fluid & Physics Simulation (`vivid-sim`)

High-impact "wow" generators that attract creative coders. A single well-done GPU fluid operator would be a signature feature.

### Fluid Sim

2D Navier-Stokes fluid simulation on GPU.

- **Parameters:** viscosity, diffusion, force, velocity dissipation, pressure iterations
- **Inputs:** force texture (mouse/audio-reactive injection), dye texture
- **Use case:** Smoke, ink, water effects driven by audio or interaction

### Reaction-Diffusion

Gray-Scott / Belousov-Zhabotinsky pattern simulation.

- **Parameters:** feed rate, kill rate, diffusion A/B, time step
- **Use case:** Organic growth patterns, biological textures, evolving abstract visuals

### Boids / Flocking

GPU-accelerated flocking simulation with configurable rules.

- **Parameters:** separation, alignment, cohesion, max speed, perception radius, population
- **Use case:** Swarm visuals, particle choreography, audio-reactive flocks

### Cloth Sim

Simple spring-mass cloth simulation.

- **Parameters:** stiffness, damping, gravity, wind, resolution
- **Use case:** Fabric effects, flag animations, deformable surfaces

---

## Group 4 — AI/ML Operators (`vivid-ml`)

Pose detection alone opens up interactive installation work. ONNX Runtime gives broad model access.

### Pose Estimator

Skeleton detection from camera feed (MediaPipe / MoveNet).

- **Parameters:** model (single/multi-person), confidence threshold, smoothing
- **Outputs:** skeleton joints as spread, confidence values
- **Use case:** Body-driven visuals, interactive dance installations

### Depth Estimator

Monocular depth estimation from 2D camera (MiDaS).

- **Parameters:** model quality (fast/balanced/precise), depth range
- **Outputs:** depth texture (grayscale)
- **Use case:** Fake 3D from 2D feed, depth-based effects, parallax

### Segmentation

Background removal and body part segmentation.

- **Parameters:** model, threshold, edge refinement, class selection
- **Outputs:** mask texture, segmented texture
- **Use case:** Green-screen-free background removal, body part isolation

### Style Transfer

Real-time neural style transfer (ONNX models).

- **Parameters:** model path, strength, resolution
- **Use case:** Artistic transformation of camera feeds, painterly effects

### Object Detection

Object and face detection with bounding box output.

- **Parameters:** model, confidence threshold, class filter
- **Outputs:** detection spread (class, bbox, confidence)
- **Use case:** Reactive installations, face-triggered effects

---

## Group 5 — Generative Patterns (`vivid-gen`)

Fun, relatively straightforward GPU compute work that fills out the operator palette. Extends the existing generative vocabulary (Spirograph, Voronoi, etc.).

### Strange Attractor

Lorenz, Rössler, Clifford, and other chaotic attractors rendered as point clouds or trails.

- **Parameters:** attractor type, constants (a, b, c, d), point count, trail length, line width
- **Use case:** Abstract mathematical art, audio-reactive chaos

### L-System

Lindenmayer system renderer for fractal trees and branching structures.

- **Parameters:** axiom, rules, iterations, angle, length, randomness
- **Use case:** Organic branching visuals, procedural trees, growth animations

### Cellular Automaton

Conway's Game of Life and arbitrary rule-based cellular automata on GPU.

- **Parameters:** rule set, grid resolution, birth/survival rules, wrap mode, seed pattern
- **Use case:** Evolving grid patterns, glitch-adjacent generative textures

### Fractal Flame / IFS

Iterated function system renderer for fractal flame visuals.

- **Parameters:** variation set, transform count, color palette, quality, gamma
- **Use case:** Psychedelic visuals, abstract art, VJ content

---

## Group 6 — Color Science (`vivid-color` or core)

Professional color management for broadcast and installation work.

### LUT Apply

3D LUT import and application (.cube, .3dl files).

- **Parameters:** LUT file path, intensity, interpolation mode
- **Use case:** Film emulation, color grading, broadcast color matching

### Color Space Convert

Convert between color spaces (sRGB, Rec.709, DCI-P3, Rec.2020).

- **Parameters:** source space, target space, rendering intent
- **Use case:** Correct color output for specific display/projector profiles

### Scopes

Waveform monitor, vectorscope, and histogram overlays.

- **Parameters:** scope type, opacity, position, size
- **Use case:** Color calibration, exposure monitoring, professional grading workflow

---

## Group 7 — Typography & Vector (`vivid-type`)

Kinetic typography is a major live-visual category currently underserved.

### Rich Text

Multi-font, multi-style text rendering with per-character animation.

- **Parameters:** text content, font, size, color, alignment, character spacing, line height, animation mode
- **Use case:** Titles, lyrics display, kinetic typography

### SVG Render

SVG import and GPU-accelerated rendering.

- **Parameters:** file path, scale, color override
- **Use case:** Vector graphics, logos, icons in visual compositions

### Path Animate

Spline/bezier path animation for motion graphics.

- **Parameters:** path data, speed, loop mode, object attachment
- **Use case:** Motion graphics, animated elements following curves

---

## Group 8 — Timeline & Sequencing

Needed to graduate from "live tool" to "production tool". Currently everything is reactive/live.

### Keyframe Animator

Bezier-curve keyframe animation on any parameter.

- **Parameters:** target node/param, keyframes (time, value, curve), loop mode, playback speed
- **Use case:** Pre-programmed parameter automation, complex choreographed sequences

### Cue List

Show-control cue sequencer with go-to-cue, auto-follow, and cross-fade.

- **Parameters:** cue list (variation + transition + duration), current cue, trigger mode
- **Use case:** Theater, installation, and pre-programmed show playback

### Timeline Playback

Global timeline with transport controls (play, pause, seek, loop region).

- **Parameters:** duration, position, loop start/end, playback speed
- **Use case:** Synchronized multi-element compositions with fixed duration

---

## Suggested Priority

| Priority | Group | Rationale |
|----------|-------|-----------|
| 1 | Projection Mapping | Can't serve installation/live-show market without it |
| 2 | Output Protocols (NDI/Spout) | Small effort, massive reach beyond macOS |
| 3 | Fluid / Physics Sim | Signature feature, magnet for creative coders |
| 4 | AI/ML Operators | Pose detection opens interactive installation work |
| 5 | Timeline / Keyframes | Graduate from live tool to production tool |
| 6 | Generative Patterns | Fills out creative palette, straightforward GPU work |
| 7 | Color Science | Professional deployment needs |
| 8 | Typography / Vector | Important but narrower use case |
