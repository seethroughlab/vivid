# LLM-over-MCP evals (P4.6)

Does an agent driving Vivid through the MCP tools actually accomplish tasks? These evals
run cases (discover operators, build a 2-node scene, set a param, state the cross-cadence
architecture fact) and grade the result.

## Layout

- `harness.py` — transports (`RealTransport` = HTTP to the live control server;
  `MockTransport` = an offline simulation of the graph slice) + the `run_loop` driver.
- `providers.py` — `FakeProvider` (scripted, deterministic) and `AnthropicProvider` /
  `OpenAIProvider` (real agents; lazy-imported, need an API key).
- `graders.py` — `PatternGrader` (final-answer facts + tool-call shape), `SceneGrader`
  (the end-state graph via `get_session`), `AllOf`.
- `cases.py` — the cases; each carries a `fake_script` the fake provider replays.
- `llm_mcp_eval.py` — the runner.

## Run

```sh
# Offline, no app, no key — what CI runs (verifies the whole pipeline):
uv run mcp/evals/llm_mcp_eval.py --selftest

# Against a LIVE app with a real model (start the app first):
uv run --with anthropic mcp/evals/llm_mcp_eval.py \
    --provider anthropic --model claude-opus-4-8 --case all \
    --base-url http://127.0.0.1:9876
```

The `fake` path is **verified**; real-provider runs need a running app + an API key and are
**not** exercised in this environment (see `.github/workflows/llm-mcp-evals.yml`,
manual-dispatch).
