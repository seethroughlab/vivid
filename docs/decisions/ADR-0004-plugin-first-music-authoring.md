# ADR-0004: Plugin-First Music Authoring

Status: accepted

Date: 2026-06-17

## Context

Vivid Classic's VST3/CLAP/AU work showed that mature instruments and effects dramatically improve
music authoring. It also showed that trying to rival full synth workstations inside Vivid would pull
attention away from the environment.

## Decision

Vivid 4 is plugin-first for music authoring.

Native audio operators should stay modest: routing, mixing, analysis, utility processing, reference
implementations, and helpers that support the session environment.

## Consequences

- Plugin state preservation is a core product promise.
- Native synth/effect breadth is not an early success metric.
- Agent workflows should understand plugins, presets, roles, and musical variation at the session
  level.
