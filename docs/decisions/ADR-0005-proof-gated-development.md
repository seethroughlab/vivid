# ADR-0005: Proof-Gated Development

Status: accepted

Date: 2026-06-17

## Context

Vivid Classic often proved ideas by implementing them deeply, then later discovering product or
vocabulary issues. Vivid 4 needs to prove usability before promoting concepts into runtime,
schema, API, or native UI architecture.

## Decision

Vivid 4 uses proof-gated development.

Every major feature slice starts with a user task, hypothesis, pressure test, expected evidence, and
failure modes. Disposable prototypes and mocked agent workflows may precede native implementation.

## Consequences

- HTML mocks and task scripts are valid early artifacts.
- Implementation plans are written after product shape survives pressure testing.
- Promotion into runtime/API/schema requires evidence from a native slice.
