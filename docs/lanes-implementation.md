# Lanes Implementation

*Engineering guide for making the repo match Vivid's general multiplicity model.*

## Goal

Vivid should read as if lanes were designed in from the beginning. A contributor should learn one rule:

- **lanes are the multiplicity model**
- **payload kind is orthogonal to multiplicity**
- **execution strategy is orthogonal to both**

This applies across payload kinds. Float lanes and string lanes are both first-class lane-bearing values. Differences between them are capability differences, not model differences.

## Core Model

A value in Vivid is:

- **payload kind + lane set**

Where:

- payload kind answers what each lane carries
- lane set answers how many parallel elements there are and how they relate
- execution strategy answers how the runtime evaluates them

Current built-in lane-bearing payload kinds include:

- float lanes via `VIVID_PORT_LANE_ARRAY`
- string lanes via `VIVID_PORT_STRING_LANES`

Neither payload kind defines the lane model by itself. The lane model is shared. Storage, operators, and backend support may differ by payload kind, but those differences do not create separate collection systems.

## Canonical Public Surfaces

These are the canonical multiplicity-facing surfaces for the repo:

- `VIVID_PORT_LANE_ARRAY`
- `VIVID_PORT_STRING_LANES`
- `VIVID_PORT_TRANSPORT_LANE_ARRAY`
- `VIVID_PORT_TRANSPORT_STRING_LANES`
- `VividLanePort`
- `VividStringLanePort`
- `input_lanes`, `output_lanes`
- `input_string_lanes`, `output_string_lanes`

Canonical runtime/UI/control-server strings:

- `lane_array`
- `string_lanes`

These names encode two independent dimensions:

- payload kind (`float`, `string`, etc.)
- multiplicity (`lanes`)

## Clean-Break Naming Rules

### 1. Use lane terms for multiplicity-bearing surfaces

Use `...LANE...` / `...LANES` when a surface exists because a value can carry many parallel elements.

### 2. No new spread-named multiplicity surfaces

No new multiplicity-bearing surface may use `spread`.

Remaining acceptable uses of `spread` are only:

- clearly marked historical references
- unrelated domain terms such as stereo spread or spatial spread

### 3. Locked naming decisions

These naming decisions are not open for re-litigation:

- `VIVID_PORT_STRING_SPREAD` → `VIVID_PORT_STRING_LANES`
- `VIVID_PORT_TRANSPORT_STRING_SPREAD` → `VIVID_PORT_TRANSPORT_STRING_LANES`
- `string_spread` → `string_lanes`
- `SpreadSourceOp` → `LaneSourceOp`
- `SpreadSinkOp` → `LaneSinkOp`
- `IdentitySpreadSourceOp` → `IdentityLaneSourceOp`

Prefer one clean vocabulary over old/new aliases.

## Compiler and Runtime Expectations

### Payload kind and multiplicity are separate

Compiler legality, lane provenance, lane behavior, and lane identity are lane-model concerns. They are not float-specific concerns.

That means:

- lane legality is payload-agnostic in principle
- lane provenance is payload-agnostic in principle
- execution strategy is not part of the payload-kind definition

### Capability differences are allowed

Payload kinds may differ in:

- available operators
- backend support
- storage representation
- performance characteristics

But those are capability differences, not model differences. A string lane value is still a lane value.

### String-lane support is lane support

String lanes are not transport-only. String-lane work belongs to the lane model.

That means future work on strings should be framed as:

- string-lane routing
- string-lane reshape/reduction
- payload-generic lane operators where semantics are shared

not as a separate collection system for strings.

## Preferred Operator Direction

The preferred long-term direction is:

- lane reshape/reduction operators become payload-generic where semantics are shared

Examples:

- `Repeat`
- `Tile`
- `Select`

If payload-generic implementation is not immediate, temporary string-specific operators are acceptable only if they are clearly part of the same lane operator family and the generic end state remains the documented target.

## Alignment Checklist

Status of the clean-break checklist:

- [x] rename `string_spread` API/UI/control-server strings to `string_lanes`
- [x] rename lane test fixtures to lane-oriented names (LaneSourceOp, LaneSinkOp, IdentityLaneSourceOp)
- [x] remove contributor-facing comments that describe lane-bearing transport as spreads
- [x] update string-lane tests and comments to use lane vocabulary
- [x] ensure docs do not describe strings as a spread-era exception

## Acceptance Bar

The repo should feel clean-slate to a newcomer when these are true:

1. A contributor reading the lane docs sees one multiplicity model across payload kinds.
2. `string_lanes` is the public/runtime term, not `string_spread`.
3. Strings are described as first-class lane payloads with no maturity caveat.
4. Remaining uses of `spread` are only historical references or unrelated domain terms.
5. Reshape/reduction support for strings is framed as lane-family work, not separate collection work.
