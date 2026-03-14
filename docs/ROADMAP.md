# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation

Project-local operator ownership (clone/scaffold destination policy, package CMake patching, team workflow regression tests) shipped as part of launch prep.

---

## Deferred Past 1.0

- Subpatches
- Simulation zones (frame-to-frame feedback)
- Multi-window / multi-monitor
- Windows / Linux
- WebSocket API — external process integration over WebSocket; enables non-MCP clients to control Vivid programmatically
- Project file format (single JSON vs. directory with assets)
- Library version pinning
- Accessibility
- MIDI input tests (virtual MIDI loopback integration suite)

---

## Extra Findings

- [ ] Check if wgpu-native PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
