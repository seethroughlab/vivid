# Authoring a Vivid operator

An **operator** is Vivid's unit of extension. You write one C++ struct, drop it in a package
directory with a manifest, and the app compiles it into a loadable `.dylib` at install time — no
app rebuild. Built-in operators use the same API, so the built-ins are your reference implementations.

This guide walks from **choosing a kind** → authoring → packaging → building → testing → loading →
exposing it to agents. The API surface itself is in [`../operator-api/`](../operator-api/) (current
operator ABI **v11**); real-time rules are in [`../../app/docs/thread-safety.md`](../../app/docs/thread-safety.md).

## 1. Choose a kind

Every operator implements `OperatorBase` plus exactly one capability interface that fixes its
execution cadence:

| Kind (manifest) | Interface | Runs on | Ports | Example |
|---|---|---|---|---|
| `gpu_visual` | `GpuProcessable` | GPU submit (main thread) | texture out (+ texture in for effects) | [`example-visuals/gradient.cpp`](../../app/operators/packages/example-visuals/gradient.cpp) |
| `audio_effect` | `AudioProcessable` | audio thread (~48 kHz) | one stereo audio in + one stereo audio out | [`example-audio/drive.cpp`](../../app/operators/packages/example-audio/drive.cpp) |
| `instrument` | `AudioProcessable` | audio thread | one stereo audio out (reads note events) | [`example-audio/sine_synth.cpp`](../../app/operators/packages/example-audio/sine_synth.cpp) |
| `frame` | `FrameProcessable` | main thread (~60 Hz) | scalar/value ports | `app/tests/fixtures/pkg/noop.cpp` |

The `kind` is declared in the manifest; it defaults the wgpu link (only `gpu_visual` links wgpu).
The op's descriptor capability flags remain the runtime authority.

## 2. Author the operator

The shape is the same for every kind (see the examples for full code):

```cpp
#include "operator_api/operator.h"     // + gpu_operator.h / gpu_common.h for gpu_visual
#include <array>
#include <vector>

struct MyOp : vivid::OperatorBase, vivid::AudioProcessable {   // pick the interface for your kind
    static constexpr const char* kName        = "MyOp";        // registry name (unique)
    static constexpr const char* kDisplayName = "My Op";
    static constexpr const char* kSummary     = "One line an agent/user reads to pick this op.";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "effect", "mine" };

    vivid::Param<float> amount{ "amount", 0.5f, 0.f, 1.f };    // name, default, min, max

    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&amount); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { /* declare ports */ }

    void process_audio(const VividAudioContext* c) override { /* your DSP */ }
};

VIVID_REGISTER(MyOp)   // emits the extern "C" ABI surface the loader binds to
```

- **Params** are `vivid::Param<float|int|bool>`; declare display hints / semantic metadata on them
  for better inspectors + agent mapping (see `operator_api/types.h`).
- **Read live values from the context**, not the member, inside a process callback: the host writes
  automated/mapped values into `ctx->param_values[i]` (index = `collect_params` order).

### Audio operator contract (v1)

The audio runtime is single-stereo-port. An **effect** declares exactly one stereo audio input and
one stereo audio output; an **instrument/generator** declares one stereo audio output and no audio
input, and renders from `ctx->note_events` (on/off with `sample_offset`, `pitch`, `velocity`,
`note_id`). Declare audio ports with `channels = 2` (or `0` = auto). Extra audio ports or non-stereo
channel counts are **rejected at load** ([`operator_descriptor_validation`](../../app/src/operator_api/operator_descriptor_validation.h)) — they would be silently dropped otherwise.

**`process_audio` runs on the audio thread: it must be real-time safe — no allocation, no locks, no
I/O, bounded work per block.** Do heavy setup in the constructor or `prepare_instance_assets()`. See
[`thread-safety.md`](../../app/docs/thread-safety.md).

## 3. Package it

A package is a directory with a `vivid-package.json` and the source files:

```json
{
  "name": "my-pack",
  "version": "0.1.0",
  "operators": [
    { "name": "MyOp", "kind": "audio_effect", "source": "my_op.cpp" }
  ]
}
```

One package can hold several operators of different kinds (see `example-audio`, which ships an
`audio_effect` + an `instrument`).

## 4. Build, install, load

- **From an agent (MCP):** `install_operator_package("<abs path to the package dir>")` compiles each
  operator and installs it. Restart the app (or it is scanned on next launch) and the op is registered.
- **What happens:** the package compiler invokes `clang++` against `operator_api/` + your source,
  producing a `.dylib` in the managed operators dir (`~/Library/Application Support/Vivid/operators`,
  or `$VIVID_OPERATORS_DIR`). The startup scan `dlopen`s it, validates the descriptor (loud named
  codes on any issue), and registers it — flowing through the same registry as built-ins.

## 5. Test it

Follow the package smoke tests as templates:
[`test_package_audio.cpp`](../../app/tests/test_package_audio.cpp) (compile → load → **run** an
audio effect + instrument and assert their output) and
[`test_package_compile.cpp`](../../app/tests/test_package_compile.cpp) (the compile+load pipeline).
For pure DSP, unit-test your math directly (headless, no app).

## 6. Expose it to agents

Discovery is automatic once registered:

- **Visual operators:** `list_operators` (MCP) — the spawnable catalog; then `add_node` /
  `set_node_param` / `connect_nodes`.
- **Native audio operators:** `list_audio_operators` (MCP) returns `{instruments, effects}`; then
  `set_track_audio_instrument` / `add_audio_effect` / `set_audio_op_param`, and `get_audio_graph` to
  inspect the resulting chain.

Rich `kSummary` + `kKeywords` + semantic param metadata make an operator far easier for an agent to
find and wire correctly.

## Not yet: custom inspectors & editors

Operators can't yet ship a **custom inspector** (a compound-widget param panel) or a **custom editor**
(a full canvas — a drum grid, an envelope). The ABI structs exist in `operator_api/types.h`, but the
loader + UI don't wire them for packages yet — that's the editors/inspectors ABI (tracked as UI-4).
Until then, author params with display hints/semantic metadata and let the host render them.
