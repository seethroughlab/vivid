# Port Type Registry — One-Shot ABI Break Spec

This document replaces the earlier phased HANDLE-migration plan.

The new assumption is explicit:

- We do not need backward compatibility with the old `VIVID_PORT_HANDLE` model.
- We do not need intermediate states where old and new systems coexist.
- We do need one internally coherent final model before implementation starts.

The goal is to make package-defined port types first-class while keeping runtime transport rules explicit and safe.

## Final Decision

`VIVID_PORT_HANDLE` is removed entirely.

`VividPortType` becomes extensible, so each custom C++ payload type gets its own port type id. But type identity alone is not enough for the runtime. The runtime must also know how values of that type move through the system.

So the final model has two orthogonal concepts:

- `type_id`: what the payload is
- `transport`: how the payload moves

This is the key correction to the earlier design. The old document treated custom types as if they all shared one transport mechanism. That is not true in the current codebase, and it would not be true in a clean redesign either.

## Final Model

### Built-in vs custom types

Built-in port types remain fixed constants:

```cpp
typedef uint32_t VividPortType;

#define VIVID_PORT_FLOAT          0
#define VIVID_PORT_AUDIO          1
#define VIVID_PORT_SPREAD         2
#define VIVID_PORT_STRING         3
#define VIVID_PORT_STRING_SPREAD  4
#define VIVID_PORT_TEXTURE        5
```

Custom types use hashed ids in the high-bit range:

```cpp
template<typename T>
constexpr VividPortType vivid_port_type() {
    return vivid_type_id<T>() | 0x80000000u;
}

inline bool vivid_is_custom_port_type(VividPortType t) {
    return (t & 0x80000000u) != 0;
}
```

### Transport class

Every port descriptor must also declare a transport class:

```cpp
typedef enum VividPortTransport {
    VIVID_PORT_TRANSPORT_SCALAR         = 0, // float-like main-thread copy
    VIVID_PORT_TRANSPORT_AUDIO_BUFFER   = 1, // audio sample buffers
    VIVID_PORT_TRANSPORT_SPREAD         = 2, // float spread copy
    VIVID_PORT_TRANSPORT_STRING         = 3, // string copy
    VIVID_PORT_TRANSPORT_STRING_SPREAD  = 4, // string spread copy
    VIVID_PORT_TRANSPORT_TEXTURE        = 5, // GPU texture/view routing
    VIVID_PORT_TRANSPORT_CUSTOM_VALUE   = 6, // memcpy-by-value snapshot
    VIVID_PORT_TRANSPORT_CUSTOM_REF     = 7  // opaque shared-handle/reference
} VividPortTransport;
```

This is the runtime truth that replaces the overloaded HANDLE bucket.

Examples:

- `float` uses `SCALAR`
- `texture` uses `TEXTURE`
- `MediaStreamV1` should use `CUSTOM_REF`
- a small POD scene fragment might use `CUSTOM_VALUE`

### Final descriptor shape

```cpp
typedef struct VividPortDescriptor {
    const char*            name;
    VividPortType          type;
    VividPortDirection     direction;
    VividPortTransport     transport;
    uint32_t               payload_size;  // 0 for non-custom built-ins
    const char*            type_name;     // "vivid::MediaStreamV1", NULL for built-ins
} VividPortDescriptor;
```

This is intentionally the final ABI shape. There is no temporary alias field such as `handle_type_id`.

## Authoring API

### Built-in ports

Built-in ports remain simple:

```cpp
out.push_back({"phase", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SCALAR, 0, nullptr});
out.push_back({"input", VIVID_PORT_AUDIO, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr});
out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr});
```

### Custom ports

Custom ports use a new macro:

```cpp
#define VIVID_CUSTOM_PORT(port_name, dir, CppType, TransportKind) \
    VividPortDescriptor { \
        (port_name), \
        vivid_port_type<CppType>(), \
        (dir), \
        (TransportKind), \
        sizeof(CppType), \
        #CppType \
    }
```

Examples:

```cpp
out.push_back(VIVID_CUSTOM_PORT("media_stream",
                                VIVID_PORT_OUTPUT,
                                vivid::MediaStreamV1,
                                VIVID_PORT_TRANSPORT_CUSTOM_REF));

out.push_back(VIVID_CUSTOM_PORT("scene_out",
                                VIVID_PORT_OUTPUT,
                                mypkg::SceneFragmentV1,
                                VIVID_PORT_TRANSPORT_CUSTOM_VALUE));
```

There is no `VIVID_HANDLE_PORT` compatibility macro.

## Payload Rules

Custom payload types must declare which transport they expect. That determines the validity rules.

### `CUSTOM_VALUE`

For `VIVID_PORT_TRANSPORT_CUSTOM_VALUE`:

- must be POD / trivially copyable
- must not require destructors
- may cross control/audio/GPU boundaries only through fixed-size copies
- must fit the configured snapshot limit for any boundary it crosses

Default rule:

- if a `CUSTOM_VALUE` type is ever consumed by audio, it must be `<= 256` bytes

Important: this is not a global rule for all custom types. It is a rule for audio-bridgeable value types.

### `CUSTOM_REF`

For `VIVID_PORT_TRANSPORT_CUSTOM_REF`:

- payload is an opaque runtime-managed reference/handle
- cross-thread delivery uses handle snapshots, not raw struct copies
- audio thread may resolve or consume only via pre-approved lock-free/runtime-safe access patterns

This is the right category for things like media sessions and future shared runtime resources.

## Registration

Packages that define custom types must export a registration function. This is not optional in the final model for packages that use custom types.

```cpp
typedef struct VividPortTypeInfo {
    uint32_t                type_id;
    const char*             type_name;
    const char*             package_name;
    uint32_t                payload_size;
    uint32_t                abi_version;
    VividPortTransport      transport;
    const char*             description;
} VividPortTypeInfo;

void vivid_register_port_type(const VividPortTypeInfo* info);
const VividPortTypeInfo* vivid_lookup_port_type(uint32_t type_id);
uint32_t vivid_list_port_types(const VividPortTypeInfo** out, uint32_t max);
```

Package export:

```cpp
extern "C" void vivid_register_types(void) {
    static const VividPortTypeInfo info = {
        vivid_port_type<mypkg::SceneFragmentV1>(),
        "mypkg::SceneFragmentV1",
        "my_3d_package",
        sizeof(mypkg::SceneFragmentV1),
        1,
        VIVID_PORT_TRANSPORT_CUSTOM_VALUE,
        "Scene fragment carrying transform and mesh/material ids"
    };
    vivid_register_port_type(&info);
}
```

Collision rules:

- same `type_id`, different `payload_size`: reject package
- same `type_id`, different `transport`: reject package
- same `type_id`, different `abi_version`: reject package
- same everything: accept as shared type definition

## Runtime Rules

### Scheduler

The scheduler must validate both:

- `from_port.type == to_port.type`
- `from_port.transport == to_port.transport`

The old HANDLE-specific path disappears completely.

This means the scheduler becomes simpler conceptually, but not because transport disappears. It becomes simpler because transport is explicit instead of hidden inside one special enum case plus `handle_type_id`.

### Audio bridge

The audio engine must stop hardcoding `MediaStreamV1`.

There are now two generic cases:

- `CUSTOM_VALUE`: copy `payload_size` bytes into a fixed snapshot buffer
- `CUSTOM_REF`: copy a runtime-managed handle/reference token into an audio-safe snapshot structure

This means the current `HandleInputSnapshot` concept should be replaced by something transport-aware, not just renamed.

Suggested shape:

```cpp
struct CustomPortSnapshot {
    bool valid = false;
    VividPortType type = 0;
    VividPortTransport transport = VIVID_PORT_TRANSPORT_CUSTOM_VALUE;
    uint32_t byte_size = 0;
    static constexpr uint32_t kMaxBytes = 256;
    uint8_t bytes[kMaxBytes] = {};
};
```

If a type is `CUSTOM_VALUE` and `payload_size > kMaxBytes`, it is not audio-bridgeable and graph build should fail for control->audio or gpu->audio crossings.

### Control server / MCP / UI

All introspection must expose:

- `type`
- `type_name`
- `transport`
- `payload_size`

The current `"handle"` string is removed from public introspection.

The graph editor should present custom ports by `type_name`, not by a generic `"handle"` category.

## Scope of the ABI Break

This should be done as one deliberate ABI break, not as multiple transitional phases.

That means changing all of these together:

- `src/operator_api/types.h`
- `src/operator_api/type_id.h`
- `src/runtime/scheduler.h`
- `src/runtime/scheduler.cpp`
- `src/runtime/audio_engine.h`
- `src/runtime/audio_engine.cpp`
- `src/runtime/control_server.cpp`
- `src/runtime/operator_registry.h`
- `src/runtime/operator_registry.cpp`
- `src/ui/node_graph_util.h`
- `src/runtime/operator_creator.cpp`
- any core operators currently using HANDLE-era ports

The goal is that after the rewrite:

- no runtime code refers to `VIVID_PORT_HANDLE`
- no runtime code refers to `handle_type_id`
- no runtime code uses `input_handles` / `output_handles` naming for the generic custom-type path

## Migration Policy

There is no compatibility layer.

Specifically:

- old plugins/operators must be rebuilt
- old package binaries are invalid until rebuilt
- any old graph metadata that still refers to HANDLE-era editor labels can be updated directly

This is acceptable because Vivid is still pre-release and the cleaner design is worth the break.

## What This Enables

- Package-defined types become first-class and introspectable.
- The runtime can distinguish value types from reference/session types without a HANDLE special-case.
- Audio-domain safety becomes generic instead of hardcoded to known types.
- UI and MCP can talk about real types instead of a catch-all `"handle"` bucket.
- Future conversion logic can be added on top of a coherent base.

## Deferred

These are explicitly not part of the one-shot rewrite:

- automatic conversion between custom types
- inheritance/interface-style type compatibility
- per-graph type environments
- backward compatibility shims for old HANDLE plugins

## Open Questions

1. Should `CUSTOM_REF` snapshots carry raw handle ids, or a small opaque runtime token type separate from handle ids?
2. Should the 256-byte `CUSTOM_VALUE` audio snapshot limit remain fixed, or become a compile-time constant exposed in one header?
3. Should custom-type headers be expected in a standard package location such as `include/port_types/` for cross-package reuse?
4. Should the registry be loaded at package scan time or only when a package is activated/instantiated?

## Key Correction From The Earlier Draft

The earlier draft was right to eliminate HANDLE as a type identity mechanism.

It was wrong to imply that custom types no longer need explicit transport semantics.

The clean design is:

- remove HANDLE
- keep type ids first-class
- make transport class explicit

That is the model this document now specifies.
