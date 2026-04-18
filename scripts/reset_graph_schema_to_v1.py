#!/usr/bin/env python3
"""One-shot graph-schema reset for the v0.1.0-alpha.1 baseline.

Walks one or more root directories for *.json files that have a top-level
`schema_version` field (i.e. Vivid graphs). For v3 files, applies the same
port/param renames the C++ loader used to do at src/runtime/graph/graph.cpp
(the v3->v4 migration for Composite/Mixer/Stack/Alternate). Then stamps
every processed file's schema_version to 1.

Idempotent: a second run produces no diff.
"""

from __future__ import annotations

import json
import pathlib
import sys
from typing import Iterable


# Mirrors src/runtime/graph/graph.cpp:368-416 exactly. Keys are the node
# `type` field in the JSON; values are (port_map, param_map) where each map
# is {old_name: new_name}.
V3_MIGRATIONS = {
    "Composite": (
        {
            "a": "layer_0", "b": "layer_1", "c": "layer_2",
            "d": "layer_3", "e": "layer_4", "f": "layer_5",
        },
        {
            "connected_a": "connected_0", "connected_b": "connected_1",
            "connected_c": "connected_2", "connected_d": "connected_3",
            "connected_e": "connected_4", "connected_f": "connected_5",
            "opacity_a": "opacity_0", "opacity_b": "opacity_1",
            "opacity_c": "opacity_2", "opacity_d": "opacity_3",
            "opacity_e": "opacity_4", "opacity_f": "opacity_5",
            "x_a": "x_0", "x_b": "x_1", "x_c": "x_2",
            "x_d": "x_3", "x_e": "x_4", "x_f": "x_5",
            "y_a": "y_0", "y_b": "y_1", "y_c": "y_2",
            "y_d": "y_3", "y_e": "y_4", "y_f": "y_5",
            "scale_a": "scale_0", "scale_b": "scale_1",
            "scale_c": "scale_2", "scale_d": "scale_3",
            "scale_e": "scale_4", "scale_f": "scale_5",
            "rotation_a": "rotation_0", "rotation_b": "rotation_1",
            "rotation_c": "rotation_2", "rotation_d": "rotation_3",
            "rotation_e": "rotation_4", "rotation_f": "rotation_5",
        },
    ),
    "Mixer": (
        {"input_1": "input_0", "input_2": "input_1",
         "input_3": "input_2", "input_4": "input_3"},
        {"gain_1": "gain_0", "gain_2": "gain_1",
         "gain_3": "gain_2", "gain_4": "gain_3"},
    ),
    "Stack": (
        {"a": "input_0", "b": "input_1", "c": "input_2", "d": "input_3"},
        {},
    ),
    "Alternate": (
        {"a": "input_0", "b": "input_1", "c": "input_2", "d": "input_3"},
        {},
    ),
}


def iter_graph_files(roots: Iterable[pathlib.Path]) -> Iterable[pathlib.Path]:
    for root in roots:
        if not root.exists():
            print(f"skip: {root} (does not exist)", file=sys.stderr)
            continue
        for path in root.rglob("*.json"):
            # Skip build artifacts and VCS metadata.
            parts = set(path.parts)
            if ".git" in parts or "build" in parts or "build-release" in parts:
                continue
            yield path


def apply_v3_migration(graph: dict) -> None:
    """Rewrite ports/params in-place for nodes of the four repeat-group types."""
    nodes = graph.get("nodes")
    if not isinstance(nodes, dict):
        return

    # Collect ids of nodes that need migration, grouped by operator type.
    targets: dict[str, set[str]] = {t: set() for t in V3_MIGRATIONS}
    for node_id, node in nodes.items():
        if not isinstance(node, dict):
            continue
        t = node.get("type")
        if t in V3_MIGRATIONS:
            targets[t].add(node_id)

    # Params (on the target node itself).
    for type_name, node_ids in targets.items():
        if not node_ids:
            continue
        _, param_map = V3_MIGRATIONS[type_name]
        if not param_map:
            continue
        for node_id in node_ids:
            node = nodes[node_id]
            params = node.get("params")
            if not isinstance(params, dict):
                continue
            renamed = {}
            for k, v in params.items():
                renamed[param_map.get(k, k)] = v
            node["params"] = renamed

    # Connection destination ports. Only `to` side gets remapped — these are
    # input ports on the target operator.
    conns = graph.get("connections")
    if isinstance(conns, list):
        # Flat lookup: which node ids are targets of any type? With matching
        # port map.
        port_map_by_node: dict[str, dict[str, str]] = {}
        for type_name, node_ids in targets.items():
            port_map, _ = V3_MIGRATIONS[type_name]
            for nid in node_ids:
                port_map_by_node[nid] = port_map
        for c in conns:
            if not isinstance(c, dict):
                continue
            to = c.get("to")
            if not isinstance(to, str) or "/" not in to:
                continue
            node_id, port = to.split("/", 1)
            pmap = port_map_by_node.get(node_id)
            if pmap is None:
                continue
            if port in pmap:
                c["to"] = f"{node_id}/{pmap[port]}"


def process_file(path: pathlib.Path) -> str:
    """Return 'unchanged' | 'migrated' | 'stamped' | 'skipped'."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as e:
        print(f"skip: {path}: {e}", file=sys.stderr)
        return "skipped"
    try:
        graph = json.loads(text)
    except json.JSONDecodeError:
        return "skipped"
    if not isinstance(graph, dict) or "schema_version" not in graph:
        return "skipped"

    original_version = graph.get("schema_version")
    migrated = False
    if original_version == 3:
        apply_v3_migration(graph)
        migrated = True

    # If the file was already v1 and no migration applied, leave it alone —
    # writing now would only normalize JSON formatting (escape style, key
    # order, trailing newlines) which is not the job of this reset.
    if original_version == 1 and not migrated:
        return "unchanged"

    # Stamp.
    graph["schema_version"] = 1

    # Serialize with 4-space indent (matches existing formatting in graphs/).
    # ensure_ascii=False preserves UTF-8 in translations / descriptions.
    new_text = json.dumps(graph, ensure_ascii=False, indent=4) + "\n"

    if new_text == text:
        return "unchanged"

    path.write_text(new_text, encoding="utf-8")
    return "migrated" if migrated else "stamped"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"usage: {argv[0]} <root> [<root> ...]", file=sys.stderr)
        return 2

    roots = [pathlib.Path(a).resolve() for a in argv[1:]]
    counts = {"unchanged": 0, "migrated": 0, "stamped": 0, "skipped": 0}
    for path in iter_graph_files(roots):
        result = process_file(path)
        counts[result] += 1
        if result in ("migrated", "stamped"):
            rel = path
            try:
                rel = path.relative_to(pathlib.Path.cwd())
            except ValueError:
                pass
            print(f"  {result:9s} {rel}")

    print()
    print(f"unchanged: {counts['unchanged']}  "
          f"stamped-only: {counts['stamped']}  "
          f"v3-migrated: {counts['migrated']}  "
          f"skipped (non-graph): {counts['skipped']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
