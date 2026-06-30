# Semantic parameter & port tags

Operator params and ports can carry **semantic hints** beyond their raw type, so agents
and tooling can reason about meaning ("this is a frequency in Hz", "this output is an
analysis signal") rather than guessing from names. The fields live on every param/port
descriptor:

- `semantic_tag` — *what the value means* (e.g. `frequency_hz`, `gate`, `color_rgba`).
- `semantic_shape` — *structural form* (e.g. `scalar`, `vec2`, `color`, `event`).
- `semantic_unit` — *unit* for params (e.g. `Hz`, `dB`, `ms`).
- `semantic_intent` — a free-form role hint (e.g. `input_gain`). **Not vocabulary-checked.**

All fields are **optional** — leave them unset when there's nothing meaningful to say.

## The contract

If a field *is* set, its value must come from the vocabulary below, or use the `x_`
**custom-extension** namespace (e.g. `x_my_house_metric`) for project-specific meaning.
This is enforced by `vivid::validate_semantic_metadata()` in
[`app/src/operator_api/semantic_vocab.h`](../app/src/operator_api/semantic_vocab.h) — the
header IS the source of truth; this doc tracks it. `tests/test_semantic_metadata.cpp`
locks the checker; the same function can be run live over any descriptor (built-in or
dlopen'd) to flag drift.

Today the only tag our built-in operators actually declare is `analysis` (on the standard
rms/peak/waveform output ports, via `append_analysis_ports`). The rest of the vocabulary
is seeded for operators to grow into.

## `semantic_tag`

| Group | Tags |
|-------|------|
| audio / analysis | `analysis`, `amplitude_linear`, `gain_db`, `frequency_hz`, `pan`, `resonance`, `q_factor`, `bpm`, `beats`, `phase_01`, `time_seconds`, `time_milliseconds`, `midi_note`, `midi_velocity`, `gate`, `trigger`, `sample_rate_hz` |
| visual / spatial | `color_rgba`, `position_xy`, `position_xyz`, `scale_xy`, `scale_xyz`, `rotation_degrees`, `rotation_radians`, `uv`, `resolution_px` |
| generic | `seed`, `probability_01`, `count`, `index`, `enabled`, `path_audio`, `path_image`, `path_video`, `path_font` |

## `semantic_shape`

`scalar`, `vec2`, `vec3`, `vec4`, `color`, `bool`, `int`, `enum`, `event`, `string`,
`path`, `pattern`

## `semantic_unit`

`Hz`, `s`, `ms`, `dB`, `deg`, `rad`, `bpm`, `px`

## Extending

To add a vocabulary term: add it to the relevant set in `semantic_vocab.h` and to the
table above, in the same change. For one-off or experimental meaning, prefer an `x_`
custom tag rather than widening the shared vocabulary.
