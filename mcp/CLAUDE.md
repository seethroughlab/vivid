# MCP Servers

## Purpose

This directory contains the Python MCP bridge servers that expose Vivid's runtime to LLM tool calls. They translate MCP stdio protocol into HTTP requests against the running Vivid instance's control server.

## Key Files

| File | Role |
|------|------|
| `vivid_mcp.py` | Main MCP server: graph mutation, parameter control, inspection, capture, package management |
| `vivid_opdev_mcp.py` | Operator development MCP server: source search, API docs, example operators, scaffolding, build/test |
| `vivid_analysis_mcp.py` | Analysis MCP server: visual and audio perception tools |
| `opdev_docs/` | Markdown API reference docs served by the opdev MCP tools (`get_operator_api_docs`) |
| `visual/` | Python visual analysis library (color, luminance, composition, style classification) |
| `analysis/` | Python audio analysis library (spectral, perception models, mood classification) |
| `requirements.txt` | Python dependencies for all three servers |

## How It's Organized

### Bridge Architecture

All three MCP servers bridge to the same HTTP control server (port 9876) running inside the Vivid app process. The flow is:

```
LLM tool call → MCP server (Python, stdio) → HTTP POST → ControlServer (C++, main thread) → RuntimeAPI
```

For live-graph operations (add_node, set_param, capture, etc.), the MCP servers must connect to a running Vivid instance. The `ensure_runtime` tool handles this — it launches Vivid if needed and waits for the control server to become available.

For static lookups (operator catalog, docs, package metadata), the MCP servers can bypass the control server and invoke the `vivid` CLI directly for one-shot JSON queries.

### Server Responsibilities

**vivid_mcp.py** — the primary server for graph authoring:
- Graph topology: add_node, connect, disconnect, remove_node
- Parameters: set_param, get_param, set_string_param
- Inspection: inspect_graph, list_nodes, list_types, operator_docs
- Capture: capture_image, analyze_output, compare_outputs
- Persistence: save_graph, load_graph, new_graph
- Packages: install_package, list_packages, rebuild_package
- Variations, presets, MIDI mappings, modulation assignments

**vivid_opdev_mcp.py** — for building custom operators:
- Source browsing: search_source, read_source_file, find_symbol, find_references
- API reference: get_operator_api_docs (serves from `opdev_docs/`)
- Examples: list_example_operators, get_example_operator, search_example_operators
- Scaffolding: scaffold_operator, recommend_starting_point
- Build/test: rebuild_package, test_package, get_build_activity, explain_build_failure

**vivid_analysis_mcp.py** — for perception and analysis:
- Frame analysis: capture + visual feature extraction (brightness, contrast, hue, composition)
- Audio analysis: capture + spectral analysis, mood classification

## Relationships

- **Upstream:** LLM clients (Claude Code, IDE extensions) connect via MCP stdio protocol
- **Downstream:** HTTP requests to `ControlServer` (port 9876) → `RuntimeAPI`
- **Peer:** The control server is embedded in the Vivid app process; MCP servers are separate Python processes

## See Also

- `docs/LLM-INTEGRATION.md` — MCP server design, four LLM roles, tool organization
- `docs/runtime/control_server.md` — HTTP endpoint catalog
