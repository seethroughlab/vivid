#!/usr/bin/env python3
"""Audit how well MCP guidance covers the loaded Vivid operator catalog."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_POLICY = REPO_ROOT / "mcp" / "operator_coverage_policy.json"
MCP_SCAN_PATHS = [
    REPO_ROOT / "mcp" / "vivid_mcp.py",
    REPO_ROOT / "mcp" / "vivid_opdev_mcp.py",
    REPO_ROOT / "mcp" / "opdev_docs",
]
CORE_OPERATOR_ENVS = ("audio", "control", "gpu")
VALID_KINDS = {"audio", "control", "gpu", "module"}
LEGACY_OK_WORDS = ("legacy", "compat", "compatibility", "old graph", "older graph", "superseded")


@dataclass
class Finding:
    severity: str
    code: str
    operator: str
    message: str
    path: str = ""


@dataclass
class AuditResult:
    ok: bool = True
    summary: dict[str, int] = field(default_factory=dict)
    findings: list[Finding] = field(default_factory=list)
    operators: list[dict[str, Any]] = field(default_factory=list)


def load_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def parse_json_from_stdout(stdout: str) -> dict[str, Any]:
    for line in reversed(stdout.splitlines()):
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            continue
    raise RuntimeError("vivid list-types did not emit a JSON object")


def run_list_types(vivid: pathlib.Path) -> list[dict[str, Any]]:
    proc = subprocess.run(
        [str(vivid), "list-types", "--json"],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"{vivid} list-types --json failed with {proc.returncode}\n{proc.stderr}"
        )
    data = parse_json_from_stdout(proc.stdout)
    if not data.get("ok"):
        raise RuntimeError(f"list-types returned failure: {data}")
    result = data.get("result", data)
    types = result.get("types")
    if not isinstance(types, list):
        raise RuntimeError("list-types JSON did not contain result.types")
    return types


def load_catalog(catalog_path: pathlib.Path | None, vivid: pathlib.Path) -> list[dict[str, Any]]:
    if catalog_path:
        data = load_json(catalog_path)
        result = data.get("result", data)
        types = result.get("types")
        if not isinstance(types, list):
            raise RuntimeError(f"{catalog_path} did not contain result.types")
        return types
    return run_list_types(vivid)


def scan_core_operator_names() -> set[str]:
    names: set[str] = {"audio_out", "video_out"}
    for env in CORE_OPERATOR_ENVS:
        env_dir = REPO_ROOT / "operators" / env
        if not env_dir.is_dir():
            continue
        for source in env_dir.glob("*/*"):
            if source.suffix not in {".cpp", ".h", ".mm"}:
                continue
            text = source.read_text(encoding="utf-8", errors="ignore")
            names.update(re.findall(r'kName\s*=\s*"([^"]+)"', text))
            names.update(m.strip() for m in re.findall(r"VIVID_DEFINE_OP\(([^)]+)\)", text))
    return names


def scan_mcp_text(paths: list[pathlib.Path] = MCP_SCAN_PATHS) -> dict[str, str]:
    out: dict[str, str] = {}
    for path in paths:
        if path.is_file():
            out[str(path.relative_to(REPO_ROOT))] = path.read_text(
                encoding="utf-8", errors="ignore"
            )
        elif path.is_dir():
            for child in sorted(path.rglob("*")):
                if child.is_file() and child.suffix in {".py", ".md", ".txt"}:
                    out[str(child.relative_to(REPO_ROOT))] = child.read_text(
                        encoding="utf-8", errors="ignore"
                    )
    return out


def classify_operator(op: dict[str, Any], core_names: set[str]) -> str:
    if op.get("is_module") or op.get("kind") == "module":
        return "module"
    if op.get("name") in core_names:
        return "core"
    return "advisory"


def has_discovery_docs(op: dict[str, Any]) -> bool:
    return bool(op.get("has_docs") or op.get("brief") or op.get("summary"))


def text_has_legacy_context(text: str) -> bool:
    low = text.lower()
    return any(word in low for word in LEGACY_OK_WORDS)


def add(result: AuditResult, finding: Finding) -> None:
    result.findings.append(finding)


def audit_catalog(
    types: list[dict[str, Any]],
    policy: dict[str, Any],
    strict: str = "core",
    mcp_text: dict[str, str] | None = None,
) -> AuditResult:
    result = AuditResult()
    core_names = scan_core_operator_names()
    allowed_missing_docs = set(policy.get("allowed_missing_docs", []))
    legacy_operators = set(policy.get("legacy_operators", []))
    replacements = dict(policy.get("preferred_replacements", {}))
    capability_expectations = dict(policy.get("capability_expectations", {}))
    mcp_text = mcp_text if mcp_text is not None else scan_mcp_text()

    by_name = {op.get("name", ""): op for op in types}
    for op in types:
        name = op.get("name", "")
        classification = classify_operator(op, core_names)
        checked = {
            "name": name,
            "kind": op.get("kind", ""),
            "display_name": op.get("display_name", ""),
            "classification": classification,
            "has_docs": bool(op.get("has_docs")),
            "has_discovery_text": has_discovery_docs(op),
        }
        result.operators.append(checked)

        severity = "error" if strict == "core" and classification == "core" else "warning"
        if not name:
            add(result, Finding(severity, "missing_name", name, "Operator entry has no name"))
        if not op.get("display_name"):
            add(result, Finding(severity, "missing_display_name", name, "Missing display_name"))
        if op.get("kind") not in VALID_KINDS:
            add(result, Finding(severity, "invalid_kind", name, f"Invalid kind {op.get('kind')!r}"))
        if not has_discovery_docs(op) and name not in allowed_missing_docs:
            add(
                result,
                Finding(
                    severity,
                    "missing_discovery_docs",
                    name,
                    "Missing has_docs/brief/summary discovery metadata",
                ),
            )

    for legacy, preferred in replacements.items():
        if legacy in by_name and preferred and preferred not in by_name and strict == "core":
            add(
                result,
                Finding(
                    "error",
                    "missing_preferred_replacement",
                    preferred,
                    f"Preferred replacement for {legacy} is not in list-types",
                ),
            )

    for capability, expectation in capability_expectations.items():
        preferred = expectation.get("preferred")
        if preferred and preferred not in by_name:
            add(
                result,
                Finding(
                    "error" if strict == "core" else "warning",
                    "missing_capability_preferred",
                    preferred,
                    f"Capability {capability} expects preferred operator {preferred}",
                ),
            )
        terms = expectation.get("mcp_terms", [])
        if terms and not any(term in text for term in terms for text in mcp_text.values()):
            add(
                result,
                Finding(
                    "error" if strict == "core" else "warning",
                    "missing_capability_mcp_term",
                    preferred or capability,
                    f"MCP text does not mention expected terms for {capability}: {terms}",
                ),
            )

    for path, text in mcp_text.items():
        lines = text.splitlines()
        for legacy in legacy_operators:
            for idx, line in enumerate(lines, start=1):
                context = "\n".join(lines[max(0, idx - 2):min(len(lines), idx + 1)])
                if legacy in line and not text_has_legacy_context(context):
                    add(
                        result,
                        Finding(
                            "error" if strict == "core" else "warning",
                            "legacy_reference_without_context",
                            legacy,
                            f"Legacy operator reference lacks legacy/compatibility context at line {idx}",
                            path,
                        ),
                    )
        for legacy, preferred in replacements.items():
            for idx, line in enumerate(lines, start=1):
                context = "\n".join(lines[max(0, idx - 2):min(len(lines), idx + 1)])
                if (legacy in line and preferred and preferred not in context and
                        not text_has_legacy_context(context)):
                    add(
                        result,
                        Finding(
                            "error" if strict == "core" else "warning",
                            "preferred_replacement_not_named",
                            legacy,
                            f"Reference should name preferred replacement {preferred} at line {idx}",
                            path,
                        ),
                    )

    errors = sum(1 for f in result.findings if f.severity == "error")
    warnings = sum(1 for f in result.findings if f.severity == "warning")
    result.summary = {
        "operators": len(types),
        "core": sum(1 for op in result.operators if op["classification"] == "core"),
        "advisory": sum(1 for op in result.operators if op["classification"] == "advisory"),
        "modules": sum(1 for op in result.operators if op["classification"] == "module"),
        "errors": errors,
        "warnings": warnings,
    }
    result.ok = errors == 0
    return result


def to_jsonable(result: AuditResult) -> dict[str, Any]:
    return {
        "ok": result.ok,
        "summary": result.summary,
        "findings": [f.__dict__ for f in result.findings],
        "operators": result.operators,
    }


def render_markdown(result: AuditResult) -> str:
    lines = [
        "# MCP Operator Coverage Audit",
        "",
        f"- Operators: {result.summary.get('operators', 0)}",
        f"- Core strict targets: {result.summary.get('core', 0)}",
        f"- Advisory/package targets: {result.summary.get('advisory', 0)}",
        f"- Modules: {result.summary.get('modules', 0)}",
        f"- Errors: {result.summary.get('errors', 0)}",
        f"- Warnings: {result.summary.get('warnings', 0)}",
        "",
    ]
    if not result.findings:
        lines.append("No coverage gaps found.")
        return "\n".join(lines) + "\n"

    lines.append("| Severity | Code | Operator | Message | Path |")
    lines.append("|---|---|---|---|---|")
    for f in result.findings:
        path = f.path.replace("|", "\\|")
        message = f.message.replace("|", "\\|")
        lines.append(f"| {f.severity} | {f.code} | `{f.operator}` | {message} | {path} |")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vivid", default=str(REPO_ROOT / "build" / "vivid"))
    parser.add_argument("--catalog-json", type=pathlib.Path)
    parser.add_argument("--policy", type=pathlib.Path, default=DEFAULT_POLICY)
    parser.add_argument("--strict", choices=("core", "none"), default="core")
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    parser.add_argument("--markdown-output", type=pathlib.Path)
    parser.add_argument("--json-output", type=pathlib.Path)
    args = parser.parse_args(argv)

    try:
        policy = load_json(args.policy)
        types = load_catalog(args.catalog_json, pathlib.Path(args.vivid))
        result = audit_catalog(types, policy, strict=args.strict)
    except Exception as exc:  # noqa: BLE001
        print(f"mcp operator coverage audit failed: {exc}", file=sys.stderr)
        return 2

    json_data = to_jsonable(result)
    markdown = render_markdown(result)
    if args.json_output:
        args.json_output.write_text(json.dumps(json_data, indent=2) + "\n", encoding="utf-8")
    if args.markdown_output:
        args.markdown_output.write_text(markdown, encoding="utf-8")

    if args.format == "json":
        print(json.dumps(json_data, indent=2))
    else:
        print(markdown, end="")
    return 0 if result.ok or args.strict == "none" else 1


if __name__ == "__main__":
    raise SystemExit(main())
