# Custom Port Authoring Contract

## Purpose

Source of truth for authoring custom ports.

The goal is to make it easy to answer:

- when to use a custom port
- how to declare one correctly
- what the runtime requires at load time
- what is allowed across the audio boundary

## The Model

A custom port has two separate identities:

1. a C++ payload type used by operator code
2. a stable runtime type identity used by Vivid across `dlopen`, probing, and tooling

The C++ type gives author-time type safety.
The stable id gives runtime/tooling/package stability.

## Required Pieces

Every custom port type must declare:

- `type_name`
  - valid C++ identifier used in generated/operator code
- `stable_type_id`
  - lowercase namespaced id such as `seethroughlab.vivid.media_stream_v1`
- `transport`
  - `CUSTOM_VALUE` or `CUSTOM_REF`
- `payload_size`
  - `sizeof(T)`-equivalent byte size
- `audio_safe`
  - whether the payload is valid for audio snapshot crossing

## Stable Id Rules

`stable_type_id` is a runtime contract, not display text.

Rules:

- lowercase only
- digits allowed
- `_` allowed
- `.` required as a namespace separator
- no spaces
- no leading or trailing `.`

Good:

- `seethroughlab.vivid.media_stream_v1`
- `tests.vivid.ref_token_v1`

Bad:

- `MediaStreamV1`
- `Tests.Vivid.MediaStream`
- `media stream`

## Transport Rules

### `CUSTOM_VALUE`

Use for small trivially copyable snapshot payloads.

Examples:

- compact analysis payloads
- small immutable control snapshots

### `CUSTOM_REF`

Use for trivially copyable ref-token payloads that identify shared state
indirectly.

Examples:

- media/session handles
- mesh/buffer/session ids with generation fields

`CUSTOM_REF` is not a license to pass arbitrary live object pointers through the
graph.

## Audio Boundary Rules

Audio-crossing custom payloads must be:

- trivially copyable
- bounded
- explicitly marked `audio_safe`

In practice:

- `CUSTOM_VALUE` crossing audio should stay small POD snapshot data
- `CUSTOM_REF` crossing audio should be a small ref-token struct, not raw object
  ownership

If a custom type is not safe for audio crossing, mark `audio_safe = false` and
let the runtime reject that wire shape.

## Authoring Macros

Typed declarations should use the public helpers in:

- [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h)
- [port_type_registry.h](/Users/jeff/Developer/vivid/src/operator_api/port_type_registry.h)

Common pattern:

```cpp
struct MediaStreamV1 {
    uint64_t handle_id = 0;
    uint64_t source_generation = 0;
    uint32_t schema_version = 1;
};

VIVID_DECLARE_CUSTOM_REF_TYPE(
    MediaStreamV1,
    "seethroughlab.vivid.media_stream_v1",
    "MediaStreamV1",
    true
);

// In collect_ports(...)
out.push_back(VIVID_CUSTOM_REF_PORT("media_stream", VIVID_PORT_OUTPUT, MediaStreamV1));

// At file scope
VIVID_DESCRIBE_REF_TYPE(MediaStreamV1)
```

## Scaffolding Rules

`OperatorCreator` now enforces:

- valid custom `type_name`
- valid lowercase namespaced `stable_type_id`
- `payload_size > 0`
- transport must be `CUSTOM_REF` or `CUSTOM_VALUE`
- ports sharing one `stable_type_id` must agree on:
  - `type_name`
  - `transport`
  - `payload_size`
  - `audio_safe`
- built-in ports may not carry stray custom-port metadata

That means scaffolding errors should be treated as contract mistakes, not as
something to “fix up later in generated code.”

## Tooling / Debugging Surfaces

For runtime inspection:

- `list_types`
  - exposes custom port descriptor metadata plus registry-backed fields like
    `audio_safe`
- `get_registry_diagnostics`
  - exposes registered custom port types
  - exposes loader ABI mismatch diagnostics

Use those before assuming a package/operator custom-port issue is in graph
wiring itself.
