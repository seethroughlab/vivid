# Operator API

The public C ABI for authoring Vivid operators — the visual, audio, and frame-rate extension
surface. Operators are `.dylib` packages loaded by the host and registered by descriptor name;
built-ins use the same API.

**Current operator ABI: v17.** The host loads any operator built for **[v11, v17]** — an
additive-only range, not an exact match, so an older dylib is never orphaned. Per-version history
and the loadable-range contract live in [`abi-changelog.md`](abi-changelog.md).

## The headers (`app/src/operator_api/`)

- **`types.h`** — the core ABI: descriptors (`VividOperatorDescriptor`, `VividParamDescriptor`,
  `VividPortDescriptor`), the process contexts (`VividGpuContext` / `VividAudioContext` /
  `VividFrameContext`), and the callback signatures. Start here.
- **`operator.h`** — the C++ base classes (`OperatorBase` + the `GpuProcessable` /
  `AudioProcessable` / `FrameProcessable` capability interfaces) that most authors subclass.
- **`value_model.h`** — the value vocabulary every runtime value carries: payload type,
  multiplicity, identity, storage, and the per-operator `VividMultiplicityBehavior` an operator
  declares. Included by `types.h`.
- **`value_view.h`** — the operator-facing value transport (`VividValueView` inputs /
  `VividValueOutput` outputs) the contexts expose as `values[]` / `value_outputs`.
- **`gpu_operator.h`**, **`gpu_common.h`**, **`audio_dsp.h`**, **`metronome_sync.h`** — GPU base
  classes + shared authoring utilities.
- **`operator_descriptor_validation.{h,cpp}`** — descriptor validation run at load (named issue
  codes).

> For the end-to-end authoring walkthrough (choose a kind → descriptor metadata → package → build →
> test → load → expose to agents, with the RT-safety rules), see
> [`../operator-authoring/`](../operator-authoring/). The built-in operators
> (`app/src/gpu/builtin_ops.*`, `app/src/audio/builtin_audio_ops.*`) are the working references, and
> `app/docs/thread-safety.md` covers the RT constraints.
