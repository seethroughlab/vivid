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
            {"tool": "list_operators", "args": {}},
            {"tool": "add_node", "args": {"op": "Plasma"}},
            {"tool": "add_node", "args": {"op": "Output"}},
            {"tool": "connect_nodes", "args": {"node_id": 2, "input_id": 1}},
            {"answer": "Created Plasma (1) -> Output (2) and wired them."},
        ],
    ),
    Case(
        name="set_param",
        goal="Add a Tint node and set its amount parameter to 0.5.",
        grader=PatternGrader(required_tool_prefixes=["list_operators", "add_node", "set_node_param"],
                             min_tool_calls=3),
        fake_script=[
            {"tool": "list_operators", "args": {}},
            {"tool": "add_node", "args": {"op": "Tint"}},
            {"tool": "set_node_param", "args": {"node_id": 1, "name": "amount", "value": 0.5}},
            {"answer": "Added Tint (1) and set amount to 0.5."},
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
    # --- native audio operators (the audio authoring surface) ---
    Case(
        name="discover_audio_ops",
        goal="What native audio instruments and effects can I use to build a track's sound?",
        grader=PatternGrader(required_facts=["drive"], required_tool_prefixes=["list_audio_operators"],
                             min_tool_calls=1),
        fake_script=[
            {"tool": "list_audio_operators", "args": {}},
            {"answer": "Instruments: SineSynth, Sampler. Effects: Drive, Bitcrush."},
        ],
    ),
    Case(
        name="build_audio_chain",
        goal="On track 0, set the instrument to SineSynth and add a Drive effect after it.",
        grader=PatternGrader(
            required_tool_prefixes=["list_audio_operators", "set_track_audio_instrument", "add_audio_effect"],
            min_tool_calls=3),
        fake_script=[
            {"tool": "list_audio_operators", "args": {}},
            {"tool": "set_track_audio_instrument", "args": {"track": 0, "op": "SineSynth"}},
            {"tool": "add_audio_effect", "args": {"track": 0, "op": "Drive"}},
            {"answer": "Track 0 is now SineSynth -> Drive."},
        ],
    ),
    Case(
        name="set_named_audio_param",
        goal="On track 0's Drive effect (index 0), set its drive parameter (param 0) to 0.8.",
        grader=PatternGrader(required_tool_prefixes=["list_audio_ops", "set_audio_op_param"],
                             min_tool_calls=2),
        fake_script=[
            {"tool": "list_audio_ops", "args": {"track": 0}},
            {"tool": "set_audio_op_param", "args": {"track": 0, "index": 0, "param": 0, "value": 0.8}},
            {"answer": "Set Drive's drive to 0.8 on track 0."},
        ],
    ),
    Case(
        name="install_project_package",
        goal="Install the operator package in the folder /tmp/my-pack so its operators become available.",
        grader=PatternGrader(required_tool_prefixes=["install_operator_package"],
                             required_facts=["myop"], min_tool_calls=1),
        fake_script=[
            {"tool": "install_operator_package", "args": {"path": "/tmp/my-pack"}},
            {"answer": "Compiled and registered MyOp from /tmp/my-pack; it's now spawnable."},
        ],
    ),
    # --- pick a param by SEMANTIC INTENT, not by exact name ---
    Case(
        name="semantic_intent_param",
        goal=("Make the Plasma node (id 1) glow more. Discover its parameters and set the one "
              "whose purpose is glow intensity."),
        grader=PatternGrader(required_facts=["glow"],
                             required_tool_prefixes=["list_operators", "set_node_param"],
                             min_tool_calls=2),
        fake_script=[
            {"tool": "list_operators", "args": {}},
            {"tool": "set_node_param", "args": {"node_id": 1, "name": "glow", "value": 0.9}},
            {"answer": "Plasma exposes 'glow' (intent: glow intensity); set it to 0.9."},
        ],
    ),
]


def by_name(name: str) -> Case | None:
    return next((c for c in CASES if c.name == name), None)
