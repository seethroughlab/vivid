#!/usr/bin/env python3
"""Generate the bundled operator embeddings JSON.

Run this manually (or via a cmake target) when the core operator set changes
in a way that should shift the Map-tab layout. The output is committed to the
repo and shipped in the app bundle's Resources/.

    uv run --with sentence-transformers --with umap-learn \\
        tools/generate_operator_embeddings.py

If the runtime needs to handle packages that were installed after this file
was generated, `embed_operators.py` covers that case at launch time using the
same helpers.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

from operator_embeddings_shared import (
    EMBEDDING_MODEL,
    REPO_ROOT,
    UMAP_PARAMS,
    build_layout,
    collect_operator_records,
    fetch_operator_list,
)

DEFAULT_OUTPUT = REPO_ROOT / "resources" / "operator_embeddings.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=DEFAULT_OUTPUT,
        help=f"Output JSON path (default: {DEFAULT_OUTPUT.relative_to(REPO_ROOT)})",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Limit to first N operators (debug).",
    )
    args = parser.parse_args()

    print(f"[embeddings] fetching operator catalog...", file=sys.stderr)
    if args.limit:
        entries = fetch_operator_list()
        names = sorted({e["type"] for e in entries if e.get("type")})[: args.limit]
        records = collect_operator_records(names)
    else:
        records = collect_operator_records()
    print(f"[embeddings] collected {len(records)} operators", file=sys.stderr)

    t0 = time.time()
    entries = build_layout(records)
    print(
        f"[embeddings] embed+umap took {time.time() - t0:.1f}s",
        file=sys.stderr,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": 1,
        "model": EMBEDDING_MODEL,
        "umap": UMAP_PARAMS,
        "operators": entries,
    }
    args.output.write_text(json.dumps(payload, indent=2) + "\n")
    try:
        display = args.output.relative_to(REPO_ROOT)
    except ValueError:
        display = args.output
    print(
        f"[embeddings] wrote {display} ({len(entries)} operators)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
