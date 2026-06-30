"""Eval cases (P4.6): goal + grader + a deterministic fake_script.

Each case is one task an agent should accomplish over the MCP tools. fake_script is the
scripted action list the `fake` provider replays — it must satisfy the grader, so the
offline smoke proves the loop + grader + (mock) transport all agree. A real provider
(anthropic/openai) ignores fake_script and is graded on what it actually does."""
from __future__ import annotations

from dataclasses import dataclass, field

from graders import AllOf, PatternGrader, SceneGrader


@dataclass
class Case:
    name: str
    goal: str
    grader: object
    fake_script: list[dict] = field(default_factory=list)


CASES: list[Case] = [
    Case(
        name="discover_operators",
        goal="What visual operators can I spawn? List them.",
        grader=PatternGrader(required_facts=["plasma"], required_tool_prefixes=["list_operators"],
                             min_tool_calls=1),
        fake_script=[
            {"tool": "list_operators", "args": {}},
            {"answer": "Available operators include Plasma, Tint and Output."},
        ],
    ),
    Case(
        name="build_scene",
        goal="Build a scene: a Plasma generator feeding an Output node, wired together.",
        grader=AllOf([
            SceneGrader(min_nodes=2, required_ops=["Plasma", "Output"]),
            PatternGrader(required_tool_prefixes=["add_node", "connect_nodes"]),
        ]),
        fake_script=[
            {"tool": "add_node", "args": {"op_type": "Plasma"}},
            {"tool": "add_node", "args": {"op_type": "Output"}},
            {"tool": "connect_nodes", "args": {"node_id": 2, "input_id": 1}},
            {"answer": "Created Plasma (1) -> Output (2) and wired them."},
        ],
    ),
    Case(
        name="set_param",
        goal="Add a Tint node and set its first base parameter to 0.5.",
        grader=PatternGrader(required_tool_prefixes=["add_node", "set_node_param"], min_tool_calls=2),
        fake_script=[
            {"tool": "add_node", "args": {"op_type": "Tint"}},
            {"tool": "set_node_param", "args": {"node_id": 1, "index": 0, "value": 0.5}},
            {"answer": "Added Tint (1) and set param 0 to 0.5."},
        ],
    ),
    Case(
        name="architecture_fact",
        goal=("In Vivid, do the audio engine and the GPU visuals communicate directly? "
              "Explain how data crosses between them."),
        grader=PatternGrader(required_facts=["mapping", "audio"],
                             rejected_claims=["share the same memory"]),
        fake_script=[
            {"answer": ("No — the audio engine and GPU visuals run at different cadences and "
                        "do not communicate directly. Data crosses through the mapping bridge, "
                        "which routes audio analysis to visual params (and back).")},
        ],
    ),
]


def by_name(name: str) -> Case | None:
    return next((c for c in CASES if c.name == name), None)
