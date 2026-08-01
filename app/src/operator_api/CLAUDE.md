# `app/src/operator_api/`

**This is the one public, versioned surface.** It is the C ABI a loadable operator (dylib
or shader-as-operator) compiles against. A leaf module — it includes only its own headers,
never `app/audio/gpu/ui/...`. Treat every struct here as published: third-party operators
built against an older-but-additive ABI must keep working.

## The ABI contract (read before editing any struct)

- Version lives in `types.h`: `VIVID_OPERATOR_ABI_VERSION` (current) and
  `VIVID_OPERATOR_ABI_MIN_LOADABLE` (oldest still loadable). The host accepts a **range**
  `[MIN_LOADABLE, VERSION]`, enforced in `gpu/operator_loader.cpp` (`abi < min || abi >
  current → reject`). Never narrow this to equality — it orphans every installed dylib.
- **Additive-only.** Add new fields at the **END** of the context structs and bump
  `VERSION`. Bump `MIN_LOADABLE` **only** for a non-additive change (a field removed,
  reordered, or resized) — that is a breaking change that orphans older dylibs.
- Per-version history + rationale: **`docs/operator-api/abi-changelog.md`** and the inline
  block at `types.h:11-37`.
- Beyond the version int, the loader also enforces structural compat: old params must be a
  prefix (`param_layout_compatible`), ports must match exactly (`port_layout_compatible`).

## Key files

- `operator.h` — `OperatorBase` + the `FrameProcessable / AudioProcessable /
  GpuProcessable` mixins and the `VIVID_REGISTER` macro that emits the exported C entry
  points (`vivid_abi_version / vivid_descriptor / vivid_create / vivid_destroy /
  vivid_process_*`).
- `types.h` — the ABI version constants + the core descriptor/context structs
  (`VividParamDescriptor`, `VividPortDescriptor`, `VividOperatorDescriptor`,
  `VividFrameContext`, `VividAudioContext`, `VividNoteEvent`, ...).
- `gpu_operator.h` — `VividGpuContext`. Note it embeds **webgpu-native** handles
  (`WGPUDevice/Queue/...`) by value — an intentional external coupling (see
  `docs/decisions/ADR-0044-operator-abi-webgpu-native-coupling.md`).
- `value_model.h` / `value_view.h` — the value vocabulary + operator-facing value
  transport. `semantic_vocab.h`, `shader_meta.*`, `operator_descriptor_validation.*` —
  metadata + validation.
