# Testing Fixtures

Examples preserved for the test suite. These are functional but were moved from `examples/` to keep the user-facing examples focused and curated.

## Categories

### build-verification/
Minimal tests for build system verification.
- `hello` - Console-only compile test

### video-codecs/
Video codec compatibility testing.
- `video-demo` - Tests H.264, HEVC, ProRes, HAP, MJPEG

### hardware/
Hardware-dependent tests (may need to skip in CI).
- `webcam-retro` - Live webcam integration

### audio/
Audio processing and sequencing tests.
- `audio-effects` - Effect chain (delay, reverb, compressor, etc.)
- `ambient-melody` - Complex sequencer with song structure
- `synth-test` - Oscillators, envelopes, filters, mixers with visual feedback

### shaders/
Shader and lighting validation.
- `pbr-demo` - Basic PBR shader test
- `multi-lights` - Point, spot, directional lights
- `shading-modes` - All 6 shading mode variations
- `ibl-demo` - Image-based lighting

### geometry/
Geometry and primitive generation tests.
- `geometry-showcase` - All primitive types
- `render3d-demo` - Basic 3D rendering smoke test

### materials/
Material system tests.
- `textured-pbr-demo` - 19 PBR materials gallery

### particles/
Particle and point rendering tests.
- `pointsprites` - Point sprite pattern rendering

### 2d-effects/
2D texture effect and generator tests.
- `effects-gallery` - Mirror, Edge, Dither, Pixelate, Quantize, ChromaticAberration, Scanlines, Vignette
- `blend-modes` - All Composite blend modes (Over, Add, Multiply, Screen, Overlay, Difference)
- `generators` - Shape types, Gradient types, Noise types, Ramp, SolidColor

### value-operators/
Value and modulation operator tests.
- `math-logic-test` - LFO, Math operations, Logic operations controlling visual parameters

### canvas/
Canvas 2D drawing API tests.
- `multi-canvas-cube` - Multiple canvases composited with 3D

---

## Running Fixtures

```bash
./build/bin/vivid testing-fixtures/shaders/pbr-demo
```

## Future: Automated Testing

These fixtures will be used for:

1. **Visual Regression** - Headless render + screenshot comparison
   ```bash
   vivid testing-fixtures/shaders/pbr-demo --headless --frames 10 --output pbr-demo.png
   ```

2. **Smoke Tests** - Ensure all fixtures compile and run N frames without crashing

3. **CI Pipeline** - Cross-platform testing on macOS, Windows, Linux

See [ROADMAP.md](../ROADMAP.md) for the full testing strategy.
