# Vivid PoC — MCP bridge

A [FastMCP](https://github.com/jlowin/fastmcp) stdio server that proxies tool calls to the running
app's loopback control server (`control_server.cpp`, default `http://127.0.0.1:9876`).

```
Claude / Claude Code ──stdio MCP──▶ vivid_mcp.py ──HTTP POST /<method>──▶ app control server
```

## Run

1. Launch the app (`app/build/vivid_poc.app/Contents/MacOS/vivid_poc`) — it logs
   `control server listening on 127.0.0.1:9876`. Override the port with `VIVID_PORT`.
2. Run the bridge (Python deps via [uv](https://docs.astral.sh/uv/)):
   ```sh
   uv run --directory mcp vivid_mcp.py
   ```

Quick test without an MCP client — the control server speaks plain JSON over HTTP:
```sh
curl -s -XPOST localhost:9876/status
curl -s -XPOST localhost:9876/add_node -d '{"op":"Plasma"}'
curl -s -XPOST localhost:9876/connect_mapping -d '{"src":"master.transient","dst":"node:4.warp"}'
```

## Claude Code / Desktop config (`.mcp.json`)

```json
{
  "mcpServers": {
    "vivid": {
      "command": "uv",
      "args": ["run", "--directory", "/Users/jeff/Developer/vivid/mcp", "vivid_mcp.py"],
      "env": { "VIVID_URL": "http://127.0.0.1:9876" }
    }
  }
}
```

Then ask the agent to `get_authoring_guide()` and build a reactive scene.

## Music-theory tools

`theory.py` (zero-dependency, pure) powers chord/scale/rhythm/analysis tools —
`set_progression`, `add_chord`, `set_drum_pattern`, `arpeggiate`, `quantize_to_scale`,
`analyze_clip`, … — so the agent composes by musical intent, not raw MIDI. Full vocabulary
(chord symbols, scale + drum names, step-strings, roman numerals) in
[`docs/music-theory-tools.md`](../docs/music-theory-tools.md). Correctness suite:

```sh
uv run --directory mcp test_theory.py
```
