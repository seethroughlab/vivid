# v0.1.1 Release Testing Checklist

Use this checklist to verify functionality before tagging the release.

Legend: **Mac** = macOS, **Win** = Windows, **RPi** = Raspberry Pi

---

## Build Verification

| Test                                                         | Mac | Win | RPi |
|--------------------------------------------------------------|-----|-----|-----|
| `cmake -B build && cmake --build build` completes            | [x] | [ ] | [ ] |
| `./build/bin/vivid --version` shows correct version          | [x] | [ ] | [ ] |
| All addons build (check `build/lib/` for dylib/dll/so files) | [x] | [ ] | [ ] |

---

## Core Features

### Hot Reload

| Test                                                          | Mac | Win | RPi |
|---------------------------------------------------------------|-----|-----|-----|
| `./build/bin/vivid examples/getting-started/02-hello-noise`   | [x] | [ ] | [ ] |
| Edit chain.cpp while running → changes apply without restart  | [x] | [ ] | [ ] |
| No crashes on repeated hot-reloads                            | [x] | [ ] | [ ] |

### Chain Visualizer

| Test                                              | Mac | Win | RPi |
|---------------------------------------------------|-----|-----|-----|
| Press `Tab` to toggle visualizer                  | [x] | [ ] | [ ] |
| Node graph renders correctly (Sugiyama layout)    | [ ] | [ ] | [ ] |
| Mini-map click-to-navigate works                  | [ ] | [ ] | [ ] |
| Keyboard navigation: `F` (fit), `1` (100%), Enter | [ ] | [ ] | [ ] |
| Parameter inspector shows when node selected      | [ ] | [ ] | [ ] |
| Slider adjustments update preview in real-time    | [ ] | [ ] | [ ] |

### Snapshot Mode

| Test                                     | Mac | Win | RPi |
|------------------------------------------|-----|-----|-----|
| `--snapshot /tmp/test.png` creates PNG   | [ ] | [ ] | [ ] |

---

## 2D Effects

| Example                                                        | Mac | Win | RPi |
|----------------------------------------------------------------|-----|-----|-----|
| `examples/getting-started/01-hello-vivid` - Gradient renders   | [ ] | [ ] | [ ] |
| `examples/getting-started/02-hello-noise` - Animated noise     | [ ] | [ ] | [ ] |
| `examples/getting-started/03-hello-feedback` - Feedback effect | [ ] | [ ] | [ ] |
| `examples/2d-effects/blur-bloom` - Blur and bloom visible      | [ ] | [ ] | [ ] |
| `examples/2d-effects/color-effects` - HSV/color manipulation   | [ ] | [ ] | [ ] |

---

## 3D Rendering

### Basic 3D

| Test                                                              | Mac | Win | RPi |
|-------------------------------------------------------------------|-----|-----|-----|
| `examples/3d-rendering/3d-basics` - Geometry renders with lighting| [ ] | [ ] | [ ] |
| `examples/3d-rendering/pbr-materials` - PBR shading works         | [ ] | [ ] | [ ] |

### Shadows (New in v0.1.1)

| Test                                                           | Mac | Win | RPi |
|----------------------------------------------------------------|-----|-----|-----|
| `testing-fixtures/shadow-comprehensive` - All shadow types     | [ ] | [ ] | [ ] |
| Directional light shadows visible                              | [ ] | [ ] | [ ] |
| Point light shadows visible                                    | [ ] | [ ] | [ ] |
| Spot light shadows visible                                     | [ ] | [ ] | [ ] |
| PCF soft edges visible (not hard pixelated)                    | [ ] | [ ] | [ ] |

### Fog (New in v0.1.1)

| Test                                                     | Mac | Win | RPi |
|----------------------------------------------------------|-----|-----|-----|
| `examples/3d-rendering/fog-test` - Fog fades distant obj | [ ] | [ ] | [ ] |
| Near objects visible, far objects fade to fog color      | [ ] | [ ] | [ ] |

### Debug Gizmos

| Test                                              | Mac | Win | RPi |
|---------------------------------------------------|-----|-----|-----|
| Camera frustum wireframe visible when enabled     | [ ] | [ ] | [ ] |
| Light direction/cone wireframes visible           | [ ] | [ ] | [ ] |

---

## Audio

### Playback

| Test                                                        | Mac | Win | RPi |
|-------------------------------------------------------------|-----|-----|-----|
| `examples/audio/audio-reactive` - Audio plays, no pops      | [ ] | [ ] | [ ] |
| No buffer underruns during playback                         | [ ] | [ ] | [ ] |
| FFT visualization responds to audio                         | [ ] | [ ] | [ ] |

### Synthesis (New in v0.1.1)

| Test                                                        | Mac | Win | RPi |
|-------------------------------------------------------------|-----|-----|-----|
| `testing-fixtures/wavetable-test` - Wavetable synth plays   | [ ] | [ ] | [ ] |
| `testing-fixtures/multi-sampler-test` - MultiSampler works  | [ ] | [ ] | [ ] |

### Audio-Visual Recording

| Test                                   | Mac | Win | RPi |
|----------------------------------------|-----|-----|-----|
| Record button in visualizer works      | [ ] | [ ] | [ ] |
| Exported video has synchronized audio  | [ ] | [ ] | [ ] |
| No audio glitches during recording     | [ ] | [ ] | [ ] |

---

## Video

### Playback

| Test                                                        | Mac | Win | RPi |
|-------------------------------------------------------------|-----|-----|-----|
| `examples/video/video-basics` - Video plays                 | [ ] | [ ] | [ ] |
| HAP codec video plays                                       | [ ] | N/A | N/A |

### Export

| Test                             | Mac | Win | RPi |
|----------------------------------|-----|-----|-----|
| Video export produces valid file | [ ] | [ ] | [ ] |
| Snapshot export works            | [ ] | [ ] | [ ] |

---

## Addons

### vivid-midi

| Test                                              | Mac | Win | RPi |
|---------------------------------------------------|-----|-----|-----|
| MIDI input detection works (if controller avail)  | [ ] | [ ] | [ ] |
| `testing-fixtures/midi-test` runs without errors  | [ ] | [ ] | [ ] |

### vivid-serial

| Test                                        | Mac | Win | RPi |
|---------------------------------------------|-----|-----|-----|
| Serial port enumeration works               | [ ] | [ ] | [ ] |
| DMX output works (if Enttec device avail)   | [ ] | [ ] | [ ] |

### vivid-network

| Test                      | Mac | Win | RPi |
|---------------------------|-----|-----|-----|
| OSC receive works         | [ ] | [ ] | [ ] |
| WebSocket connection works| [ ] | [ ] | [ ] |

---

## MCP Server (Claude Integration)

| Test                                          | Mac | Win | RPi |
|-----------------------------------------------|-----|-----|-----|
| `./build/bin/vivid mcp` starts without errors | [ ] | [ ] | [ ] |
| Responds to JSON-RPC initialize request       | [ ] | [ ] | [ ] |
| `list_operators` returns operator list        | [ ] | [ ] | [ ] |
| `search_docs` returns documentation results   | [ ] | [ ] | [ ] |

---

## Memory & Stability

### Memory Leak Test

| Test                                            | Mac | Win | RPi |
|-------------------------------------------------|-----|-----|-----|
| Run example for 5 min, memory stays stable      | [ ] | [ ] | [ ] |

### Stress Test

| Test                                            | Mac | Win | RPi |
|-------------------------------------------------|-----|-----|-----|
| Rapid hot-reload (save file 10+ times quickly)  | [ ] | [ ] | [ ] |
| No crashes or hangs                             | [ ] | [ ] | [ ] |

---

## Platform-Specific

### macOS

| Test                                        | Status |
|---------------------------------------------|--------|
| Window resizing works                       | [ ]    |
| Fullscreen toggle (`F` key) works           | [ ]    |
| Retina display renders at correct resolution| [ ]    |

### Windows

| Test                           | Status |
|--------------------------------|--------|
| Window resizing works          | [ ]    |
| Fullscreen toggle works        | [ ]    |
| DPI scaling handled correctly  | [ ]    |

### Raspberry Pi

| Test                                      | Status |
|-------------------------------------------|--------|
| Basic examples run at acceptable framerate| [ ]    |
| No GPU driver errors                      | [ ]    |

---

## Documentation

| Test                              | Status |
|-----------------------------------|--------|
| README examples still accurate    | [ ]    |
| CHANGELOG.md reflects all changes | [ ]    |
| docs/LLM-REFERENCE.md up to date  | [ ]    |

---

## Sign-off

| Platform | Tester | Date | Pass/Fail |
|----------|--------|------|-----------|
| macOS    |        |      |           |
| Windows  |        |      |           |
| RPi      |        |      |           |

**Ready for v0.1.1 tag:** [ ] Yes / [ ] No
