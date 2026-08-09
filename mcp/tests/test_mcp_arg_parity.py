#!/usr/bin/env python3
"""MCP <-> control-server ARGUMENT-shape parity guard (static; no running app).

`test_mcp_parity.py` guards METHOD-name parity: every control handler has an MCP tool and
vice-versa. It does NOT look at arguments, so a rename on one side slips through — e.g. the
handler starts reading `b.value("operator")` while the MCP tool keeps posting `{"op": ...}`.
The method still exists on both sides, name-parity stays green, and the argument silently
falls back to its default. The feature just quietly stops working.

This guard closes that gap in the high-confidence direction: **a key the MCP bridge sends that
the control handler never reads**. That is always a bug (dead/renamed arg). The reverse
(handler reads a key the bridge omits) is NOT failed — handlers use `b.value("k", default)`, so
an omitted optional key is legitimate; those are printed as INFO only.

How the two sides are parsed:
- Bridge (`mcp/vivid_mcp.py`): via `ast`, so multi-line dict payloads parse exactly. A `_post`
  whose payload is not a plain string-keyed dict literal (computed dict, **spread, a variable)
  cannot be analyzed statically and is SKIPPED — and counted/reported, never silently dropped.
- Handlers (`app/src/cli/**`): regex for `<body>.value("k")` / `.contains("k")` / `["k"]`,
  scoped to each `handlers_["m"] = [...]{...}` lambda. Because a few handlers delegate body
  parsing to shared helpers (`copy_live_capture`, `resolve_frame`, `track_index_from_request`,
  …) — some defined in OTHER files — every free function taking a `json&` param is parsed for
  the keys it reads, and a helper's keys are unioned into any handler that calls it. This is
  generic (no hardcoded helper key lists), so new helpers are covered automatically.

Run standalone (`uv run mcp/tests/test_mcp_arg_parity.py`) or under pytest.
"""
import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI_DIR = ROOT / "app" / "src" / "cli"
BRIDGE = ROOT / "mcp" / "vivid_mcp.py"
# Handler families + control_server.cpp hold the `handlers_["m"] = [...]` lambdas. Helper
# functions that parse a request body can live in any cli translation unit, so scan them all.
HANDLER_FILES = [CLI_DIR / "control_server.cpp"] + sorted(CLI_DIR.glob("control_handlers_*.cpp"))
CLI_SOURCES = sorted(CLI_DIR.glob("*.cpp"))

# (method, key) pairs the guard should not fail on. Two legitimate reasons, and the reason string
# must say which: (A) the key is consumed by machinery the static parser can't follow, or (B) a
# REAL known gap the tool advertises but the handler ignores, kept here (not silently) until it is
# resolved. Anything without an entry fails — so genuine new drift can't hide.
ARG_PARITY_ALLOW: dict[tuple[str, str], str] = {
    ("inspect_scene", "detail"): (
        "REAL GAP (not a parser artifact): the inspect_scene MCP tool exposes detail= but the "
        "handler (control_handlers_introspection.cpp) never reads it — the knob is a no-op. "
        "Pending a decision: honor detail (mirror inspect_bindings' summary/full split) or drop "
        "the param from the tool. Remove this entry once fixed."
    ),
}

# Free functions that a handler lambda delegates request-body parsing to (they take the whole
# `json&` body and read keys off it — some are defined in OTHER translation units). Their keys
# are still parsed from source (so a helper reading a NEW key is picked up automatically); only
# this NAME set is curated, because a generic "any json&-taking function" match also snags C++
# keywords and locals (`for`, a `value` lambda, …) and over-credits handlers. Add a name here
# only when a handler genuinely routes its body through it.
BODY_HELPERS = {
    "copy_live_capture",        # audio_analysis_tools.cpp — reads source/seconds/… off the body
    "resolve_audio_source",     # audio_analysis_tools.cpp
    "resolve_frame",            # control_handlers_visual_analysis.cpp — frame spec keys
    "track_index_from_request", # control_handlers_mappings.cpp — reads "track"
}

# Match a JSON key read off a json variable named `<var>`: var.value("k" | var.contains("k" |
# var["k"]. The negative lookbehind keeps a short var like `b` from matching inside `numb`/`rb`.
# `var` is captured per-scope (a lambda's body param, or a helper's json param name).
_KEY_ACCESS = r'(?<![A-Za-z0-9_]){var}\s*(?:\.\s*(?:value|contains|at|count)\s*\(\s*"([a-z0-9_]+)"|\[\s*"([a-z0-9_]+)"\s*\])'


def _keys_for_var(text: str, var: str) -> set[str]:
    keys: set[str] = set()
    for m in re.finditer(_KEY_ACCESS.format(var=re.escape(var)), text):
        keys.add(m.group(1) or m.group(2))
    return keys


def _brace_body(text: str, open_idx: int) -> str:
    """Return the substring from the `{` at/after open_idx through its matching `}`."""
    i = text.find("{", open_idx)
    if i < 0:
        return ""
    depth, j = 0, i
    while j < len(text):
        ch = text[j]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[i : j + 1]
        j += 1
    return text[i:]


# A free function definition that takes a `const json& <param>` (or `json& <param>`). Captures
# the function name, the json param's name, and the position to start brace-matching its body.
_FUNC_DEF = re.compile(
    r'\b([a-z_][a-z0-9_]*)\s*\([^;{}]*?\bjson&\s*([a-z_][a-z0-9_]*)[^;{}]*?\)\s*(?:const\s*)?\{'
)


def json_helpers() -> dict[str, set[str]]:
    """name -> set of body keys the helper reads off its json param."""
    helpers: dict[str, set[str]] = {}
    for f in CLI_SOURCES:
        text = f.read_text()
        for m in _FUNC_DEF.finditer(text):
            name, param = m.group(1), m.group(2)
            body = _brace_body(text, m.end() - 1)
            keys = _keys_for_var(body, param)
            if keys:
                helpers[name] = helpers.get(name, set()) | keys
    return helpers


def handler_arg_keys() -> dict[str, set[str]]:
    """control method -> set of request keys it reads (its lambda body + any helper it calls)."""
    helpers = {n: k for n, k in json_helpers().items() if n in BODY_HELPERS}
    helper_names = sorted(helpers, key=len, reverse=True)
    out: dict[str, set[str]] = {}
    for f in HANDLER_FILES:
        text = f.read_text()
        # Split into per-handler regions: each starts at `handlers_["m"]` and runs to the next.
        starts = [(m.start(), m.group(1)) for m in re.finditer(r'handlers_\[\s*"([a-z0-9_]+)"\s*\]', text)]
        for idx, (pos, method) in enumerate(starts):
            end = starts[idx + 1][0] if idx + 1 < len(starts) else len(text)
            region = text[pos:end]
            # The lambda's body param name (usually `b`); default to `b` if the header is terse.
            hm = re.search(r'const\s+json&\s*([a-z_][a-z0-9_]*)', region)
            var = hm.group(1) if hm else "b"
            keys = _keys_for_var(region, var)
            for hn in helper_names:
                if re.search(rf'\b{re.escape(hn)}\s*\(', region):
                    keys |= helpers[hn]
            out[method] = out.get(method, set()) | keys
    # Resolve handler aliases: `handlers_["x"] = handlers_["y"];` gives x the keys of y. Iterate
    # to a fixpoint so a chain (x->y->z) fully resolves regardless of definition order.
    aliases = [
        (m.group(1), m.group(2))
        for f in HANDLER_FILES
        for m in re.finditer(
            r'handlers_\[\s*"([a-z0-9_]+)"\s*\]\s*=\s*handlers_\[\s*"([a-z0-9_]+)"\s*\]',
            f.read_text(),
        )
    ]
    for _ in range(len(aliases) + 1):
        changed = False
        for dst, src in aliases:
            merged = out.get(dst, set()) | out.get(src, set())
            if merged != out.get(dst, set()):
                out[dst] = merged
                changed = True
        if not changed:
            break
    return out


def bridge_payloads() -> tuple[dict[str, set[str]], list[str]]:
    """method -> set of literal payload keys; plus a list of methods whose payload was non-literal."""
    tree = ast.parse(BRIDGE.read_text())
    payloads: dict[str, set[str]] = {}
    skipped: list[str] = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "_post"):
            continue
        if not node.args or not isinstance(node.args[0], ast.Constant) or not isinstance(node.args[0].value, str):
            continue
        method = node.args[0].value
        if len(node.args) < 2:
            payloads.setdefault(method, set())  # no-arg tool, empty payload
            continue
        payload = node.args[1]
        if isinstance(payload, ast.Dict) and all(
            isinstance(k, ast.Constant) and isinstance(k.value, str) for k in payload.keys
        ) and None not in payload.keys:  # ast.Dict key is None for **spread entries
            payloads[method] = payloads.get(method, set()) | {k.value for k in payload.keys}
        else:
            skipped.append(method)
    return payloads, skipped


def check() -> tuple[dict[str, set[str]], dict[str, set[str]], list[str]]:
    """Returns (violations, info_missing, skipped)."""
    bridge, skipped = bridge_payloads()
    handlers = handler_arg_keys()
    violations: dict[str, set[str]] = {}   # bridge sends -> handler never reads (FAIL)
    info_missing: dict[str, set[str]] = {}  # handler reads -> bridge never sends (INFO only)
    for method, sent in bridge.items():
        if method not in handlers:
            continue  # method-name parity is the other guard's job
        read = handlers[method]
        unread = {k for k in (sent - read) if (method, k) not in ARG_PARITY_ALLOW}
        if unread:
            violations[method] = unread
        missing = read - sent
        if missing:
            info_missing[method] = missing
    return violations, info_missing, skipped


def test_mcp_arg_parity():
    handlers = handler_arg_keys()
    bridge, _ = bridge_payloads()
    assert handlers, "parsed zero control handlers — regex/paths out of date"
    assert bridge, "parsed zero bridge payloads — ast walk/paths out of date"
    violations, _info, _skipped = check()
    assert not violations, (
        "MCP tools sending arguments the control handler never reads (renamed/dead key — the "
        "arg silently defaults). Fix the mismatch, or add (method, key) to ARG_PARITY_ALLOW with "
        "a reason:\n  " + "\n  ".join(f"{m}: {sorted(ks)}" for m, ks in sorted(violations.items()))
    )


if __name__ == "__main__":
    handlers = handler_arg_keys()
    bridge, skipped = bridge_payloads()
    violations, info_missing, _ = check()
    analyzable = sum(1 for m in bridge if m in handlers)
    print(
        f"bridge tools with literal payloads: {len(bridge)} | control handlers: {len(handlers)} | "
        f"arg-analyzable (on both sides): {analyzable} | "
        f"skipped (computed payload): {len(set(skipped))} | allowlisted: {len(ARG_PARITY_ALLOW)}"
    )
    if skipped:
        print(f"  computed-payload tools (arg-shape not checked): {sorted(set(skipped))}")
    if info_missing:
        print("  INFO (handler reads a key the bridge omits — legitimate if optional):")
        for m, ks in sorted(info_missing.items()):
            print(f"    {m}: {sorted(ks)}")
    if violations:
        print("\nFAIL — MCP tools sending keys the handler never reads (silent arg drift):", file=sys.stderr)
        for m, ks in sorted(violations.items()):
            print(f"  {m}: {sorted(ks)}", file=sys.stderr)
        sys.exit(1)
    print("PASS — every argument the MCP bridge sends is read by its control handler")
