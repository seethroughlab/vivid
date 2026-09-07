# Vivid — repo navigation

Vivid is an agent-first audio-visual environment. The trunk (`vivid-4`) holds a real macOS C++
application under **`app/`**: two best-in-class surfaces — a DAW (tracks × scenes of clips, each
track an instrument + FX chain) and a rewireable visuals node-graph — joined by a bidirectional
**mapping bridge** and driven MCP-natively.

## Where things are
- **`app/`** — the C++ app (GLFW + wgpu-native + miniaudio + VST3). Entry: `app/src/main.cpp`.
  - `app/src/app/` — `App`/`Window`/`input`/`frame`, plus the **`edit_gateway.*` + `undo_manager.*`**
    command sink (ADR-0017 undo/redo — every UI + MCP edit routes through it) and `examples.*` /
    `node_presets*.cpp` (ADR-0021 content).
  - `app/src/audio/` — VST3 host + multi-track session (`vst3_host.h` is the session C API).
  - `app/src/gpu/` — wgpu context, the visuals `VisualGraph`, shader/effect ops, video, `file_drop_registry.*`.
  - `app/src/ui/` — `Renderer2D`, the node-graph editor, clip editor, `ui_style.h` (the palette/widgets),
    `shader_library_view.*` (the ADR-0021 content browser).
  - `app/src/cli/control_server.{h,cpp}` — loopback HTTP control server (the MCP backend);
    `control_handlers_edit.cpp` registers `undo`/`redo`.
  - `app/src/mapping.h` — the `MappingRegistry` (the bridge); `app/src/persist.*` — session JSON;
    `app/src/persist_undo.*` — the canonical-document projection + tiered restore for undo.
- **`mcp/`** — `vivid_mcp.py`, a FastMCP (stdio) bridge proxying tools to the control server. See `mcp/README.md`.
- **`docs/decisions/`** — ADRs. Strategy: **ADR-0010** (native reboot promoted to product trunk) →
  **ADR-0011** (product architecture + the trunk decision). Product surfaces: **ADR-0014** (the
  visual graph is home), **ADR-0016** ✅ (a shader file is an operator). The **platform-gap set**
  (from 2026-07-14) is now **fully shipped**: **ADR-0017** ✅ every edit is reversible · **ADR-0021** ✅
  content is browsable · **ADR-0019** ✅ nothing fails silently · **ADR-0020** ✅ the inner loop is
  visible · **ADR-0018** ✅ a bad operator must not cost you your work (undo, error surfaces, authoring
  loop, resilience — dirty/autosave/recovery + crash attribution + safe-mode/quarantine). **ADR-0022**
  (session audio graph) is proposed; its undo dependency is now cleared.
- **`docs/roadmap/reboot-readiness-roadmap.md`** — the reboot-readiness assessment + phased roadmap (P0–P4, largely
  landed).
- **`docs/roadmap/classic-platform-gap.md`** — the classic→trunk **product** lift. Its finding —
  *the trunk has excellent plumbing and almost no presentation* — drove the platform-gap set, which
  has now **all shipped** (ADR-0017 undo, ADR-0019 error surfaces, ADR-0020 authoring loop, ADR-0018
  resilience). Read for the historical framing.
- **`vivid-classic`** (git branch) — the mature predecessor; the engineering benchmark (operator/ABI
  model, codegen, packages, tests/CI, production gate, docs culture). Read via `git show vivid-classic:<path>`.

## Build & run (macOS)
```sh
cmake -S app -B app/build && cmake --build app/build -j
app/build/Vivid.app/Contents/MacOS/Vivid        # logs: control server on 127.0.0.1:9876
uv run --directory mcp vivid_mcp.py                      # the MCP bridge (app must be running)
```

## Status (2026-07-16)
`app/` is the **product trunk** for the `vivid-4` reboot. Target is an extensible,
cross-platform-capable platform, using `vivid-classic` as the mature predecessor and benchmark where
it helps. Strategy (ADR-0011, **accepted**): **keep `app/` as the trunk and adopt classic's platform
by selective lift**. Build identity is now `vivid` / `com.vivid.app` / "Vivid"; the audio-session C
API lives in `namespace vivid::session`.

Current trunk has P0-P4 style productization work in place: App/Window decomposition, headless tests,
CI/gate scaffolding, named control-server errors, runtime health/version surfaces, operator ABI +
loader/package/hot-reload pieces, semantic metadata, MCP eval harness, and release scaffolding. The
platform-gap lift has **fully landed**: **whole-document undo/redo** (ADR-0017, EditGateway command
sink), **content browsing** (ADR-0021), a shader file as a first-class operator (ADR-0016), the
curated VST3/CLAP param inspector, **error surfaces** (ADR-0019 — node badges, health dot, diagnostics
panel, leveled logger/toasts), the **visible authoring loop** (ADR-0020 — always-on hot-reload,
rollback-first, shippable toolchain, fork-to-edit), and **resilience** (ADR-0018 — dirty/autosave/
recovery, crash attribution + history, safe-mode/quarantine).
Before choosing next work, read [ADR-0009](docs/decisions/ADR-0009-two-surface-bridge-and-native-reboot.md),
[ADR-0010](docs/decisions/ADR-0010-native-reboot-seed.md),
[ADR-0011](docs/decisions/ADR-0011-reboot-product-architecture.md), and
[the roadmap](docs/roadmap/reboot-readiness-roadmap.md).
