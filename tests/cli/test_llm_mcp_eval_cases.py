from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from llm_mcp_eval.cases import ALL_CASES  # noqa: E402
from llm_mcp_eval.graders import PatternGraderConfig, grade_pattern  # noqa: E402
from llm_mcp_eval.loop import ConversationTrace, ToolCallRecord  # noqa: E402


def _trace(answer: str, tool_names: list[str]) -> ConversationTrace:
    return ConversationTrace(
        final_answer=answer,
        tool_call_records=[
            ToolCallRecord(name=name, arguments={}, result="{}", timestamp=float(i))
            for i, name in enumerate(tool_names)
        ],
        turns=max(len(tool_names), 1),
    )


def test_audio_fix_suggestion_case_allows_required_tools_only():
    case = ALL_CASES["audio_fix_suggestion"]

    required = {
        "main__ensure_runtime",
        "main__get_graph_errors",
        "main__inspect_graph",
        "main__compare_audio_to_intent",
        "main__evaluate_audio_musically",
    }
    forbidden = {
        "main__add_node",
        "main__connect",
        "main__disconnect",
        "main__set_param",
        "main__load_graph",
        "main__save_graph",
    }

    assert required.issubset(case.tool_allowlist)
    assert forbidden.isdisjoint(case.tool_allowlist)


def test_audio_fix_suggestion_grader_passes_on_concrete_music_eval_backed_fixes():
    config = PatternGraderConfig(
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
    )

    answer = (
        "The graph is slower and flatter than the target house groove. Raise clock1 bpm "
        "from 108 toward 124-126. Restore crisp offbeat hats by adding the hat steps back "
        "on the off-beats and increasing hat1 volume. Strengthen the backbeat by pushing "
        "snare1 and clap1 volume so beats 4 and 12 read clearly."
    )
    trace = _trace(answer, ["main__ensure_runtime", "main__compare_audio_to_intent"])

    result = grade_pattern(config, trace)
    assert result.passed


def test_audio_fix_suggestion_grader_fails_when_music_eval_or_core_fixes_are_missing():
    config = PatternGraderConfig(
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
    )

    answer = "The groove should feel more energetic and polished overall."
    trace = _trace(answer, ["main__ensure_runtime", "main__inspect_graph"])

    result = grade_pattern(config, trace)
    assert not result.passed
    failed = {check.name for check in result.checks if not check.passed}
    assert "required:tempo_fix" in failed
    assert "required:hat_fix" in failed
    assert "required:backbeat_fix" in failed
    assert "used_tool:main__compare_audio_to_intent" in failed
