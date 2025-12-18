# Testing Fixtures

Core testing fixtures for build verification, visual regression, and hardware-dependent tests.

## Core Fixtures

### build-verification/
Minimal tests for build system verification.
- `hello` - Console-only compile test

### feedback-effects/
Temporal effect testing (Feedback, TimeMachine, FrameCache).
- 2x2 grid: feedback trail, time delay, frame freeze, spiral feedback
- Tests frame accumulation and temporal buffer management

### canvas-compositing/
Canvas features: clip paths, layering, transforms.
- Multi-layer compositing with different blend modes
- Circular clip mask with animation
- Nested canvas drawing

### particle-system/
CPU Particles and GPU PointSprites.
- Left: CPU particles with physics, gravity, lifetime
- Right: GPU point sprites for high particle counts

### retro-suite/
Vintage/retro visual effects gallery.
- 3x2 grid: Original, CRT, Scanlines, Pixelate, Dither, Quantize
- Common source for visual comparison

### blend-modes-all/
Comprehensive blend mode testing.
- 4x4 grid of all CompositeOp blend modes
- Same source pair for consistent comparison

### value-operators/
Core value and modulation operator tests.
- `math-logic-test` - LFO, Math operations, Logic operations

---

## Hardware Fixtures

Tests requiring external hardware (may need to skip in CI).

### hardware/webcam-retro/
Live webcam integration with retro effects.

### hardware/osc-receiver/
OSC input visualization.
- Requires external OSC source (TouchOSC, etc.)
- Default port: 8000
- Messages: `/vivid/color`, `/vivid/position`, `/vivid/size`, `/vivid/effect`

### hardware/dmx-output/
DMX lighting control.
- Requires USB-DMX adapter
- Animates RGB fixture on channels 1-3
- On-screen visualization of DMX values

---

## Addon Fixtures

Testing fixtures in their respective addon directories:

| Addon | Location | Fixtures |
|-------|----------|----------|
| vivid-audio | `addons/vivid-audio/tests/fixtures/` | audio-effects, ambient-melody, synth-test, **audio-reactive**, **fft-spectrum** |
| vivid-render3d | `addons/vivid-render3d/tests/fixtures/` | pbr-demo, multi-lights, shading-modes, ibl-demo, geometry-showcase, render3d-demo, textured-pbr-demo |
| vivid-video | `addons/vivid-video/tests/fixtures/` | video-demo |

### New Audio Fixtures

**audio-reactive/** - Audio levels driving visual parameters
- Shape size/color responds to RMS amplitude
- Ring expands on peak detection
- Bloom intensity modulated by audio

**fft-spectrum/** - FFT frequency visualization
- 32-bar spectrum analyzer
- Bass/mids/highs band indicators
- Multi-oscillator audio source

---

## Running Fixtures

```bash
# Core fixtures
./build/bin/vivid testing-fixtures/feedback-effects
./build/bin/vivid testing-fixtures/blend-modes-all
./build/bin/vivid testing-fixtures/retro-suite

# Hardware fixtures
./build/bin/vivid testing-fixtures/hardware/osc-receiver
./build/bin/vivid testing-fixtures/hardware/dmx-output

# Addon fixtures
./build/bin/vivid addons/vivid-audio/tests/fixtures/audio-reactive
./build/bin/vivid addons/vivid-audio/tests/fixtures/fft-spectrum
```

## Automated Testing

These fixtures support automated testing via snapshot mode:

```bash
# Capture frame 10 for visual regression
vivid testing-fixtures/retro-suite --snapshot output.png --snapshot-frame 10

# Smoke test (run 60 frames, verify no crash)
timeout 5 vivid testing-fixtures/feedback-effects --snapshot /dev/null --snapshot-frame 60
```

### CI Pipeline Usage

```yaml
- name: Visual Regression
  run: |
    for fixture in feedback-effects canvas-compositing particle-system retro-suite blend-modes-all; do
      ./build/bin/vivid testing-fixtures/$fixture --snapshot /tmp/$fixture.png --snapshot-frame 30
      # Compare against reference-images/$fixture.png
    done
```

## Reference Images

Store reference screenshots in `testing-fixtures/reference-images/` for visual diff comparison:

```
reference-images/
├── feedback-effects.png
├── canvas-compositing.png
├── particle-system.png
├── retro-suite.png
└── blend-modes-all.png
```

See [ROADMAP.md](../ROADMAP.md) for the full testing strategy.
