#!/usr/bin/env python3
"""Vivid LLM MCP Eval Harness — validates MCP information sufficiency with real LLMs."""

from __future__ import annotations

import argparse
import asyncio
import pathlib
import sys

from dotenv import load_dotenv

# Ensure the scripts directory is on the path
SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))

from llm_mcp_eval.cases import ALL_CASES, EvalCase, SYSTEM_PROMPT
from llm_mcp_eval.graders import OperatorTestResult, extract_cpp_code
from llm_mcp_eval.loop import run_eval_loop
from llm_mcp_eval.operator_harness import build_and_test
from llm_mcp_eval.providers import AnthropicProvider, FakeProvider, OpenAIProvider, Provider
from llm_mcp_eval.schema import mcp_to_anthropic, mcp_to_openai
from llm_mcp_eval.sessions import DualMCPSessions
from llm_mcp_eval.transcript import save_eval_result


def make_provider(name: str, model: str | None) -> Provider:
    if name == "fake":
        # FakeProvider sequences are set per-case, so return a placeholder
        return None  # type: ignore  — replaced per case
    elif name == "openai":
        return OpenAIProvider(model=model or "gpt-4o")
    elif name == "anthropic":
        return AnthropicProvider(model=model or "claude-sonnet-4-20250514")
    else:
        raise ValueError(f"Unknown provider: {name}")


async def run_case(
    case: EvalCase,
    provider: Provider,
    provider_name: str,
    output_dir: pathlib.Path,
    max_turns: int,
) -> bool:
    """Run a single eval case. Returns True if passed."""

    print(f"\n{'='*60}")
    print(f"  Case: {case.name}  |  Provider: {provider_name}  |  Model: {provider.model}")
    print(f"{'='*60}")

    async with DualMCPSessions() as sessions:
        # Get tools filtered to this case's allowlist
        ns_tools = sessions.list_tools(case.tool_allowlist)
        if not ns_tools:
            print(f"  WARNING: no tools matched allowlist for {case.name}")

        # Convert schemas for the provider
        if provider_name == "anthropic":
            tool_schemas = [mcp_to_anthropic(t.prefixed_name, t.tool) for t in ns_tools]
        else:
            tool_schemas = [mcp_to_openai(t.prefixed_name, t.tool) for t in ns_tools]

        # Run the conversation loop
        trace = await run_eval_loop(
            provider=provider,
            sessions=sessions,
            system_prompt=SYSTEM_PROMPT,
            user_prompt=case.user_prompt,
            tools=tool_schemas,
            max_turns=max_turns,
        )

    print(f"  Turns: {trace.turns}  |  Tool calls: {len(trace.tool_call_records)}  |  Truncated: {trace.truncated}")

    # For operator cases, run the build-and-test harness
    operator_result: OperatorTestResult | None = None
    if case.grader_type == "operator" and trace.final_answer:
        source = extract_cpp_code(trace.final_answer)
        if source:
            print("  Building and testing operator...")
            isolated_home = output_dir / ".eval_home"
            isolated_home.mkdir(parents=True, exist_ok=True)
            operator_result = await build_and_test(source, output_dir, isolated_home)
            print(f"  Compile: {'OK' if operator_result.compile_ok else 'FAIL'}  |  "
                  f"Test: {'PASS' if operator_result.test_passed else 'FAIL'}")
        else:
            print("  No valid C++ code block found in answer")
            operator_result = OperatorTestResult(
                code_extracted=False, source="", compile_ok=False, test_passed=False, output="",
            )

    # Grade
    grade = case.grade(trace, operator_result)

    # Save artifacts
    save_eval_result(
        case_name=case.name,
        provider_name=provider_name,
        model=provider.model,
        trace=trace,
        grade=grade,
        output_dir=output_dir,
        operator_result=operator_result,
    )

    # Print results
    status = "PASS" if grade.passed else "FAIL"
    print(f"  Result: {status}  ({grade.notes})")
    for check in grade.checks:
        mark = "+" if check.passed else "-"
        print(f"    [{mark}] {check.name}: {check.detail}")

    return grade.passed


async def main() -> int:
    load_dotenv(REPO_ROOT / ".env", override=False)

    parser = argparse.ArgumentParser(description="Vivid LLM MCP Eval Harness")
    parser.add_argument("--provider", required=True, choices=["openai", "anthropic", "fake"])
    parser.add_argument("--model", default=None, help="Model name (default per provider)")
    parser.add_argument("--case", default="all", help="Case name or 'all'")
    parser.add_argument("--output-dir", default="build/llm_mcp_evals", help="Artifact output directory")
    parser.add_argument("--max-turns", type=int, default=20, help="Max conversation turns")
    args = parser.parse_args()

    output_dir = pathlib.Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    # Select cases
    if args.case == "all":
        cases = list(ALL_CASES.values())
    elif args.case in ALL_CASES:
        cases = [ALL_CASES[args.case]]
    else:
        print(f"Unknown case: {args.case}")
        print(f"Available: {', '.join(ALL_CASES.keys())}")
        return 1

    results: list[tuple[str, bool]] = []

    for case in cases:
        if args.provider == "fake":
            provider = FakeProvider(case.fake_sequence)
        else:
            provider = make_provider(args.provider, args.model)

        passed = await run_case(case, provider, args.provider, output_dir, args.max_turns)
        results.append((case.name, passed))

    # Summary
    print(f"\n{'='*60}")
    print("  SUMMARY")
    print(f"{'='*60}")
    all_passed = True
    for name, passed in results:
        status = "PASS" if passed else "FAIL"
        print(f"  {status}  {name}")
        if not passed:
            all_passed = False

    print()
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
