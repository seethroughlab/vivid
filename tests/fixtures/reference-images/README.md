# Reference Images for Visual Regression Testing

This directory contains golden master reference images for visual regression testing.

## Directory Structure

```
reference-images/
  2d-effects/
    chain-basics.png
    feedback.png
    kaleidoscope.png
    particles.png
    retro-crt.png
  operators/
    noise.png
    gradient.png
    blur.png
    ...
```

## Generating Reference Images

Use snapshot mode to capture reference images:

```bash
# Capture a reference image from an example
./build/bin/vivid examples/2d-effects/chain-basics --snapshot testing-fixtures/reference-images/2d-effects/chain-basics.png --snapshot-frame 30

# Capture with specific frame (allows warm-up)
./build/bin/vivid examples/2d-effects/feedback --snapshot testing-fixtures/reference-images/2d-effects/feedback.png --snapshot-frame 60
```

## Running Visual Regression Tests

```bash
# Generate test snapshot
./build/bin/vivid examples/2d-effects/chain-basics --snapshot /tmp/test-chain-basics.png --snapshot-frame 30

# Compare with reference (using ImageMagick)
compare -metric RMSE testing-fixtures/reference-images/2d-effects/chain-basics.png /tmp/test-chain-basics.png /tmp/diff.png 2>&1
```

## Updating Reference Images

When intentionally changing visual output:

1. Run the example to verify the new output looks correct
2. Regenerate the reference image with the snapshot command above
3. Commit the updated reference image

## Notes

- Reference images should be generated at 1280x720 (default resolution)
- Use --snapshot-frame 30+ to allow warm-up for animated effects
- Some effects are time-dependent; these may need special handling
