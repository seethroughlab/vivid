# GPU Demo Repro

This document is the repeatable repro path for GPU-only demo issues that headless smoke cannot
classify on its own.

## Why This Exists

`test_demo_graphs` is intentionally conservative:

- movie/media graphs defer to `test_media_headless`
- external I/O graphs are skipped in headless smoke
- GPU-only graphs skip when no usable headless GPU adapter is available

That makes the headless lane trustworthy, but it also means some GPU/demo issues need a separate
windowed repro path.

## Current Classification Procedure

For a GPU-only graph such as `rich_text_demo.json`, run both:

### 1. Headless/demo harness path

```bash
./build/test_demo_graphs ./build/graphs rich_text_demo
```

Interpretation:

- `PASS`: the headless GPU path is healthy
- `SKIP: needs GPU`: no usable headless adapter, move to the windowed path
- `FAIL`: investigate as a possible headless GPU/runtime problem

### 2. Windowed runtime path

```bash
./build/vivid graphs/gpu/rich_text_demo.json \
  --screenshot /tmp/rich_text_demo_windowed.png \
  --screenshot-delay 20
```

Interpretation:

- screenshot written successfully: the normal windowed runtime path is healthy
- crash/hang/device error: investigate as a possible GPU/runtime or operator-local problem

## Ownership Matrix

Use the two runs together to classify ownership:

| Headless | Windowed | Likely Owner |
|---|---|---|
| fail | fail | GPU/runtime or operator-local bug |
| skip | pass | headless infrastructure / environment gap |
| pass | fail | window/surface lifecycle bug |
| pass | pass | no active issue |

## Current Read For `rich_text_demo.json`

As of the current audit follow-up:

- headless path still skips on this machine because there is no usable headless adapter
- windowed screenshot path succeeds cleanly

So the open issue is no longer classified as a general GPU/runtime crash. The current owner is
**test infrastructure / environment coverage**, with optional later operator-specific follow-up only
if a GPU-capable failing repro reappears.
