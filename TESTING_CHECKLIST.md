# v0.1.2 Release Testing Checklist

Use this checklist to verify functionality before tagging the release.

## Build Verification

### macOS (Apple Silicon)
- [ ] `cmake -B build && cmake --build build` completes without errors
- [ ] `./build/bin/vivid --version` shows correct version
- [ ] All addons build: vivid-audio, vivid-video, vivid-render3d, vivid-midi, vivid-serial, vivid-network

### Windows
- [ ] CI build passes (GitHub Actions)
- [ ] Local build completes (if available)

### Raspberry Pi
- [ ] CI cross-compile passes (GitHub Actions)

---

## Core Features

### Hot Reload
- [ ] `./build/bin/vivid examples/getting-started/02-hello-noise`
- [ ] Edit chain.cpp while running → changes apply without restart
- [ ] No crashes on repeated hot-reloads

### Chain Visualizer
- [ ] Press `V` to toggle visualizer
- [ ] Node graph renders correctly (Sugiyama layout)
- [ ] Mini-map shows and click-to-navigate works
- [ ] Keyboard navigation: `F` (fit), `1` (100%), arrows, Enter
- [ ] Parameter inspector shows when node selected
- [ ] Slider adjustments update preview in real-time

### Snapshot Mode
- [ ] `./build/bin/vivid examples/getting-started/02-hello-noise --snapshot /tmp/test.png`
- [ ] PNG file created successfully

---

## 2D Effects

### Run each and verify visual output:
- [ ] `examples/getting-started/01-hello-vivid` - Gradient renders
- [ ] `examples/getting-started/02-hello-noise` - Animated noise
- [ ] `examples/getting-started/03-hello-feedback` - Feedback effect
- [ ] `examples/2d-effects/blur-bloom` - Blur and bloom visible
- [ ] `examples/2d-effects/color-effects` - HSV/color manipulation

---

## 3D Rendering

### Basic 3D
- [ ] `examples/3d-rendering/3d-basics` - Geometry renders with lighting
- [ ] `examples/3d-rendering/pbr-materials` - PBR shading works

### Shadows (New in v0.1.2)
- [ ] `testing-fixtures/shadow-comprehensive` - All shadow types render
- [ ] Directional light shadows visible
- [ ] Point light shadows visible
- [ ] Spot light shadows visible
- [ ] PCF soft edges visible (not hard pixelated)

### Fog (New in v0.1.2)
- [ ] `examples/3d-rendering/fog-test` - Fog fades distant objects
- [ ] Near objects visible, far objects fade to fog color

### Debug Gizmos
- [ ] Camera frustum wireframe visible when enabled
- [ ] Light direction/cone wireframes visible when enabled

---

## Audio

### Playback
- [ ] `examples/audio/audio-reactive` - Audio plays without clicks/pops
- [ ] No buffer underruns during playback
- [ ] FFT visualization responds to audio

### Synthesis (New in v0.1.2)
- [ ] `testing-fixtures/wavetable-test` - Wavetable synth plays
- [ ] `testing-fixtures/multi-sampler-test` - MultiSampler loads and plays

### Audio-Visual Recording
- [ ] Record button in visualizer works
- [ ] Exported video has synchronized audio
- [ ] No audio glitches during recording

---

## Video

### Playback
- [ ] `examples/video/video-basics` - Video plays (if test video available)
- [ ] HAP codec video plays (macOS)

### Export
- [ ] Video export produces valid file
- [ ] Snapshot export works on all platforms

---

## Addons

### vivid-midi
- [ ] MIDI input detection works (if controller available)
- [ ] `testing-fixtures/midi-test` runs without errors

### vivid-serial
- [ ] Serial port enumeration works
- [ ] DMX output works (if Enttec device available)

### vivid-network
- [ ] OSC receive works
- [ ] WebSocket connection works

---

## MCP Server (Claude Integration)

- [ ] `./build/bin/vivid mcp` starts without errors
- [ ] Responds to JSON-RPC initialize request
- [ ] `list_operators` returns operator list
- [ ] `search_docs` returns documentation results

---

## Memory & Stability

### Memory Leak Test
- [ ] Run `examples/getting-started/02-hello-noise` for 5 minutes
- [ ] Memory usage stays stable (no continuous growth)

### Stress Test
- [ ] Rapid hot-reload (save file 10+ times quickly)
- [ ] No crashes or hangs

---

## Platform-Specific

### macOS
- [ ] Window resizing works
- [ ] Fullscreen toggle (`F` key) works
- [ ] Retina display renders at correct resolution

### Windows
- [ ] Window resizing works
- [ ] Fullscreen toggle works
- [ ] DPI scaling handled correctly

### Raspberry Pi
- [ ] Basic examples run at acceptable framerate
- [ ] No GPU driver errors

---

## Documentation

- [ ] README examples still accurate
- [ ] CHANGELOG.md reflects all changes
- [ ] docs/LLM-REFERENCE.md up to date

---

## Sign-off

| Platform | Tester | Date | Pass/Fail |
|----------|--------|------|-----------|
| macOS    |        |      |           |
| Windows  |        |      |           |
| RPi      |        |      |           |

**Ready for v0.1.2 tag:** [ ] Yes / [ ] No
