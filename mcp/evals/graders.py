"""Graders (P4.6): score a Transcript (+ optional end state) into pass/fail + reasons.

Pure functions of the transcript and transport state — no LLM, no network — so they're
unit-tested directly (llm_mcp_eval.py --selftest)."""
from __future__ import annotations

from dataclasses import dataclass, field

from harness import Transcript


@dataclass
class Grade:
    passed: bool
    reasons: list[str] = field(default_factory=list)


@dataclass
class PatternGrader:
    """Check the final answer text + the tool-call log. required_facts: substrings that
    must appear (case-insensitive); rejected_claims: substrings that must NOT; min_tool_calls;
    required_tool_prefixes: each listed prefix must match some called tool."""
    required_facts: list[str] = field(default_factory=list)
    rejected_claims: list[str] = field(default_factory=list)
    min_tool_calls: int = 0
    required_tool_prefixes: list[str] = field(default_factory=list)

    def grade(self, t: Transcript, transport=None) -> Grade:
        reasons: list[str] = []
        ans = t.final_answer.lower()
        for fact in self.required_facts:
            if fact.lower() not in ans:
                reasons.append(f"missing required fact: {fact!r}")
        for claim in self.rejected_claims:
            if claim.lower() in ans:
                reasons.append(f"contains rejected claim: {claim!r}")
        if len(t.steps) < self.min_tool_calls:
            reasons.append(f"too few tool calls: {len(t.steps)} < {self.min_tool_calls}")
        called = t.tools_called()
        for pre in self.required_tool_prefixes:
            if not any(c.startswith(pre) for c in called):
                reasons.append(f"no tool call matching prefix {pre!r}")
        return Grade(not reasons, reasons)


@dataclass
class SceneGrader:
    """Assert the end state of the graph the agent built — reads get_session via the
    transport. min_nodes / required_ops (each op name must appear in the chain)."""
    min_nodes: int = 0
    required_ops: list[str] = field(default_factory=list)

    def grade(self, t: Transcript, transport=None) -> Grade:
        reasons: list[str] = []
        if transport is None:
            return Grade(False, ["SceneGrader needs a transport"])
        chain = transport.post("get_session", {}).get("graph", {}).get("chain", [])
        if len(chain) < self.min_nodes:
            reasons.append(f"too few nodes: {len(chain)} < {self.min_nodes}")
        ops = {n.get("op") for n in chain}
        for op in self.required_ops:
            if op not in ops:
                reasons.append(f"missing op in scene: {op!r}")
        return Grade(not reasons, reasons)


@dataclass
class AllOf:
    """Composite: passes only if every sub-grader passes."""
    graders: list

    def grade(self, t: Transcript, transport=None) -> Grade:
        reasons: list[str] = []
        for g in self.graders:
            r = g.grade(t, transport)
            reasons += r.reasons
        return Grade(not reasons, reasons)
