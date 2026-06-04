# Audit 06: GPU, Media & Visual I/O

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit GPU runtime services and visual media paths for resource lifetime, shader contract, platform interop, capture/readback, and media decode robustness risks.

## Scope

- `src/runtime/gpu/`
- `docs/runtime/gpu.md`
- GPU and media-related operators under `operators/gpu/`
- Movie decode/session/upload shared code under `operators/shared/`
- Webcam, Syphon, texture loader, movie, capture, and analysis paths
- GPU, media, operator, and integration tests

## Primary Questions

- [ ] Are WebGPU, Metal, texture, and buffer lifetimes explicit and leak-resistant?
- [ ] Are WGSL parsing and uniform layout rules consistent between runtime and operators?
- [ ] Do media decode/upload/session paths handle reload, seek, failure, and shutdown safely?
- [ ] Are visual I/O operators resilient to missing devices, missing files, and unsupported codecs?
- [ ] Are capture, screenshot, readback, and analysis paths synchronized correctly?
- [ ] Are GPU-backed lane or texture outputs documented and tested?
- [ ] Are platform-specific paths isolated enough for future cross-platform work?

## Subsystem Checklist

- [ ] Trace GPU context initialization, resize, frame submission, and shutdown.
- [ ] Review WGSL header parsing and uniform layout assumptions against representative GPU operators.
- [ ] Inspect movie decode worker/session/queue/upload lifetimes.
- [ ] Check webcam and Syphon behavior for unavailable devices and platform stubs.
- [ ] Review texture readback, screenshot, mipmap, and analysis utilities for synchronization hazards.
- [ ] Verify tests cover shader parse failures, missing media, reload during playback, and capture correctness.
- [ ] Identify repeated GPU resource setup patterns that should become helpers or API contract docs.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings Template

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|

## Completion Criteria

- [ ] Findings table is filled in or explicitly marked with no findings.
- [ ] Resource lifetime findings identify the owning object and cleanup path.
- [ ] Media and GPU findings are separated when their fixes belong to different owners.
- [ ] Platform-specific risks are labeled as macOS-only or cross-platform blockers.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
