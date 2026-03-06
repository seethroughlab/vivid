# Semantic Parameter Tags Spec (v1)

Date: 2026-03-06
Status: Accepted baseline (Milestone 8 follow-up, Semantic Tags Phase 0)

## Purpose

Define a stable, machine-readable semantic layer for operator parameters so tooling (especially LLM workflows) can make better decisions about defaults, wiring, hints, and safe conversions.

This spec defines taxonomy only. It does not require any runtime behavior changes yet.

## Scope and Non-Goals

Scope:
- Controlled vocabulary for semantic tags.
- Value-shape categories.
- Optional units and intent metadata.
- Compatibility and forward-evolution rules.

Non-goals (for v1):
- No automatic rewiring.
- No mandatory tag coverage for all operators.
- No graph-file schema change requirement.

## Metadata Model (Conceptual)

Each parameter may expose:
- `semantic_tag`: primary meaning label from controlled vocabulary.
- `shape`: data shape category (scalar/vector/color/event/etc.).
- `unit` (optional): unit string from allowed set for that tag.
- `intent` (optional): human/tool hint such as `input_gain`, `cutoff_primary`, `tempo_sync`.

The metadata is authored in operator code/metadata, not in user parameter values.

## Controlled Vocabulary (v1)

### Timing and Transport
- `time_seconds`
- `time_milliseconds`
- `phase_01`
- `bpm`
- `beats`
- `sample_rate_hz`

### Audio Control
- `frequency_hz`
- `amplitude_linear`
- `gain_db`
- `pan`
- `q_factor`
- `resonance`
- `gate`
- `trigger`
- `midi_note`
- `midi_velocity`

### Visual and Spatial
- `color_rgba`
- `position_xy`
- `position_xyz`
- `scale_xy`
- `scale_xyz`
- `rotation_degrees`
- `rotation_radians`
- `uv`
- `resolution_px`

### Simulation/Control Utility
- `seed`
- `probability_01`
- `count`
- `index`
- `enabled`

### Content/Resource Paths
- `path_audio`
- `path_image`
- `path_video`
- `path_font`

## Value Shapes (v1)

- `scalar`
- `vec2`
- `vec3`
- `vec4`
- `color`
- `bool`
- `int`
- `enum`
- `event`
- `string`
- `path`
- `pattern`

Shape compatibility is independent of semantic meaning. Example: `frequency_hz` is usually `scalar`; `position_xy` is `vec2`.

## Units (v1)

Allowed units for common tags:
- `frequency_hz`: `Hz`
- `time_seconds`: `s`
- `time_milliseconds`: `ms`
- `gain_db`: `dB`
- `rotation_degrees`: `deg`
- `rotation_radians`: `rad`
- `bpm`: `bpm`
- `resolution_px`: `px`

Rules:
- Unit is optional but recommended for ambiguous numeric tags.
- If present, unit must be compatible with the semantic tag.

## Compatibility Rules

- Untagged parameters are valid and must continue to work unchanged.
- Unknown tags must be tolerated and treated as untyped hints (never fatal at runtime).
- Unknown shapes must be ignored (fallback to existing type behavior).
- Tooling may warn on invalid/unknown tags, but load/evaluation must remain non-breaking.

## Serialization Boundary

- Graph JSON remains source-of-truth for parameter values.
- Semantic metadata is not required in graph JSON for v1.
- If metadata is eventually serialized, it must be optional and non-authoritative versus operator definitions.

## Validation Rules (for future lint/tests)

- `semantic_tag` must match controlled vocabulary or extension namespace rule.
- `shape` must match allowed shape set.
- If `unit` exists, it must be in the allowed unit list for the selected tag.
- `gate`/`trigger` should use shape `event` or boolean-compatible control type.
- `path_*` tags should use shape `path` or `string`.

## Extension Policy

To avoid taxonomy drift:
- Core vocabulary additions require PR review and doc update in this file.
- Package-local experimental tags must be namespaced: `x_<package>_<name>` (example: `x_vivid3d_sdf_blend_mode`).
- Namespaced tags are never promoted automatically; promotion requires explicit normalization into core vocabulary.

## Examples

Good:
- cutoff param: `semantic_tag=frequency_hz`, `shape=scalar`, `unit=Hz`
- envelope gate input: `semantic_tag=gate`, `shape=event`
- texture path: `semantic_tag=path_image`, `shape=path`

Anti-patterns:
- Using `frequency_hz` with `unit=ms`
- Using generic `count` for semantically rich params like MIDI note
- Encoding multiple meanings in one tag (`frequency_or_rate`)

## Operator Authoring Guidance: When To Tag vs Not To Tag

Use semantic tags to help tooling reason about **meaning**, not just type.

Tag a parameter when:
- The parameter has clear, stable intent across operators (examples: frequency, time, gain, gate, color, file path).
- The value is likely to be modulated, auto-wired, or adjusted by MCP/LLM workflows.
- Unit context matters and ambiguity would hurt (`Hz`, `s`, `ms`, `dB`, `deg`, `rad`, `px`).
- You want inspector hints and schema output to expose stronger guidance.

Do not tag (or defer tagging) when:
- The parameter is purely internal/temporary and likely to be renamed.
- Meaning is highly operator-specific and no stable vocabulary fit exists yet.
- The parameter mixes multiple concepts (split it first if possible).
- You are unsure of stable semantics; leave untagged instead of guessing.

Practical rule:
- Prefer **untagged** over **wrongly tagged**.
- Start with only high-impact params (one to three per operator), then expand.

Recommended minimum for new scaffolded operators:
- Tag at least one primary modulation/control parameter (e.g. `amount`, `gain`, `frequency`, `time`).
- Add `shape` for every tagged parameter.
- Add `unit` whenever numeric scale is not obvious from tag alone.

Examples:
- Good: `gain` -> `amplitude_linear`, `shape=scalar`
- Good: `cutoff` -> `frequency_hz`, `shape=scalar`, `unit=Hz`
- Good: `file` -> `path_audio|path_image|path_video`, `shape=path`
- Avoid: tagging `mode` enum as `count` (prefer namespaced extension like `x_pkg_mode` if needed)
- Avoid: tagging a generic `value` with a specific physical unit unless the operator contract guarantees it

## Versioning

- This document defines taxonomy version `v1`.
- Future updates should be additive when possible.
- Breaking semantic renames require compatibility aliases during transition.
