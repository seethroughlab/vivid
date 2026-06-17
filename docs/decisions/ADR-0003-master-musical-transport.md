# ADR-0003: Master Musical Transport

Status: accepted

Date: 2026-06-17

## Context

Classic explored multiple timing models, but music authoring needs a shared clock. Session launches,
MIDI clips, theory generators, plugin sync, effects, cue paths, and rhythmic visual bindings all need
common musical time.

## Decision

Vivid 4 has one master musical transport with BPM, time signature, beat, bar, phrase, and launch
quantization.

Local clocks may exist as creative modulation sources, but they do not replace the session's shared
musical grid.

## Consequences

- Temporal plurality without a master clock is rejected for Vivid 4 music authoring.
- Session behavior can branch or wait without becoming a linear timeline.
- Timing concepts should be defined once and reused across UI, agent tools, and runtime plans.
