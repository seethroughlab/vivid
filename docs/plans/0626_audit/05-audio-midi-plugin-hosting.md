# Audit 05: Audio, MIDI & Plugin Hosting

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the audio runtime, MIDI integration, and native plugin-hosting paths for real-time safety, thread-boundary correctness, device lifecycle reliability, and plugin discovery risks.

## Scope

- `src/runtime/audio/`
- `docs/runtime/audio_engine.md`
- Audio-related parts of `src/runtime/graph/`
- Audio, MIDI, AU, VST3, and CLAP operators
- Relevant shared code under `operators/shared/`
- Audio, operator, lane, and integration tests that exercise audio cadence or plugin hosting

## Primary Questions

- [ ] Are real-time audio paths free of blocking operations, unbounded allocation, and unsafe locks?
- [ ] Are audio/frame bridge snapshots coherent and race-safe?
- [ ] Are device start, stop, switch, and failure paths recoverable?
- [ ] Are MIDI message lifetimes and routing semantics explicit?
- [ ] Are AU/VST3/CLAP scan and host boundaries clear and failure-tolerant?
- [ ] Do audio operators consistently handle sample rate, block size, lanes, and reset behavior?
- [ ] Are audio tests strong enough without relying on fragile device availability?

## Subsystem Checklist

- [ ] Trace audio callback execution and identify every dependency it touches.
- [ ] Review `AudioFrameBridge` usage from both audio and frame cadences.
- [ ] Inspect MIDI input/output and clock routing for timestamp and ownership assumptions.
- [ ] Review plugin scanning and host shared code for UI-thread, audio-thread, and background-thread boundaries.
- [ ] Check audio operators for denormal, clipping, reset, bypass, and preset consistency.
- [ ] Verify tests cover reload during audio processing, multi-lane audio, missing devices, and plugin scan failures.
- [ ] Identify places where plugin-specific code duplicates shared host behavior.

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
- [ ] Real-time safety findings are called out with high priority.
- [ ] Thread-boundary assumptions are documented or flagged.
- [ ] Audio test gaps distinguish device-dependent and device-free coverage.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
