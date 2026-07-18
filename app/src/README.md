# Newcomer Map for `app/src/`

This map is the fastest way to find the right first file. Read [`../ARCHITECTURE.md`](../ARCHITECTURE.md)
first for the App/Window split, thread model, and MCP control flow.

## First Landmarks

- `main.cpp` wires startup and teardown. It should stay boring.
- `app/app.h` owns shared engine/document state: session, transport, graph, control server, GPU, undo, and audio runtime state.
- `app/window.h` owns one view of that state: layout, selection, drag/menu state, renderer, and clip editor.
- `app/frame.cpp` is the main-thread tick: drain MCP, apply mappings, draw, and commit undo snapshots.
- `app/input.cpp` is GLFW input dispatch into the current `Window`.

## Change By Task

- Audio engine or real-time behavior: start with `audio/vst3_host.h`, then read `audio/audio_callback.cpp` and [`../docs/thread-safety.md`](../docs/thread-safety.md). Keep audio-thread work allocation-free and non-blocking.
- MCP/control methods: start with `cli/control_server.cpp`, then the narrow `cli/control_handlers_*.cpp` domain file. Stable errors live in `cli/control_errors.h`; parse/index helpers live in `cli/control_parse.h`.
- Audio MCP methods: `cli/control_handlers_audio.cpp` is the coordinator. Domain registrations live beside it: `_analysis`, `_clip_pool`, `_devices`, `_graph`, and `_catalog`.
- Visual graph behavior: start with `gpu/visual_graph.*`, then `gpu/op_registry.*` and `operator_api/` for operator contracts.
- UI drawing or hit testing: start with `ui/session_view.*`, `ui/node_graph.*`, and `ui/layout.h`. Keep draw and input geometry shared through `ui/layout.h`.
- Undo/redo: start with `app/edit_gateway.*`, then `app/undo_manager.*`, `app/persist_undo.*`, and `cli/edit_methods.*` for MCP edit capture.
- Content, packages, or hot reload: start with `packages/package_manager.*`, `packages/package_compiler.*`, and `gpu/operator_loader.*`.

## Local Guides

Each major directory has a `CLAUDE.md` with invariants for that area. When changing code in a domain, read that directory guide before editing.
