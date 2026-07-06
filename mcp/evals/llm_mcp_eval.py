# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""LLM-over-MCP eval runner (P4.6).

Runs eval cases by driving the Vivid control surface through a provider (an agent) and
grading the result. The `fake` provider + a mock transport need no app and no API key, so
the whole loop+grader pipeline is verifiable offline:

  uv run mcp/evals/llm_mcp_eval.py --selftest                 # all cases, fake+mock (CI)
  uv run mcp/evals/llm_mcp_eval.py --provider fake --case all

Real runs drive a LIVE app (start it first) with a real model (needs the provider SDK + an
API key):

  uv run --with anthropic mcp/evals/llm_mcp_eval.py \\
      --provider anthropic --model claude-opus-4-8 --case all --base-url http://127.0.0.1:9876
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))  # import sibling modules when run via uv

from cases import CASES, by_name           # noqa: E402
from harness import MockTransport, RealTransport, run_loop  # noqa: E402
from providers import make_provider          # noqa: E402


def run_case(case, provider_name: str, model: str, base_url: str) -> dict:
    transport = MockTransport() if provider_name == "fake" else RealTransport(base_url)
    provider = make_provider(provider_name, model, case)
    transcript = run_loop(provider, transport, case.goal)
    grade = case.grader.grade(transcript, transport)
    return {
        "case": case.name,
        "passed": grade.passed,
        "reasons": grade.reasons,
        "tools_called": transcript.tools_called(),
        "final_answer": transcript.final_answer,
    }


def selftest() -> int:
    failures = 0
    for case in CASES:
        r = run_case(case, "fake", "", "")
        ok = "OK  " if r["passed"] else "FAIL"
        print(f"  [{ok}] {case.name}" + ("" if r["passed"] else f"  {r['reasons']}"))
        failures += 0 if r["passed"] else 1
    if failures:
        print(f"llm_mcp_eval selftest: {failures} case(s) failed", file=sys.stderr)
        return 1
    print(f"llm_mcp_eval selftest: OK ({len(CASES)} cases, fake provider + mock transport)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Vivid LLM-over-MCP eval")
    ap.add_argument("--provider", choices=["fake", "anthropic", "openai"], default="fake")
    ap.add_argument("--model", default="")
    ap.add_argument("--case", default="all", help="case name or 'all'")
    ap.add_argument("--base-url",
                    default=f"http://127.0.0.1:{os.environ.get('VIVID_PORT', '9876')}")
    ap.add_argument("--output-dir", type=Path)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    cases = CASES if args.case == "all" else [c for c in [by_name(args.case)] if c]
    if not cases:
        print(f"unknown case: {args.case}", file=sys.stderr)
        return 2

    results = [run_case(c, args.provider, args.model, args.base_url) for c in cases]
    passed = sum(r["passed"] for r in results)
    for r in results:
        mark = "OK  " if r["passed"] else "FAIL"
        print(f"[{mark}] {r['case']}" + ("" if r["passed"] else f"  {r['reasons']}"))
    print(f"{passed}/{len(results)} cases passed ({args.provider})")

    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        out = args.output_dir / f"evals-{args.provider}.json"
        out.write_text(json.dumps({"provider": args.provider, "model": args.model,
                                   "results": results}, indent=2) + "\n")
        print(f"-> {out}")

    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
