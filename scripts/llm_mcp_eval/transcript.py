"""Artifact saving — transcripts, tool-call traces, grades, and operator artifacts."""

from __future__ import annotations

import json
import pathlib
from dataclasses import asdict
from typing import Any

from .graders import GradeResult, OperatorTestResult
from .loop import ConversationTrace


def save_eval_result(
    case_name: str,
    provider_name: str,
    model: str,
    trace: ConversationTrace,
    grade: GradeResult,
    output_dir: pathlib.Path,
    operator_result: OperatorTestResult | None = None,
) -> pathlib.Path:
    """Save all artifacts for a single eval run. Returns the case output directory."""

    case_dir = output_dir / case_name / provider_name
    case_dir.mkdir(parents=True, exist_ok=True)

    # Transcript
    (case_dir / "transcript.json").write_text(
        json.dumps(trace.messages, indent=2, default=str) + "\n"
    )

    # Tool call records
    records = [
        {
            "name": r.name,
            "arguments": r.arguments,
            "result": r.result[:2000],  # cap for readability
            "timestamp": r.timestamp,
        }
        for r in trace.tool_call_records
    ]
    (case_dir / "tool_calls.json").write_text(
        json.dumps(records, indent=2, default=str) + "\n"
    )

    # Grade
    grade_dict = {
        "passed": grade.passed,
        "score": grade.score,
        "notes": grade.notes,
        "model": model,
        "turns": trace.turns,
        "truncated": trace.truncated,
        "checks": [
            {"name": c.name, "passed": c.passed, "detail": c.detail}
            for c in grade.checks
        ],
    }
    (case_dir / "grade.json").write_text(
        json.dumps(grade_dict, indent=2) + "\n"
    )

    # Final answer
    if trace.final_answer:
        (case_dir / "answer.md").write_text(trace.final_answer + "\n")

    # Operator artifacts
    if operator_result is not None:
        if operator_result.source:
            (case_dir / "operator_source.cpp").write_text(operator_result.source + "\n")
        if operator_result.output:
            (case_dir / "test_output.txt").write_text(operator_result.output + "\n")

    return case_dir
