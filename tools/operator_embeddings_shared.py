"""Shared helpers for operator embedding pipelines.

Used by two entry points:
- `generate_operator_embeddings.py` — build-time generator for core operators,
  produces the JSON bundled in Resources/.
- `embed_operators.py` — launch-time sidecar the C++ runtime invokes when it
  encounters operators it has never embedded before (e.g. from a newly installed
  package). Reads a JSON payload on stdin, writes results on stdout.

Keep this file free of any import-time side effects that touch the heavy ML
stack so the module is cheap to probe from tests.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
from typing import Any

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_VIVID_BIN = REPO_ROOT / "build" / "vivid"

EMBEDDING_MODEL = "sentence-transformers/all-MiniLM-L6-v2"
EMBEDDING_DIM = 384

UMAP_PARAMS = {
    "n_components": 2,
    "metric": "cosine",
    "n_neighbors": 15,
    "min_dist": 0.1,
    "random_state": 42,
}


def resolve_vivid_bin() -> pathlib.Path:
    import os

    env_bin = os.environ.get("VIVID_BIN")
    if env_bin:
        candidate = pathlib.Path(env_bin).expanduser()
        if candidate.exists():
            return candidate.resolve()
    if DEFAULT_VIVID_BIN.exists():
        return DEFAULT_VIVID_BIN.resolve()
    raise FileNotFoundError(
        "no vivid binary found; set VIVID_BIN or build ./build/vivid"
    )


def run_cli_json(args: list[str], timeout: float = 20.0) -> Any:
    vivid = resolve_vivid_bin()
    try:
        result = subprocess.run(
            [str(vivid), *args],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(
            f"vivid {' '.join(args)} timed out after {timeout}s"
        ) from exc
    if not result.stdout.strip():
        raise RuntimeError(
            f"vivid {' '.join(args)} produced no stdout "
            f"(exit={result.returncode}): {result.stderr[:300]}"
        )
    payload = json.loads(result.stdout)
    if not payload.get("ok", False):
        raise RuntimeError(
            f"vivid {' '.join(args)} returned ok=false: "
            f"{payload.get('error', payload)}"
        )
    return payload["result"]


def fetch_operator_list() -> list[dict]:
    entries = run_cli_json(["operator-map", "--json"])
    return entries


def fetch_operator_docs(name: str) -> dict:
    return run_cli_json(["operator-docs", name, "--json"])


def _fmt_port(p: dict) -> str:
    parts = [p.get("name", "?")]
    tag = p.get("semantic_tag")
    if tag:
        parts.append(f"[{tag}]")
    t = p.get("type") or p.get("transport")
    if t:
        parts.append(f"({t})")
    desc = p.get("description") or ""
    return " ".join(parts) + (f": {desc}" if desc else "")


def _fmt_param(p: dict) -> str:
    parts = [p.get("name", "?")]
    tag = p.get("semantic_tag")
    if tag:
        parts.append(f"[{tag}]")
    unit = p.get("semantic_unit")
    if unit:
        parts.append(f"<{unit}>")
    desc = p.get("description") or ""
    line = " ".join(parts)
    if desc:
        line += f": {desc}"
    choices = p.get("choices")
    if choices:
        line += f" — choices: {', '.join(str(c) for c in choices)}"
    return line


def build_semantic_text(docs: dict) -> str:
    """Flatten an operator-docs record into one structured text blob.

    This is the string fed to the embedding model. Deterministic so the
    content hash can be used as a cache key.
    """
    lines: list[str] = []
    lines.append(f"Operator: {docs.get('name', '')}")
    kind = docs.get("kind", "")
    if kind:
        lines.append(f"Domain: {kind}")
    lane = docs.get("multiplicity_behavior", "")
    if lane:
        lines.append(f"Lane behavior: {lane}")

    brief = (docs.get("brief") or "").strip()
    if brief:
        lines.append(f"Brief: {brief}")

    body = (docs.get("body") or "").strip()
    if body:
        lines.append(f"Description: {body}")

    related = docs.get("related") or []
    if related:
        lines.append(f"Related operators: {', '.join(related)}")

    for label, key in (
        ("Best used with", "best_used_with"),
        ("Common companions", "common_companions"),
        ("Tips", "tips"),
        ("Recipes", "recipes"),
        ("Pitfalls", "pitfalls"),
    ):
        values = docs.get(key) or []
        if values:
            joined = "; ".join(str(v) for v in values)
            lines.append(f"{label}: {joined}")

    params = docs.get("params") or []
    if params:
        lines.append("Parameters:")
        for p in params:
            lines.append(f"  - {_fmt_param(p)}")

    inputs = docs.get("inputs") or []
    if inputs:
        lines.append("Input ports:")
        for p in inputs:
            lines.append(f"  - {_fmt_port(p)}")

    outputs = docs.get("outputs") or []
    if outputs:
        lines.append("Output ports:")
        for p in outputs:
            lines.append(f"  - {_fmt_port(p)}")

    return "\n".join(lines)


def content_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def _collect_one(name: str) -> dict | None:
    try:
        docs = fetch_operator_docs(name)
    except Exception as exc:
        print(f"[warn] {name}: docs fetch failed: {exc}", file=sys.stderr)
        return None
    text = build_semantic_text(docs)
    return {
        "name": docs.get("name", name),
        "kind": docs.get("kind", ""),
        "text": text,
        "hash": content_hash(text),
        "brief": (docs.get("brief") or "").strip(),
        "related": list(docs.get("related") or []),
        "multiplicity_behavior": docs.get("multiplicity_behavior", ""),
        "num_inputs": len(docs.get("inputs") or []),
        "num_outputs": len(docs.get("outputs") or []),
    }


def collect_operator_records(names: list[str] | None = None,
                             workers: int = 8) -> list[dict]:
    """Return [{name, kind, text, hash}] for the requested operators.

    If names is None, fetches the full runtime catalog. Silently skips
    operators that fail to resolve docs (newly added without metadata yet).
    The CLI is spawned in parallel (`vivid operator-docs` is independent per
    call), which cuts wall time roughly Nx for N workers up to disk/cpu limits.
    """
    if names is None:
        entries = fetch_operator_list()
        names = sorted({e["type"] for e in entries if e.get("type")})

    if workers <= 1:
        records: list[dict | None] = []
        for i, name in enumerate(names):
            if i % 20 == 0:
                print(f"[embeddings] {i}/{len(names)}: {name}", file=sys.stderr)
            records.append(_collect_one(name))
        return [r for r in records if r]

    from concurrent.futures import ThreadPoolExecutor, as_completed

    results: dict[str, dict] = {}
    total = len(names)
    done = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(_collect_one, n): n for n in names}
        for fut in as_completed(futures):
            done += 1
            if done % 20 == 0 or done == total:
                print(f"[embeddings] {done}/{total}", file=sys.stderr)
            rec = fut.result()
            if rec:
                results[futures[fut]] = rec
    # Preserve input order for stable UMAP output.
    return [results[n] for n in names if n in results]


def embed_texts(texts: list[str]):
    """Return an (n, 384) numpy array of sentence-transformer embeddings."""
    from sentence_transformers import SentenceTransformer  # type: ignore

    model = SentenceTransformer(EMBEDDING_MODEL)
    vecs = model.encode(
        texts,
        convert_to_numpy=True,
        normalize_embeddings=True,
        show_progress_bar=False,
    )
    return vecs


def reduce_to_2d(vecs):
    """Run UMAP to collapse (n, d) → (n, 2)."""
    import umap  # type: ignore

    reducer = umap.UMAP(**UMAP_PARAMS)
    return reducer.fit_transform(vecs)


def _kind_axis(kinds: list[str]) -> list[float]:
    order = {"audio": -1.0, "control": 0.0, "gpu": 1.0}
    return [order.get(k, 0.0) for k in kinds]


def align_layout(xy, kinds: list[str], io_balance: list[float] | None = None):
    """Rotate (and optionally flip) the 2D layout so its axes carry meaning.

    UMAP output is rotation-invariant. Without this pass the layout feels
    arbitrary; with it, left/right has a loose meaning — audio on the left,
    GPU on the right — and top/bottom reflects source-vs-sink character
    (fewer inputs → top, more inputs → bottom), via a Y-sign flip chosen to
    match `io_balance` = (num_inputs - num_outputs) per operator.
    """
    import numpy as np

    xy = np.asarray(xy, dtype=float)
    if len(xy) < 3:
        return xy

    center = xy.mean(axis=0)
    centered = xy - center

    target = np.asarray(_kind_axis(kinds), dtype=float)
    target -= target.mean()
    if np.allclose(target, 0.0):
        return xy

    best_theta = 0.0
    best_score = -np.inf
    for deg in range(0, 360, 2):
        theta = np.deg2rad(deg)
        c, s = np.cos(theta), np.sin(theta)
        rot = centered @ np.array([[c, -s], [s, c]])
        score = float(np.corrcoef(rot[:, 0], target)[0, 1])
        if np.isnan(score):
            continue
        if score > best_score:
            best_score = score
            best_theta = theta

    c, s = np.cos(best_theta), np.sin(best_theta)
    rotated = centered @ np.array([[c, -s], [s, c]])

    if io_balance is not None:
        io = np.asarray(io_balance, dtype=float)
        io -= io.mean()
        if not np.allclose(io, 0.0):
            corr = float(np.corrcoef(rotated[:, 1], io)[0, 1])
            if not np.isnan(corr) and corr < 0.0:
                rotated[:, 1] = -rotated[:, 1]

    return rotated + center


def normalize_layout(xy):
    """Fit coords into the unit square [0, 1]^2 with a small margin."""
    import numpy as np

    xy = np.asarray(xy, dtype=float)
    if xy.size == 0:
        return xy
    mn = xy.min(axis=0)
    mx = xy.max(axis=0)
    span = np.maximum(mx - mn, 1e-6)
    out = (xy - mn) / span
    return 0.05 + 0.9 * out


def build_layout(records: list[dict]) -> list[dict]:
    """Full pipeline: records → embeddings → UMAP → aligned, normalized xy."""
    texts = [r["text"] for r in records]
    kinds = [r.get("kind", "") for r in records]
    io_balance = [
        float(r.get("num_inputs", 0) - r.get("num_outputs", 0))
        for r in records
    ]

    vecs = embed_texts(texts)
    xy = reduce_to_2d(vecs)
    xy = align_layout(xy, kinds, io_balance)
    xy = normalize_layout(xy)

    out = []
    for i, r in enumerate(records):
        out.append(
            {
                "name": r["name"],
                "kind": r.get("kind", ""),
                "hash": r["hash"],
                "xy": [float(xy[i, 0]), float(xy[i, 1])],
                "brief": r.get("brief", ""),
                "related": r.get("related", []),
                "multiplicity_behavior": r.get("multiplicity_behavior", ""),
                "num_inputs": int(r.get("num_inputs", 0)),
                "num_outputs": int(r.get("num_outputs", 0)),
                "embedding": [float(v) for v in vecs[i]],
            }
        )
    return out
