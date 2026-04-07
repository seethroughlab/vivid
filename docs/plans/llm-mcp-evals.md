# Real-LLM MCP Eval Plan

## Summary

Add manual, CI-runnable evals that ask actual OpenAI and Anthropic models to use Vivid's MCP servers as their only codebase context. The goal is to validate whether the MCP surfaces expose enough discoverable, useful information for an LLM to answer tricky Vivid architecture questions and author a predictable simple operator.

This should be an eval harness, not a replacement for direct MCP unit tests. Existing Python and C++ coverage can prove that individual tools return expected data; these evals should prove that a real model can discover and apply that data through the tool interface.

The first version is manual-only in GitHub Actions to avoid surprise API spend and to keep occasional model/tool-call flakiness out of the normal PR path.

## Key Changes

- Add a Python eval harness, likely under `scripts/llm_mcp_eval.py` plus small helper modules, that starts stdio sessions for `mcp/vivid_mcp.py` and `mcp/vivid_opdev_mcp.py`.
- Convert selected MCP tools into OpenAI and Anthropic tool-call schemas, and expose only those tools to the model. Do not give the model direct filesystem, shell, web, or raw repo access.
- Save transcripts, tool-call traces, grader output, and generated operator artifacts under `build/llm_mcp_evals/`.
- Add provider adapters for OpenAI and Anthropic. CI should read `OPENAI_API_KEY` and `ANTHROPIC_API_KEY` from GitHub secrets, with model names supplied through workflow inputs or repo variables rather than hard-coded source constants.
- Add an allowlisted eval tool surface:
  - Opdev read-only tools for source search/read, source spans, API docs, example operators, capability guidance, and starting-point recommendations.
  - Main MCP read-only/static tools where useful, such as `list_types`, `operator_docs`, and read-only inspection/introspection tools.
  - No mutating graph or package tools for architecture and question-answer cases.
- Add a fake-provider mode so the harness, tool loop, parsers, and deterministic graders can run locally and in normal CI without API keys.

## Eval Cases

- **MCP split / ownership:** Ask which server owns live runtime graph control versus operator-authoring source/docs. Require evidence from MCP-accessible docs or source, and reject answers that collapse the two servers into one role.
- **Architecture trick question:** Ask whether audio and GPU communicate directly, how cross-cadence data flows, and what the Control/frame-side bridge role is. Require MCP-backed evidence and reject direct Audio-to-GPU routing claims.
- **Operator API lookup:** Ask how to write a minimal control operator with params and scalar ports. Require the model to discover and cite opdev docs or examples rather than answering from memory alone.
- **Simple operator generation:** Ask the model to produce a predictable control operator named `ThresholdGate`:
  - One scalar input: `in`
  - One scalar output: `gate`
  - One `Param<float>` named `threshold`, default `0.5`, range `0.0..1.0`
  - Output `1.0` when `in >= threshold`, otherwise `0.0`
  - Register with `VIVID_REGISTER(ThresholdGate)`

For the operator case, the harness should extract the final code, write it into a temporary package under the eval artifacts directory, generate a package manifest and a small C++ behavior test, then run the existing package path through the built `vivid` CLI with an isolated `HOME`. The behavior test should check inputs below, equal to, and above the threshold so grading is exact instead of aesthetic.

## Grading And CI

- Use deterministic graders first, not a second LLM.
- Require each real-model case to make at least one relevant MCP tool call before finalizing.
- Require final answers to include structured evidence from MCP tool results, such as cited MCP-returned paths, topics, tool names, or snippets.
- Match required facts for question-answer evals and reject common wrong claims.
- For the operator case, require compile success and exact behavior from the generated C++ test.
- Add `.github/workflows/llm-mcp-evals.yml`:
  - Trigger: `workflow_dispatch` only.
  - Runner: self-hosted macOS, matching the existing Vivid CI shape.
  - Steps: checkout with submodules, configure/build `vivid`, install MCP and provider client Python deps, run fake-provider smoke, then run OpenAI and Anthropic evals when secrets are present.
  - Upload `build/llm_mcp_evals/` artifacts on both pass and fail.
- The workflow should fail if a requested provider is missing its secret, but the fake-provider smoke should remain runnable without secrets for local development.

## Assumptions

- Initial provider coverage is OpenAI plus Anthropic.
- Initial CI cadence is manual-only.
- The first operator-authoring challenge should stay simple and behaviorally exact. A more complex creative operator eval can be added later once the harness proves reliable.
- The eval validates MCP information sufficiency by restricting the model's tool access and requiring MCP-sourced evidence. It cannot fully erase model pretraining, so the graders should focus on demonstrated tool-backed discovery.
- No runtime behavior changes are required for this plan, so no `docs/runtime/*.md` update is needed.
