# Vivid — MCP bridge

A [FastMCP](https://github.com/jlowin/fastmcp) stdio server that proxies tool calls to the running
app's loopback control server (`control_server.cpp`, default `http://127.0.0.1:9876`).

```
Claude / Claude Code ──stdio MCP──▶ vivid_mcp.py ──HTTP POST /<method>──▶ app control server
```

## Release status (UX Ph5 F1)

The **first-release promise** is the MCP-native creative loop (ADR-0040): **inspect / explain / edit
/ analyze / transport / mapping / project** — inspect the session, explain what drives what, author
graph + project-local code, connect mappings, capture + verify, save, and reload. These tools use
product vocabulary and their edits enter the same undo history as UI edits.

**Experimental:** the **music-eval** tools (`configure_music_eval_backend`,
`evaluate_audio_musically`, `music_eval_status`, `music_eval_result`) require a Google **Gemini** API
key and an external backend, and **fail closed** (no verdict without a key). Treat them as
experimental, not part of the day-one promise. Video export is release but secondary.

## Run

1. Launch the app (`app/build/vivid.app/Contents/MacOS/vivid`) — it logs
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

## Connecting a client

**If you installed Vivid from a release**, the bridge ships inside the app — you do not need this
repo. Launch Vivid and use **Help ▸ Connect Claude…**, which shows the exact command with the right
path already filled in and a Copy button. It looks like:

```sh
claude mcp add vivid -- uv run --script "/Applications/Vivid.app/Contents/Resources/mcp/vivid_mcp.py"
```

(`--script`, not `--directory`: script mode resolves deps from the PEP-723 header at the top of
`vivid_mcp.py` into uv's cache. `--directory` would try to create a `.venv` *inside* the app bundle,
which is unwritable under `/Applications` and would break the code signature.)

An agent that is already connected can read the same string back with `get_mcp_setup()`.

**From a repo checkout**, point at this directory instead (run from the repo root):

```sh
claude mcp add vivid -- uv run --directory "$PWD/mcp" vivid_mcp.py
```

Or, for a client that takes a JSON config (`.mcp.json`) — replace `<path-to>` with either the
bundled `Vivid.app/Contents/Resources/mcp` or your checkout's `mcp/`:

```json
{
  "mcpServers": {
    "vivid": {
      "command": "uv",
      "args": ["run", "--directory", "<path-to>/mcp", "vivid_mcp.py"],
      "env": { "VIVID_URL": "http://127.0.0.1:9876" }
    }
  }
}
```

Set `VIVID_URL` only if you changed the app's port with `VIVID_PORT`.

Then ask the agent to `get_authoring_guide()` and build a reactive scene.

## Editing & content tools

Beyond the graph/session/mapping methods, the bridge exposes **`undo`** / **`redo`** (whole-document,
ADR-0017 — a single reversible history across visual graph, mappings, and audio) and the node-preset
tools (`save_node_preset` / `list_node_presets` / `load_node_preset`, ADR-0021). This README is not an
exhaustive tool inventory — `get_authoring_guide()` and the tool list in `vivid_mcp.py` are current.

## Music-theory tools

`theory.py` (zero-dependency, pure) powers chord/scale/rhythm/analysis tools —
`set_progression`, `add_chord`, `set_drum_pattern`, `arpeggiate`, `quantize_to_scale`,
`analyze_clip`, … — so the agent composes by musical intent, not raw MIDI. Full vocabulary
(chord symbols, scale + drum names, step-strings, roman numerals) in
[`docs/music-theory-tools.md`](../docs/music-theory-tools.md). Correctness suite:

```sh
uv run --directory mcp test_theory.py
```
