#!/usr/bin/env python3
"""Launch-time sidecar: (re)embed a specific set of operators and produce a
2D layout that is jointly fit with the existing bundled core layout.

The C++ runtime invokes this when the active operator catalog contains
operators that were not present in the bundled operator_embeddings.json (for
example, after a package was installed). It is not called on every launch —
only when the content-hash set has changed.

Protocol (JSON, via stdin and stdout):

    Input:
        {
          "bundled": <contents of Resources/operator_embeddings.json>,
          "operators": [
            {"name": "...", "kind": "gpu|audio|control", "text": "..."}
          ]
        }

    Output:
        {
          "version": 1,
          "model": "...",
          "umap": {...},
          "operators": [ { "name", "kind", "hash", "xy", "embedding" }, ... ]
        }

The output is the full joint layout — bundled core plus newly embedded ops —
which the runtime writes to `~/.vivid/cache/operator_layout.json` keyed by a
hash of the active operator set.
"""

from __future__ import annotations

import json
import sys
import time

from operator_embeddings_shared import (
    EMBEDDING_MODEL,
    UMAP_PARAMS,
    align_layout,
    content_hash,
    embed_texts,
    normalize_layout,
    reduce_to_2d,
)


def main() -> int:
    raw = sys.stdin.read()
    if not raw.strip():
        print(json.dumps({"ok": False, "error": "empty stdin"}))
        return 2

    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        print(json.dumps({"ok": False, "error": f"bad json: {exc}"}))
        return 2

    bundled = payload.get("bundled") or {}
    bundled_ops = bundled.get("operators") or []
    bundled_by_name = {o["name"]: o for o in bundled_ops}

    new_ops = payload.get("operators") or []

    # Anything the caller asked about that already exists in bundled with the
    # same content hash is copied through untouched. Everything else gets
    # re-embedded along with the full bundled set so UMAP has a stable frame
    # of reference.
    records: list[dict] = []
    seen: set[str] = set()
    for op in new_ops:
        name = op.get("name")
        if not name:
            continue
        text = op.get("text", "")
        h = content_hash(text)
        records.append(
            {"name": name, "kind": op.get("kind", ""), "text": text, "hash": h}
        )
        seen.add(name)

    for op in bundled_ops:
        if op["name"] in seen:
            continue
        # Bundled text isn't in the JSON (we dropped it to keep the file small)
        # so we can't re-derive its hash. Reuse the bundled embedding instead.
        records.append(
            {
                "name": op["name"],
                "kind": op.get("kind", ""),
                "text": None,
                "hash": op.get("hash", ""),
                "_reuse": op,
            }
        )

    t0 = time.time()
    to_embed_idx = [i for i, r in enumerate(records) if r.get("text") is not None]
    cached_idx = [i for i, r in enumerate(records) if r.get("text") is None]

    import numpy as np

    width = 384
    for i in cached_idx:
        vec = records[i]["_reuse"].get("embedding") or []
        if vec:
            width = len(vec)
            break

    all_vecs = np.zeros((len(records), width))
    for i in cached_idx:
        vec = records[i]["_reuse"].get("embedding") or []
        all_vecs[i, : len(vec)] = vec

    if to_embed_idx:
        texts = [records[i]["text"] for i in to_embed_idx]
        new_vecs = embed_texts(texts)
        for k, i in enumerate(to_embed_idx):
            all_vecs[i] = new_vecs[k]

    xy = reduce_to_2d(all_vecs)
    kinds = [r.get("kind", "") for r in records]
    xy = align_layout(xy, kinds)
    xy = normalize_layout(xy)

    out_ops = []
    for i, r in enumerate(records):
        out_ops.append(
            {
                "name": r["name"],
                "kind": r.get("kind", ""),
                "hash": r["hash"],
                "xy": [float(xy[i, 0]), float(xy[i, 1])],
                "embedding": [float(v) for v in all_vecs[i]],
            }
        )

    out = {
        "version": 1,
        "model": EMBEDDING_MODEL,
        "umap": UMAP_PARAMS,
        "operators": out_ops,
        "elapsed_ms": int((time.time() - t0) * 1000),
        "ok": True,
    }
    json.dump(out, sys.stdout)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
