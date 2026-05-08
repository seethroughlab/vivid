"""Eval case definitions — prompts, tool allowlists, grader configs, and fake sequences."""

from __future__ import annotations

from dataclasses import dataclass, field
import pathlib
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

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BAD_HOUSE_FIXTURE = (
    REPO_ROOT / "tests" / "fixtures" / "llm_mcp_eval" / "audio_fix_suggestion_bad_house.json"
)


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
# Case 5: Live music-eval fix suggestions for a sub-par graph
# ---------------------------------------------------------------------------

AUDIO_FIX_SUGGESTION_TOOLS: set[str] = {
    "main__ensure_runtime",
    "main__get_graph_errors",
    "main__inspect_graph",
    "main__compare_audio_to_intent",
    "main__evaluate_audio_musically",
}


AUDIO_FIX_SUGGESTION = EvalCase(
    name="audio_fix_suggestion",
    user_prompt=(
        "Load and evaluate the live graph fixture at "
        f"`{BAD_HOUSE_FIXTURE}`.\n\n"
        "The target is: a tight four-on-the-floor house groove with strong kick pulse, "
        "audible snare/clap backbeat, and crisp offbeat hats.\n\n"
        "Use the live runtime/music-eval MCP tools to listen to the graph first. Then "
        "recommend specific Vivid graph edits to move it toward that target. Keep this "
        "suggestion-only: do not mutate the graph. Your answer must mention which node, "
        "pattern, or parameter to change and why, with concise evidence from the tool output."
    ),
    tool_allowlist=AUDIO_FIX_SUGGESTION_TOOLS,
    grader_type="pattern",
    pattern_config=PatternGraderConfig(
        required_facts=[
            ("tempo_fix", r"(?s)(?:clock1|tempo|bpm).*(?:12[0-9]|13[0-9]|increase|raise)"),
            ("hat_fix", r"(?s)(?:hat|hi-?hat).*(?:offbeat|off-beat|pattern|step|volume|level)"),
            ("backbeat_fix", r"(?s)(?:snare|clap|backbeat).*(?:volume|level|accent|stronger|present)"),
        ],
        rejected_claims=[],
        min_tool_calls=2,
        required_tool_names=[
            "main__ensure_runtime",
            "main__compare_audio_to_intent",
        ],
    ),
    fake_sequence=[
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="main__ensure_runtime",
                arguments={"graph_path": str(BAD_HOUSE_FIXTURE)},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-2",
                name="main__get_graph_errors",
                arguments={},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-3",
                name="main__compare_audio_to_intent",
                arguments={
                    "intent": "a tight four-on-the-floor house groove with strong kick pulse, audible snare/clap backbeat, and crisp offbeat hats",
                    "window_seconds": 20.0,
                    "include_payload": False,
                },
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=(
                "The graph is producing valid audio, but it misses the target house feel. "
                "The biggest fixes are:\n\n"
                "1. Raise `clock1` BPM from 108 into a house range around 124-126 so the groove "
                "stops dragging.\n"
                "2. Restore the offbeat hi-hat feel in `seq1` by adding hat triggers back on the "
                "off-beats and increasing `hat1` volume so the hats read as crisp timekeeping instead "
                "of a faint on-beat tick.\n"
                "3. Push the backbeat forward by increasing `snare1` and `clap1` presence so beats "
                "4 and 12 land clearly as the snare/clap accent.\n\n"
                "Evidence: the music-eval comparison says the rhythm feels slower and sparser than the "
                "target, with missing hat drive and a weak backbeat."
            ),
            tool_calls=[],
            stop_reason="end_turn",
        ),
    ],
)


# ---------------------------------------------------------------------------
# Case 6: Visual frame evaluation
# ---------------------------------------------------------------------------

VISUAL_EVAL_TOOLS: set[str] = {
    "main__ensure_runtime",
    "main__evaluate_live_frame",
    "main__compare_frame_to_intent",
}

BLOOM_FIXTURE = REPO_ROOT / "graphs" / "gpu" / "bloom_demo.json"

VISUAL_FIX_SUGGESTION = EvalCase(
    name="visual_fix_suggestion",
    user_prompt=(
        "Load the bloom demo graph at "
        f"`{BLOOM_FIXTURE}`.\n\n"
        "The target visual character is: warm, glowing, high-energy light with "
        "clear movement and a bright color mood.\n\n"
        "Use the visual frame evaluation tools to capture the live output and assess "
        "how closely it matches that intent. Report the match_score, the top style tags "
        "you observe, and whether the visual character matches the target description. "
        "Be specific — quote the tags and score from the tool output."
    ),
    tool_allowlist=VISUAL_EVAL_TOOLS,
    grader_type="pattern",
    pattern_config=PatternGraderConfig(
        required_facts=[
            ("used_score", r"match_score|score.{0,30}[0-9]\.[0-9]"),
            ("cited_tags", r"(?:style.?tag|tag).*(?:warm|glow|bright|movement|energy|dark|abstract|cinematic)"),
            ("visual_judgment", r"(?:match(?:es)?|align|close|far|differ|contrast|similar)"),
        ],
        rejected_claims=[],
        min_tool_calls=2,
        required_tool_names=[
            "main__ensure_runtime",
            "main__compare_frame_to_intent",
        ],
    ),
    fake_sequence=[
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-1",
                name="main__ensure_runtime",
                arguments={"graph_path": str(BLOOM_FIXTURE)},
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=None,
            tool_calls=[ToolCall(
                id="fake-2",
                name="main__compare_frame_to_intent",
                arguments={
                    "intent": "warm, glowing, high-energy light with clear movement and a bright color mood",
                    "include_payload": True,
                },
            )],
            stop_reason="tool_use",
        ),
        AssistantMessage(
            text=(
                "The live frame scores 0.18 against the intent "
                "\"warm, glowing, high-energy light with clear movement and a bright color mood.\"\n\n"
                "Top style tags from the frame: `glowing` (0.22, color_mood), `movement` (0.19, movement), "
                "`warm` (0.17, color_mood), `bright` (0.15, visual_style).\n\n"
                "The visual character broadly matches the target: the warm and glowing tags align well with "
                "the intent, and movement is present. The score of 0.18 is in the mid range for SigLIP "
                "cosine similarity (meaningful matches typically fall 0.10–0.30), indicating a reasonable "
                "but not tight alignment. The main gap is energy — the output reads as moderately bright "
                "rather than high-energy."
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
    for c in [
        MCP_SPLIT,
        ARCHITECTURE_TRICK,
        OPERATOR_API_LOOKUP,
        THRESHOLD_GATE_OPERATOR,
        AUDIO_FIX_SUGGESTION,
        VISUAL_FIX_SUGGESTION,
    ]
}
