"""Deterministic graders for eval cases."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

from .loop import ConversationTrace


@dataclass
class Check:
    name: str
    passed: bool
    detail: str


@dataclass
class GradeResult:
    passed: bool
    score: float
    checks: list[Check]
    notes: str


# ---------------------------------------------------------------------------
# Pattern grader — for Q&A evals
# ---------------------------------------------------------------------------


@dataclass
class PatternGraderConfig:
    """Configuration for a pattern-based Q&A grader."""
    required_facts: list[tuple[str, str]]    # (name, regex_pattern)
    rejected_claims: list[tuple[str, str]]   # (name, regex_pattern)
    min_tool_calls: int = 1
    required_tool_prefixes: list[str] = field(default_factory=list)  # e.g. ["opdev__"]


def grade_pattern(config: PatternGraderConfig, trace: ConversationTrace) -> GradeResult:
    """Grade a Q&A eval trace against pattern checks."""
    checks: list[Check] = []
    answer = trace.final_answer or ""

    # Check required facts
    for name, pattern in config.required_facts:
        found = bool(re.search(pattern, answer, re.IGNORECASE))
        checks.append(Check(
            name=f"required:{name}",
            passed=found,
            detail=f"Pattern {pattern!r} {'found' if found else 'NOT found'} in answer",
        ))

    # Check rejected claims
    for name, pattern in config.rejected_claims:
        found = bool(re.search(pattern, answer, re.IGNORECASE))
        checks.append(Check(
            name=f"rejected:{name}",
            passed=not found,
            detail=f"Rejected pattern {pattern!r} {'found (FAIL)' if found else 'not found (ok)'} in answer",
        ))

    # Check minimum tool calls
    total_calls = len(trace.tool_call_records)
    checks.append(Check(
        name="min_tool_calls",
        passed=total_calls >= config.min_tool_calls,
        detail=f"{total_calls} tool calls made (min {config.min_tool_calls})",
    ))

    # Check required tool prefixes
    for prefix in config.required_tool_prefixes:
        used = any(r.name.startswith(prefix) for r in trace.tool_call_records)
        checks.append(Check(
            name=f"used_prefix:{prefix}",
            passed=used,
            detail=f"Tool prefix {prefix!r} {'used' if used else 'NOT used'}",
        ))

    passed_count = sum(1 for c in checks if c.passed)
    total = len(checks)
    score = passed_count / total if total > 0 else 0.0
    all_passed = all(c.passed for c in checks)

    return GradeResult(
        passed=all_passed,
        score=score,
        checks=checks,
        notes=f"{passed_count}/{total} checks passed",
    )


# ---------------------------------------------------------------------------
# Operator grader — for code generation evals
# ---------------------------------------------------------------------------


@dataclass
class OperatorGraderConfig:
    """Configuration for the operator generation grader."""
    min_tool_calls: int = 1
    required_tool_prefixes: list[str] = field(default_factory=list)


@dataclass
class OperatorTestResult:
    """Result from compiling and testing operator code."""
    code_extracted: bool
    source: str
    compile_ok: bool
    test_passed: bool
    output: str


def extract_cpp_code(answer: str) -> str | None:
    """Extract the first C++ fenced code block from the answer."""
    # Match ```cpp ... ``` or ```c++ ... ``` or ``` ... ``` (greedy last-resort)
    patterns = [
        r"```(?:cpp|c\+\+)\s*\n(.*?)```",
        r"```\s*\n(.*?)```",
    ]
    for pat in patterns:
        m = re.search(pat, answer, re.DOTALL)
        if m:
            code = m.group(1).strip()
            # Basic sanity: must look like an operator
            if "VIVID_REGISTER" in code or "process_frame" in code:
                return code
    return None


def grade_operator(
    config: OperatorGraderConfig,
    trace: ConversationTrace,
    test_result: OperatorTestResult | None,
) -> GradeResult:
    """Grade an operator generation eval."""
    checks: list[Check] = []

    # Code extraction
    extracted = test_result is not None and test_result.code_extracted
    checks.append(Check(
        name="code_extracted",
        passed=extracted,
        detail="C++ code block extracted from answer" if extracted else "No valid C++ code block found",
    ))

    # Compilation
    compiled = test_result is not None and test_result.compile_ok
    checks.append(Check(
        name="compile_ok",
        passed=compiled,
        detail="Compilation succeeded" if compiled else "Compilation failed",
    ))

    # Behavior test
    test_passed = test_result is not None and test_result.test_passed
    checks.append(Check(
        name="test_passed",
        passed=test_passed,
        detail="Behavior test passed" if test_passed else "Behavior test failed",
    ))

    # Tool call checks
    total_calls = len(trace.tool_call_records)
    checks.append(Check(
        name="min_tool_calls",
        passed=total_calls >= config.min_tool_calls,
        detail=f"{total_calls} tool calls made (min {config.min_tool_calls})",
    ))

    for prefix in config.required_tool_prefixes:
        used = any(r.name.startswith(prefix) for r in trace.tool_call_records)
        checks.append(Check(
            name=f"used_prefix:{prefix}",
            passed=used,
            detail=f"Tool prefix {prefix!r} {'used' if used else 'NOT used'}",
        ))

    passed_count = sum(1 for c in checks if c.passed)
    total = len(checks)
    score = passed_count / total if total > 0 else 0.0
    all_passed = all(c.passed for c in checks)

    return GradeResult(
        passed=all_passed,
        score=score,
        checks=checks,
        notes=f"{passed_count}/{total} checks passed",
    )
