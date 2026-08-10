# Authoring a Vivid operator

An **operator** is Vivid's unit of extension. You write one C++ struct, drop it in a package
directory with a manifest, and the app compiles it into a loadable `.dylib` at install time — no
app rebuild. Built-in operators use the same API, so the built-ins are your reference implementations.

This guide walks from **choosing a kind** → authoring → packaging → building → testing → loading →
exposing it to agents. The API surface itself is in [`../operator-api/`](../operator-api/) (current
operator ABI **v17**); real-time rules are in [`../../app/docs/thread-safety.md`](../../app/docs/thread-safety.md).

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

### Vendored header libraries (`dependencies.vendor`)

If an operator needs a **header-only** library (nlohmann/json, stb, linmath, …), vendor its headers
inside the package and declare the include dir in the manifest:

```json
{
  "name": "geometry-pack",
  "operators": [ { "name": "MeshLoad", "kind": "gpu_visual", "source": "mesh_load.cpp" } ],
  "dependencies": {
    "vendor": [ { "name": "nlohmann_json", "include": "deps/json/include" } ]
  }
}
```

Each `dependencies.vendor[].include` is a **package-relative** directory added as a `-I` to every
operator in the package, so `mesh_load.cpp` can `#include <nlohmann/json.hpp>`. `name` is a label
only. This is portable — the headers ship *inside* the package, nothing is fetched or linked from the
build machine. The path is resolved at parse time and **must stay within the package directory** and
be a real directory, or the whole manifest is rejected (a `../..` escape is an error, not a warning).

This covers header-only and single-file-source libraries. Operators that need a *compiled* library or
a system framework (FreeType, AVFoundation, …) are not yet supported on the package compile path — see
ADR-0054's appendix (Stages 2–3).

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

## Node thumbnails (two mechanisms)

Every operator should have a **rich, dynamic node-card thumbnail** (ADR-0042). There are **two paths**,
and knowing which one applies to your op avoids the "my `draw_thumbnail` is empty — is my thumbnail
missing?" confusion:

- **Path A — `draw_thumbnail(const VividThumbnailContext*)`** (CPU 2D vector API). Override this virtual
  and draw with `ctx->draw` (a `VividDrawAPI`: `draw_rect`/`draw_line`/`draw_circle`/`draw_text`/…),
  reading **only** the read-only `ctx->param_values` snapshot (never your live `Param<>` members — the
  call is UI-thread + read-only), and `ctx->time` for subtle animation. `VIVID_REGISTER` auto-emits the
  `vivid_draw_thumbnail` export. In the audio domain this is used specifically for **scene-cell note
  generators** (kind 8): `Euclid` (Euclidean ring + orbiting playhead), `Chord`, `RandMelody` in
  `app/src/audio/builtin_audio_ops.cpp` — their thumbnail shows in the session-grid clip cell and the
  generator card's preview strip. Visual ops rarely need Path A (see Path B).

- **Path B — render into your own `output_texture_view` during `process_gpu`.** A visual op's node card
  simply blits its output texture. Ops that already produce a texture (Image, CustomShader, …) get a
  correct thumbnail **for free** — leave `draw_thumbnail` empty. Ops that emit a `Scene3D` fragment or
  value lanes (Shape3D, Deformer, LaneRamp, Clock, Switch3D, …) produce **no** texture, so their card
  would be blank; they render a small animated preview into their output texture via the header-only
  helpers `vivid::thumb3d::` (3D — `render`, `render_instances_cpu`, `render_proxy_sphere`, …) or
  `vivid::lanethumb::` (flat 2D bars/cells). For these, **a stubbed `draw_thumbnail` is fine** — the
  thumbnail is the Path-B render.

**So:** a stubbed `draw_thumbnail` + a `thumb3d`/`lanethumb` call in `process_gpu` = a working thumbnail.
A stubbed `draw_thumbnail` + no Path-B render = a genuinely **blank** card (the audit will flag it).

### Preview purpose — why you're being asked to draw (v17, ADR-0050)

`draw_thumbnail` is really an **operator-drawn compact preview**, and the *same* hook is called from more
than one surface. `ctx->purpose` (a `VividPreviewPurpose`) says which, so you know what you may assume.
Most ops draw the same thing everywhere and can ignore it. Branch on it **only** when the semantic
contract differs — it is **not** a layout hint (aspect and size are already in `surface_width`/
`surface_height`, so read those for layout) and **not** the operator's role (that's `VividOperatorRole`).

| `ctx->purpose` | live instance? | `param_values` | `time` | draw for… |
|---|---|---|---|---|
| `VIVID_PREVIEW_SESSION_CELL` | yes | node params | transport beats | musical material in the session grid |
| `VIVID_PREVIEW_AUDIO_NODE` | yes | node params | clock beats | a device/node card in the audio graph |
| `VIVID_PREVIEW_DEFAULT` | maybe | may be null | may be 0 | unspecified — assume the least |
| `VIVID_PREVIEW_CATALOG` *(reserved)* | **no** | defaults/null | 0 | a picker/reference preview — render from defaults, assume no sample/voice |
| `VIVID_PREVIEW_VISUAL_NODE` *(reserved)* | yes | node params | frame time | a visual node that draws instead of blitting a texture |

`DEFAULT` is `0`, so an older op that never reads `purpose` is unaffected, and a host that hasn't set it
yet gets `DEFAULT`. The two reserved purposes have no call site today — don't wait on them. The export is
still `vivid_draw_thumbnail`; only the vocabulary ("preview") changed.

### Audio nodes have their own preview mechanisms

Beyond Path A, the audio node-graph card gives most audio ops a meaningful preview **without**
`draw_thumbnail`, so "no `draw_thumbnail`" does **not** mean "name-only":

- **Modulators** (LFO, ADSR — kind 5): a **compound-widget** shape preview (the LFO waveform / the ADSR
  envelope) drawn from the params when the node is selected (`ui/compound_widget.h`,
  `AudioNodeGraph::compound_previews`), **plus** a live **output scope** in the node body (the actual
  control signal — flat only while the modulator is unwired/idle).
- **Instruments / effects**: a live **output scope** of the node's audio.
- **Note generators** (kind 8): Path-A `draw_thumbnail` (above).
- **Note effects** (Arp) / MidiIn / Selector: minimal — no audio and no clip to preview.
- **Pure DSP effects** (Bitcrush, Filter, the glitch pack): a name-only cell is acceptable — nothing
  static to draw.

So the ADR-0042 "definition of done" thumbnail bar for an audio op is met by *any* of these — a scope, a
compound preview, or a Path-A drawing. The audit's per-op `list_operators` view can't see the scope /
compound-widget paths, so it lists audio ops as `expected-manual` / `exempt-effect` for a human to eyeball.

Run `tools/operator_audit/audit.py` (ADR-0042) to check a visual operator's thumbnail + render + params.

## Not yet: custom inspectors & editors

Operators can't yet ship a **custom inspector** (a compound-widget param panel) or a **custom editor**
(a full canvas — a drum grid, an envelope). The ABI structs exist in `operator_api/types.h`, but the
loader + UI don't wire them for packages yet — that's the editors/inspectors ABI (tracked as UI-4).
Until then, author params with display hints/semantic metadata and let the host render them.
