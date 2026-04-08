"""Eval case definitions — prompts, tool allowlists, grader configs, and fake sequences."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .graders import (
    GradeResult,
    OperatorGraderConfig,
    OperatorTestResult,
    PatternGraderConfig,
    grade_operator,
    grade_pattern,
)
from .loop import ConversationTrace
from .providers import AssistantMessage, ToolCall

# ---------------------------------------------------------------------------
# Shared constants
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """\
You are an expert on the Vivid real-time audiovisual graph engine. You have access \
to MCP tools that let you search source code, read API documentation, browse example \
operators, and inspect type catalogs. Use these tools to answer questions — do not \
rely on prior knowledge alone. Cite evidence from tool results in your final answer.

Tool names are prefixed with "main__" (live graph / catalog tools) or "opdev__" \
(operator-development / source-reading tools). Call whichever tools are relevant \
to discover the answer."""

OPDEV_READONLY_TOOLS: set[str] = {
    "opdev__list_source_roots",
    "opdev__search_source",
    "opdev__read_source_file",
    "opdev__read_source_span",
    "opdev__find_symbol",
    "opdev__find_references",
    "opdev__get_operator_api_docs",
    "opdev__list_example_operators",
    "opdev__get_example_operator",
    "opdev__search_example_operators",
    "opdev__get_capability_guidance",
    "opdev__recommend_starting_point",
    "opdev__get_api_header",
}

MAIN_READONLY_TOOLS: set[str] = {
    "main__list_types",
    "main__operator_docs",
    "main__introspect_nodes",
    "main__inspect_graph",
    "main__run_diagnostics",
}


# ---------------------------------------------------------------------------
# Case data type
# ---------------------------------------------------------------------------

@dataclass
class EvalCase:
    name: str
    user_prompt: str
    tool_allowlist: set[str]
    grader_type: str  # "pattern" or "operator"
    pattern_config: PatternGraderConfig | None = None
    operator_config: OperatorGraderConfig | None = None
    fake_sequence: list[AssistantMessage] = field(default_factory=list)

    def grade(
        self,
        trace: ConversationTrace,
        operator_result: OperatorTestResult | None = None,
    ) -> GradeResult:
        if self.grader_type == "pattern":
            assert self.pattern_config is not None
            return grade_pattern(self.pattern_config, trace)
        else:
            assert self.operator_config is not None
            return grade_operator(self.operator_config, trace, operator_result)


# ---------------------------------------------------------------------------
# Case 1: MCP split / ownership
# ---------------------------------------------------------------------------

MCP_SPLIT = EvalCase(
    name="mcp_split",
    user_prompt=(
        "Vivid exposes two separate MCP servers. Which server owns live runtime "
        "graph control (adding nodes, setting parameters, capturing output) versus "
        "operator-authoring support (source search, API docs, example operators, "
        "scaffolding)? Cite evidence from MCP-accessible docs or source to support "
        "your answer. Do NOT collapse both roles into a single server."
    ),
    tool_allowlist=OPDEV_READONLY_TOOLS,
    grader_type="pattern",
    pattern_config=PatternGraderConfig(
        required_facts=[
            ("main_server_graph", r"vivid[_\s]?mcp|main.*(?:graph|runtime|node|param)"),
            ("opdev_server_authoring", r"(?:opdev|operator.dev).*(?:source|doc|example|scaffold)"),
        ],
        rejected_claims=[
            ("single_server", r"(?:single|one|same)\s+(?:mcp\s+)?server\s+(?:handles|does|owns)\s+(?:both|everything)"),
        ],
        min_tool_calls=1,
        required_tool_prefixes=["opdev__"],
    ),
    fake_sequence=[
        # Turn 1: model calls a search tool
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="opdev__search_source",
                arguments={"query": "MCP server responsibilities", "roots": ["mcp"]},
            )],
            stop_reason="tool_use",
        ),
        # Turn 2: model gives final answer
        AssistantMessage(
            text=(
                "Vivid uses two MCP servers with distinct roles:\n\n"
                "1. **vivid_mcp.py** (main server) — owns live runtime graph control: "
                "add_node, connect, set_param, capture_image, save_graph, etc.\n\n"
                "2. **vivid_opdev_mcp.py** (opdev server) — owns operator-authoring: "
                "search_source, read_source_file, get_operator_api_docs, "
                "list_example_operators, scaffold_operator, rebuild_package.\n\n"
                "Evidence: searched MCP source and found two separate FastMCP instances "
                "with non-overlapping tool sets."
            ),
            tool_calls=[],
            stop_reason="end_turn",
        ),
    ],
)


# ---------------------------------------------------------------------------
# Case 2: Architecture trick question
# ---------------------------------------------------------------------------

ARCHITECTURE_TRICK = EvalCase(
    name="architecture_trick",
    user_prompt=(
        "In Vivid, do audio operators and GPU operators communicate directly with "
        "each other? How does data flow between the audio cadence (~48 kHz) and the "
        "frame cadence (~60 Hz)? What role does the Control domain play in bridging "
        "these cadences? Cite evidence from architecture docs or source code."
    ),
    tool_allowlist=OPDEV_READONLY_TOOLS,
    grader_type="pattern",
    pattern_config=PatternGraderConfig(
        required_facts=[
            ("no_direct", r"(?:do\s+not|don.t|cannot|never|no)\s+(?:communicate|connect|route|wire)\s+directly"),
            ("control_bridge", r"(?:control|frame).*(bridge|mediat|rout|between)"),
            ("audio_frame_bridge", r"audio.?frame.?bridge|double.?buffer|lock.?free|snapshot"),
        ],
        rejected_claims=[
            ("direct_audio_gpu", r"audio\s+(?:and|&)\s+gpu\s+(?:communicate|connect|wire)\s+directly"),
        ],
        min_tool_calls=1,
        required_tool_prefixes=["opdev__"],
    ),
    fake_sequence=[
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="opdev__search_source",
                arguments={"query": "AudioFrameBridge cross-cadence", "roots": ["docs"]},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=(
                "Audio and GPU operators do not communicate directly. They run at "
                "different cadences — audio at ~48 kHz on a real-time thread, GPU at "
                "~60 Hz on the main thread. All cross-cadence data flows through the "
                "Control domain, which mediates between the two via the "
                "AudioFrameBridge. This bridge uses lock-free double-buffered snapshots "
                "(ParamSnapshot, AnalysisSnapshot, LaneSnapshot) so neither cadence "
                "blocks the other."
            ),
            tool_calls=[],
            stop_reason="end_turn",
        ),
    ],
)


# ---------------------------------------------------------------------------
# Case 3: Operator API lookup
# ---------------------------------------------------------------------------

OPERATOR_API_LOOKUP = EvalCase(
    name="operator_api_lookup",
    user_prompt=(
        "How do I write a minimal control-domain operator in Vivid with float "
        "parameters and scalar input/output ports? Show the required includes, "
        "class structure, port/param registration, processing method, and "
        "registration macro. Cite your sources — use opdev tools to find API docs "
        "or example operators."
    ),
    tool_allowlist=OPDEV_READONLY_TOOLS,
    grader_type="pattern",
    pattern_config=PatternGraderConfig(
        required_facts=[
            ("operator_base", r"OperatorBase"),
            ("param_float", r"Param\s*<\s*float\s*>"),
            ("vivid_register", r"VIVID_REGISTER"),
            ("process_frame", r"process_frame"),
            ("collect_params", r"collect_params"),
            ("collect_ports", r"collect_ports"),
        ],
        rejected_claims=[],
        min_tool_calls=1,
        required_tool_prefixes=["opdev__"],
    ),
    fake_sequence=[
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="opdev__get_operator_api_docs",
                arguments={"topic": "core"},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-2",
                name="opdev__get_example_operator",
                arguments={"env": "control", "name": "math"},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=(
                "A minimal control operator needs:\n\n"
                "```cpp\n"
                '#include "operator_api/operator.h"\n\n'
                "struct MyOp : vivid::OperatorBase, vivid::FrameProcessable {\n"
                '    static constexpr const char* kName = "MyOp";\n'
                "    static constexpr bool kTimeDependent = false;\n\n"
                '    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 10.0f};\n\n'
                "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n"
                "        out.push_back(&gain);\n"
                "    }\n\n"
                "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
                '        out.push_back({"in", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});\n'
                '        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});\n'
                "    }\n\n"
                "    void process_frame(const VividFrameContext* ctx) override {\n"
                "        ctx->output_values[0] = ctx->input_values[0] * gain.value();\n"
                "    }\n"
                "};\n\n"
                "VIVID_REGISTER(MyOp)\n"
                "```\n\n"
                "Source: opdev API docs (core topic) and the Math example operator."
            ),
            tool_calls=[],
            stop_reason="end_turn",
        ),
    ],
)


# ---------------------------------------------------------------------------
# Case 4: ThresholdGate operator generation
# ---------------------------------------------------------------------------

THRESHOLD_GATE_OPERATOR = EvalCase(
    name="threshold_gate_operator",
    user_prompt=(
        "Write a complete Vivid control-domain operator named `ThresholdGate` with "
        "the following specification:\n\n"
        "- One scalar input port: `in`\n"
        "- One scalar output port: `gate`\n"
        "- One `Param<float>` named `threshold`, default `0.5`, range `0.0..1.0`\n"
        "- Output `1.0` when `in >= threshold`, otherwise `0.0`\n"
        "- Register with `VIVID_REGISTER(ThresholdGate)`\n\n"
        "Use opdev tools to look up the operator API and study example operators "
        "before writing your code. Present the complete, compilable `.cpp` file in "
        "a single fenced code block."
    ),
    tool_allowlist=OPDEV_READONLY_TOOLS,
    grader_type="operator",
    operator_config=OperatorGraderConfig(
        min_tool_calls=1,
        required_tool_prefixes=["opdev__"],
    ),
    fake_sequence=[
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="opdev__get_operator_api_docs",
                arguments={"topic": "control"},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-2",
                name="opdev__get_example_operator",
                arguments={"env": "control", "name": "math"},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=(
                "Here is the complete ThresholdGate operator:\n\n"
                "```cpp\n"
                '#include "operator_api/operator.h"\n\n'
                "struct ThresholdGate : vivid::OperatorBase, vivid::FrameProcessable {\n"
                '    static constexpr const char* kName = "ThresholdGate";\n'
                "    static constexpr bool kTimeDependent = false;\n\n"
                '    vivid::Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};\n\n'
                "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n"
                "        out.push_back(&threshold);\n"
                "    }\n\n"
                "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
                '        out.push_back({"in",   VIVID_PORT_SCALAR, VIVID_PORT_INPUT});\n'
                '        out.push_back({"gate", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});\n'
                "    }\n\n"
                "    void process_frame(const VividFrameContext* ctx) override {\n"
                "        float in_val = ctx->input_values[0];\n"
                "        float thresh = ctx->param_values[0];\n"
                "        ctx->output_values[0] = (in_val >= thresh) ? 1.0f : 0.0f;\n"
                "    }\n"
                "};\n\n"
                "VIVID_REGISTER(ThresholdGate)\n"
                "```"
            ),
            tool_calls=[],
            stop_reason="end_turn",
        ),
    ],
)


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

ALL_CASES: dict[str, EvalCase] = {
    c.name: c
    for c in [MCP_SPLIT, ARCHITECTURE_TRICK, OPERATOR_API_LOOKUP, THRESHOLD_GATE_OPERATOR]
}
