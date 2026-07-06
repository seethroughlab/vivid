"""Providers (P4.6): decide the agent's next action given the goal + transcript so far.

- FakeProvider: replays a scripted list of actions. Deterministic, no network — this is
  what the --selftest / CI smoke uses to exercise the whole loop+grader pipeline offline.
- AnthropicProvider / OpenAIProvider: real agents (lazy-imported; need an API key + a live
  app). They're given the tool catalog and a system prompt and translate model tool-calls
  into the harness's {tool,args} / {answer} actions.
"""
from __future__ import annotations

import json
import os

from harness import Transcript

# The slice of the control surface the cases exercise (name -> one-line description). Real
# providers turn these into tool schemas; the fake provider ignores them.
TOOLS: dict[str, str] = {
    "status": "Liveness + counts.",
    "get_version": "App/operator-abi/session-schema versions.",
    "get_health": "Rolled-up engine health (severity ok|warning|error).",
    "list_operators": "Catalog of spawnable visual operators (params carry semantic hints).",
    "add_node": "Spawn an op node: {op}. Returns {id}.",
    "connect_nodes": "Wire {input_id} -> {node_id}.",
    "set_node_param": "Set a node base param by name: {node_id, name, value}.",
    "get_session": "Full session snapshot (session.graph.chain = the op nodes).",
    # Native audio operators (the audio peer of list_operators).
    "list_audio_operators": "Catalog of native audio operators {instruments, effects} with param schema.",
    "set_track_audio_instrument": "Set a track's native instrument: {track, op}.",
    "add_audio_effect": "Append a native audio effect to a track: {track, op}. Returns {index}.",
    "set_audio_op_param": "Set a native audio op param by index: {track, index, param, value}.",
    "list_audio_ops": "The native instrument + effect chain on a track, with params.",
    "install_operator_package": "Compile + register an operator package from a dir: {path}.",
    "list_mapping_sources": "Valid bridge sources (master/track audio characteristics) to wire.",
    "connect_mapping": "Wire a source to a dest: {src, dst, amount}.",
}

SYSTEM_PROMPT = (
    "You are driving Vivid, an audio-visual environment, over an MCP tool API. Use the "
    "tools to accomplish the user's goal, then give a short final answer describing what "
    "you did. Prefer discovering operators with list_operators before add_node."
)


class FakeProvider:
    """Replays `script`: a list of {tool,args} actions, ending with {answer}."""
    def __init__(self, script: list[dict]):
        self.script = script

    def next_action(self, goal: str, t: Transcript) -> dict:
        i = len(t.steps)
        if i < len(self.script):
            return self.script[i]
        return {"answer": self.script[-1].get("answer", "done")} if self.script and "answer" in self.script[-1] \
            else {"answer": "done"}


class AnthropicProvider:
    def __init__(self, model: str):
        import anthropic  # lazy: only needed for real runs
        self.model = model
        self.client = anthropic.Anthropic(api_key=os.environ["ANTHROPIC_API_KEY"])
        self._tools = [{"name": n, "description": d,
                        "input_schema": {"type": "object", "additionalProperties": True}}
                       for n, d in TOOLS.items()]

    def next_action(self, goal: str, t: Transcript) -> dict:
        messages = [{"role": "user", "content": goal}]
        for s in t.steps:
            messages.append({"role": "assistant", "content": [
                {"type": "tool_use", "id": f"c{len(messages)}", "name": s.tool, "input": s.args}]})
            messages.append({"role": "user", "content": [
                {"type": "tool_result", "tool_use_id": f"c{len(messages) - 1}",
                 "content": json.dumps(s.result)}]})
        resp = self.client.messages.create(model=self.model, max_tokens=1024,
                                           system=SYSTEM_PROMPT, tools=self._tools,
                                           messages=messages)
        for block in resp.content:
            if block.type == "tool_use":
                return {"tool": block.name, "args": dict(block.input)}
        text = "".join(b.text for b in resp.content if b.type == "text")
        return {"answer": text}


class OpenAIProvider:
    def __init__(self, model: str):
        import openai  # lazy
        self.model = model
        self.client = openai.OpenAI(api_key=os.environ["OPENAI_API_KEY"])
        self._tools = [{"type": "function",
                        "function": {"name": n, "description": d,
                                     "parameters": {"type": "object", "additionalProperties": True}}}
                       for n, d in TOOLS.items()]

    def next_action(self, goal: str, t: Transcript) -> dict:
        messages = [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": goal}]
        for s in t.steps:
            cid = f"c{len(messages)}"
            messages.append({"role": "assistant", "tool_calls": [
                {"id": cid, "type": "function",
                 "function": {"name": s.tool, "arguments": json.dumps(s.args)}}]})
            messages.append({"role": "tool", "tool_call_id": cid, "content": json.dumps(s.result)})
        resp = self.client.chat.completions.create(model=self.model, messages=messages,
                                                    tools=self._tools)
        msg = resp.choices[0].message
        if msg.tool_calls:
            tc = msg.tool_calls[0]
            return {"tool": tc.function.name, "args": json.loads(tc.function.arguments or "{}")}
        return {"answer": msg.content or ""}


def make_provider(name: str, model: str, case=None):
    if name == "fake":
        return FakeProvider(case.fake_script if case else [{"answer": "done"}])
    if name == "anthropic":
        return AnthropicProvider(model)
    if name == "openai":
        return OpenAIProvider(model)
    raise ValueError(f"unknown provider: {name}")
