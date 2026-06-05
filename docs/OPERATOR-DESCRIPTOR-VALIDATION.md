# Operator Descriptor Validation

Every operator's `VividOperatorDescriptor` is validated by `validate_descriptor()`
(`src/runtime/operators/operator_descriptor_validation.{h,cpp}`) **at dylib load time**
(`OperatorLoader::load()` → `operator_loader.cpp`). If any issue is found, the load is rejected:
the dylib is `dlclose`d, `load()` returns false, and the first issue is reported via
`OperatorLoader::last_error()` (code `"invalid_descriptor"` + message). The full issue list is also
surfaced as structured `{code, message}` objects through the MCP `validate_operators` tool.

Each issue has a **stable code** (declared as named constants in
`vivid::validation_codes`, `operator_descriptor_validation.h`) and a human-readable **message**.
Issues whose offending field has a name are reported by name; issues where the *name itself* is
missing are reported by index (you can't name a thing that has no name).

Almost every code is impossible to hit when an operator is built normally through
`operator_codegen` / `VIVID_DEFINE_OP` — they guard hand-written or malformed descriptors and the
generated uniform layout. They are listed here so authors and tools can act on a failure.

## Descriptor-level

| Code | Triggers when | Fix |
|------|---------------|-----|
| `null_descriptor` | `vivid_descriptor()` returned null | Ensure the operator exports a descriptor (use codegen / `VIVID_DEFINE_OP`). |
| `missing_name` | `desc->name` is null/empty | Set `static constexpr const char* kName` to a non-empty stable id. |
| `null_params` | `param_count > 0` but `params == nullptr` | Provide the params array, or set `param_count = 0`. |
| `null_ports` | `port_count > 0` but `ports == nullptr` | Provide the ports array, or set `port_count = 0`. |
| `missing_capability` | none of `has_process_frame/audio/gpu` set | Inherit one domain mixin (`FrameProcessable` / `AudioProcessable` / `GpuProcessable`). |

## Params

| Code | Triggers when | Fix |
|------|---------------|-----|
| `param_missing_name` | a param has a null/empty name | Name every `Param<T>` member. |
| `duplicate_param_name` | two params share a name | Make param names unique within the operator. |
| `param_missing_choice_labels` | `choice_count > 0` but `choice_labels == nullptr` | Provide the enum label array, or set `choice_count = 0`. |
| `param_missing_default_string` | a `FILE`/`TEXT` param has `default_string == nullptr` | Give the file/text param a default string (may be `""`). |

## Ports

| Code | Triggers when | Fix |
|------|---------------|-----|
| `port_missing_name` | a port has a null/empty name | Name every port. |
| `duplicate_port_name` | two ports of the same direction share a name | Make input port names unique, and output port names unique (an input and output may share a name). |
| `custom_port_missing_type_name` | a `CUSTOM_REF`/`CUSTOM_VALUE` transport port has no `type_name` | Set the port's `type_name` to its C++ type, or register it via `port_type_registry`. |

## Generated uniform layout (GPU operators with an inline `Uniforms` struct)

| Code | Triggers when | Fix |
|------|---------------|-----|
| `uniform_layout_missing_name` | layout has no `struct_name` | Regenerate via codegen (it sets the struct name). |
| `uniform_layout_empty` | layout `byte_size == 0` | The `Uniforms` struct has no members / zero size — add fields or remove the struct. |
| `uniform_layout_not_16_byte_aligned` | `byte_size` not a multiple of 16 | Pad the WGSL `Uniforms` struct to a 16-byte multiple (std140-style). |
| `uniform_layout_missing_members` | `member_count > 0` but `members == nullptr` | Regenerate the layout. |
| `uniform_member_missing_name` | a member has no name | Regenerate the layout. |
| `duplicate_uniform_member_name` | two members share a name | Make `Uniforms` field names unique. |
| `uniform_member_missing_type` | a member has no `wgsl_type` | Regenerate the layout. |
| `uniform_member_zero_size` | a member has zero size | Check the field type — regenerate the layout. |
| `uniform_member_zero_alignment` | a member has zero alignment | Regenerate the layout. |
| `uniform_member_out_of_order` | member offsets are not monotonic | Regenerate the layout (offsets must ascend in declaration order). |
| `uniform_member_out_of_bounds` | a member's `offset + size` exceeds `byte_size` | Regenerate the layout; the declared size is too small for the members. |

## See also
- `src/operator_api/CLAUDE.md` — operator contract & the ABI boundary
- `docs/OPERATOR-LOADING.md` — load/probe flow and where validation runs
- `src/runtime/operators/operator_descriptor_validation.h` — `validation_codes::*` constants
