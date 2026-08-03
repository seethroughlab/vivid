#!/usr/bin/env python3
"""Generate the Operator Reference snapshot from Vivid 4's live metadata (ADR-0038).

Launches a CLEAN app-ON instance (user/project operator + shader tiers redirected to empty temp
dirs, so only the bundled core operators + shaders + compiled-in audio ops register), dumps the
unified operator catalog over the control server, and writes a checked-in `site/reference.json` that
`site/build.py` renders into the Reference pages. Run this whenever the operator set changes:

    uv run --project site site/generate_reference.py            # uses the dev build by default
    uv run --project site site/generate_reference.py --app /Applications/vivid.app/Contents/MacOS/vivid

The reference is generated, never hand-written — the site build consumes the snapshot with no app.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
DEFAULT_APP = REPO / "app" / "build" / "vivid.app" / "Contents" / "MacOS" / "vivid"
OUT = HERE / "reference.json"

# Formats that belong to Vivid's own core operators: dylib visual ops (no format key), bundled shader
# operators, and compiled-in native audio ops. Everything else (VST3/CLAP/AU plugins installed on the
# machine, project compiled_operator) is NOT a Vivid operator and is excluded from the reference.
CORE_FORMATS = {None, "shader_file", "native"}


def post(base: str, method: str, **payload) -> dict:
    data = json.dumps(payload).encode()
    req = urllib.request.Request(f"{base}/{method}", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        res = json.loads(r.read())
    if not res.get("ok", False):
        raise RuntimeError(f"{method} failed: {res.get('code')} {res.get('error')}")
    return res


def wait_for_server(base: str, tries: int = 90) -> None:
    for _ in range(tries):
        try:
            post(base, "status")
            return
        except Exception:
            time.sleep(1)
    raise SystemExit(f"control server did not come up at {base}")


def slugify(v: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", v.lower()).strip("-")


def bucket(entry: dict) -> str:
    """Map Vivid 4's (domain, kind) onto three reference buckets: visual / audio / control."""
    d, k = entry.get("domain"), entry.get("kind", "")
    if d == "visual":
        return "visual"
    if d == "audio":
        return "control" if k in ("modulator", "note_effect", "generator") else "audio"
    return d or "other"


def is_core(entry: dict) -> bool:
    if entry.get("format") not in CORE_FORMATS:
        return False  # third-party plugin (VST3/CLAP/AU) or project compiled_operator
    tier = (entry.get("source") or {}).get("tier")
    return tier in (None, "bundled")  # drop stale user/project-tier ops


def transform(catalog: list[dict]) -> list[dict]:
    ops = []
    seen = set()
    for e in catalog:
        if not is_core(e):
            continue
        name = e.get("name") or ""
        if not name or name in seen:
            continue
        seen.add(name)
        ops.append({
            "name": name,
            "display_name": e.get("display_name") or name,
            "slug": slugify(name),
            "domain": bucket(e),
            "kind": e.get("kind", ""),
            "role": e.get("role", ""),  # ADR-0046: composable-primitive vs recipe classification
            "summary": e.get("summary", ""),
            "keywords": e.get("keywords", []),
            "params": e.get("params", []),
            "ports": e.get("ports", []),
            "source": e.get("source"),
        })
    ops.sort(key=lambda o: o["name"].lower())
    return ops


def dump_catalog(base: str) -> dict:
    version = post(base, "get_version")
    catalog = post(base, "list_operator_catalog", domain="all", kind="all", detail="full")
    shaders = post(base, "list_shaders")
    ops = transform(catalog.get("operators", []))

    present = [d for d in ("visual", "audio", "control") if any(o["domain"] == d for o in ops)]
    shader_errors = [
        {"name": s.get("name", ""), "path": s.get("path", ""), "error": s.get("error", "")}
        for s in shaders.get("shaders", []) if s.get("error")
    ]
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "app_version": version.get("app_version"),
        "build_type": version.get("build_type"),
        "operator_abi": version.get("operator_abi"),
        "count": len(ops),
        "domains": present,
        "operators": ops,
        "shader_errors": shader_errors,
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Generate the Vivid 4 operator reference snapshot")
    ap.add_argument("--app", type=Path, default=DEFAULT_APP, help="path to the vivid binary")
    ap.add_argument("--port", type=int, default=9897, help="control-server port for the clean instance")
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--attach", action="store_true",
                    help="attach to an already-running app on --port instead of launching a clean one "
                         "(may include stale user/project ops)")
    args = ap.parse_args(argv)
    base = f"http://127.0.0.1:{args.port}"

    if args.attach:
        wait_for_server(base, tries=3)
        snapshot = dump_catalog(base)
    else:
        if not args.app.exists():
            raise SystemExit(f"app binary not found: {args.app} (build it, or pass --app)")
        empty_ops = tempfile.mkdtemp(prefix="vivid_ref_ops_")
        empty_shaders = tempfile.mkdtemp(prefix="vivid_ref_shaders_")
        env = {**os.environ, "VIVID_PORT": str(args.port), "VIVID_DISCARD_RECOVERY": "1",
               "VIVID_OPERATORS_DIR": empty_ops, "VIVID_SHADERS_DIR": empty_shaders}
        print(f"[reference] launching clean app instance on :{args.port} …")
        proc = subprocess.Popen([str(args.app)], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_for_server(base)
            snapshot = dump_catalog(base)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()

    args.out.write_text(json.dumps(snapshot, indent=2) + "\n")
    by_domain = {d: sum(1 for o in snapshot["operators"] if o["domain"] == d) for d in snapshot["domains"]}
    print(f"[reference] wrote {snapshot['count']} operators -> {args.out}  "
          f"(app {snapshot['app_version']}, abi {snapshot['operator_abi']}; {by_domain})")
    if snapshot["shader_errors"]:
        print(f"[reference] note: {len(snapshot['shader_errors'])} shader(s) failed to parse")
    return 0


if __name__ == "__main__":
    sys.exit(main())
