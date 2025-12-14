# Testing Fixtures

Core testing fixtures for build verification and hardware-dependent tests. Most addon-specific fixtures have been moved to their respective addon `tests/fixtures/` directories.

## Core Fixtures (this directory)

### build-verification/
Minimal tests for build system verification.
- `hello` - Console-only compile test

### hardware/
Hardware-dependent tests (may need to skip in CI).
- `webcam-retro` - Live webcam integration

### value-operators/
Core value and modulation operator tests.
- `math-logic-test` - LFO, Math operations, Logic operations controlling visual parameters

---

## Addon Fixtures

Testing fixtures have been moved to their respective addon directories:

| Addon | Location | Fixtures |
|-------|----------|----------|
| vivid-effects-2d | `addons/vivid-effects-2d/tests/fixtures/` | effects-gallery, blend-modes, generators, pointsprites, multi-canvas-cube |
| vivid-render3d | `addons/vivid-render3d/tests/fixtures/` | pbr-demo, multi-lights, shading-modes, ibl-demo, geometry-showcase, render3d-demo, textured-pbr-demo |
| vivid-audio | `addons/vivid-audio/tests/fixtures/` | audio-effects, ambient-melody, synth-test |
| vivid-video | `addons/vivid-video/tests/fixtures/` | video-demo |

---

## Running Fixtures

```bash
# Core fixtures
./build/bin/vivid testing-fixtures/build-verification/hello

# Addon fixtures
./build/bin/vivid addons/vivid-effects-2d/tests/fixtures/effects-gallery
./build/bin/vivid addons/vivid-render3d/tests/fixtures/pbr-demo
```

## Future: Automated Testing

These fixtures will be used for:

1. **Visual Regression** - Headless render + screenshot comparison
   ```bash
   vivid testing-fixtures/build-verification/hello --snapshot output.png
   ```

2. **Smoke Tests** - Ensure all fixtures compile and run N frames without crashing

3. **CI Pipeline** - Cross-platform testing on macOS, Windows, Linux

See [ROADMAP.md](../ROADMAP.md) for the full testing strategy.
