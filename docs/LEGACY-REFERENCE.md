# Legacy Reference

The `legacy` branch (777 commits) is a mature, monolithic C++ implementation that covers nearly every feature on the roadmap. The current master is a clean rewrite with a different architecture (C ABI + dlopen operators, JSON graph, Dawn/WebGPU), so legacy code should never be copied verbatim. Instead, read it for **patterns and design decisions** — bind group caching, RGBA16Float intermediates, generation-based cooking, RAII handles — that would otherwise be rediscovered through debugging.

## Reading legacy without switching branches

```bash
git show legacy:<path>                    # View a single file
git ls-tree --name-only legacy <dir>/     # List a directory
git grep <pattern> legacy -- '<glob>'     # Search across files
```

## Phase → Legacy File Mapping

All paths are relative to the legacy branch root. Core engine files live under `modules/vivid-core/` unless a full module path is shown.

| Phase | Legacy files to consult |
|-------|------------------------|
| 5: Control→GPU | `include/vivid/param.h`, `include/vivid/param_registry.h`, `src/context.cpp` |
| 6: Audio Output | `src/audio_output.cpp`, `src/audio_graph.cpp`, `include/vivid/audio_output.h`, `include/vivid/audio_graph.h` |
| 7: Audio→Control | `src/audio_analysis.cpp`, `src/av_analysis.cpp`, `include/vivid/audio_analysis.h` |
| 8: Hot-Reload | `src/hot_reload.cpp`, `include/vivid/hot_reload.h`, `src/shader_preprocessor.cpp` |
| 9: REPL | `src/cli/runtime_api.cpp`, `src/cli/cli.cpp` |
| 10: MIDI | `modules/vivid-midi/src/midi_in.cpp`, `modules/vivid-midi/src/midi_out.cpp` |
| 11: UI Node Graph | `src/gui/node_graph.cpp`, `src/gui/gui.cpp`, `src/gui/panel_manager.cpp` |
| 12: Thumbnails | `src/gui/scratch_texture.cpp`, `include/vivid/operator_viz.h` |
| 13: Spreads | `include/vivid/dsp_utils.h`, `src/effects/gpu_particles.cpp` |
| 14: Polyphonic Audio | `modules/vivid-audio/src/poly_synth.cpp`, `modules/vivid-audio/src/envelope.cpp`, `modules/vivid-audio/src/sequencer.cpp` |
| 15: Instance Operator | `src/effects/gpu_particles.cpp` (instanced rendering pattern) |
| 16: MCP Server | `src/cli/mcp_server.cpp`, `docs/MCP-TOOLS.md` |
| 17: Chat Panel | `modules/vivid-imgui/`, `src/cli/runtime_api.cpp` |
| 20: Patterns | `modules/vivid-audio/src/sequencer.cpp`, `modules/vivid-audio/src/euclidean.cpp`, `modules/vivid-audio/src/arpeggiator.cpp` |
| 22: Export | `src/cli/main_production.cpp`, `include/vivid/video_exporter.h`, `include/vivid/snapshot.h` |
| 23: Operator Library | `include/vivid/module_manager.h`, `include/vivid/module_registry.h`, `docs/MODULES.md` |
| 24: LLM Perception | `src/cli/analysis_hints.cpp`, `src/cli/assertion.cpp`, `docs/ANALYSIS-TOOLS.md` |
| 25: WebSocket API | `src/cli/runtime_api.cpp`, `docs/WEBSOCKET_API.md` |

## Legacy docs worth reading

- `docs/CREATING-OPERATORS.md` — operator lifecycle, param registration, GPU resource patterns
- `docs/RECIPES.md` — complete audio-visual chain examples
- `docs/MCP-TOOLS.md` — MCP tool catalog (Phase 16 target)
- `docs/MODULES.md` — module system design (Phase 23 target)
- `docs/ANALYSIS-TOOLS.md` — perception/introspection (Phase 24 target)
