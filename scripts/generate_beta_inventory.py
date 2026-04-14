#!/usr/bin/env python3
"""Generate beta readiness inventory artifacts from the Vivid repo.

Scans graphs/, optional reference/fixture graph roots, operators/, and filters/ to
produce three markdown documents:
  - sample-graph-inventory.md
  - operator-inventory.md
  - environment-labels.md

Usage:
    python scripts/generate_beta_inventory.py \
        --graphs-dir graphs/ \
        --reference-graphs-dir reference_graphs/ \
        --fixture-graphs-dir tests/graphs/listening/audio/ \
        --fixture-graphs-dir tests/graphs/parity/ \
        --operators-dir operators/ \
        --filters-dir filters/ \
        --out-dir docs/plans/beta-release-readiness/phase-1/
"""

import argparse
import json
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

# ---------------------------------------------------------------------------
# Environment-dependency map: registered class name -> label
# ---------------------------------------------------------------------------

ENV_DEPS: dict[str, str] = {
    "WebcamIn": "camera",
    "MicInput": "microphone",
    "MidiInput": "midi_hardware",
    "MidiFilePlayer": "midi_files",
    "SyphonIn": "syphon",
    "SyphonOut": "syphon",
    "MovieFileIn": "movie_media",
    "MovieFileAudio": "movie_media",
    "OscIn": "osc_network",
    "OscOut": "osc_network",
    "Sampler": "sample_files",
    "ConvolutionReverb": "ir_files",
    "FolderList": "filesystem",
    "TextureLoader": "filesystem",
}

# Folder display order
FOLDER_ORDER = [
    "intro",
    "audio",
    "gpu",
    "filters",
    "io",
    "media",
    "media/movie_file",
    "media/texture_loader",
    "shader_sketches",
    "listening/audio",
    "parity",
]

INTRO_GRAPHS = {
    "demo.json",
    "av_demo.json",
    "av_metronome_demo.json",
    "audio_demo.json",
    "audio_reactive_demo.json",
}

# ---------------------------------------------------------------------------
# Scanning
# ---------------------------------------------------------------------------


def get_commit_hash() -> str:
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
            .decode()
            .strip()
        )
    except Exception:
        return "unknown"


def scan_graphs(graphs_dir: Path, collection: str | None = None) -> list[dict]:
    results = []
    collection = collection or graphs_dir.name
    for json_path in sorted(graphs_dir.rglob("*.json")):
        try:
            data = json.loads(json_path.read_text())
        except (json.JSONDecodeError, OSError):
            continue

        meta = data.get("meta", {})
        nodes = data.get("nodes", {})

        # Collect all operator type names used in the graph
        node_types = set()
        for node in nodes.values():
            t = node.get("type")
            if t:
                node_types.add(t)

        # Determine folder group
        rel = json_path.relative_to(graphs_dir)
        parts = rel.parts
        if len(parts) >= 3:
            folder = f"{parts[0]}/{parts[1]}"
        elif len(parts) >= 2:
            folder = parts[0]
        else:
            folder = "root"

        # Determine env deps from node types
        graph_env_deps = set()
        for nt in node_types:
            if nt in ENV_DEPS:
                graph_env_deps.add(ENV_DEPS[nt])

        display_path = str(Path(collection) / rel)

        results.append(
            {
                "path": display_path,
                "rel_path": str(rel),
                "collection": collection,
                "file": rel.name,
                "folder": folder,
                "meta": meta,
                "node_types": node_types,
                "env_deps": sorted(graph_env_deps),
                "requires_packages": meta.get("requires_packages", []),
            }
        )

    return results


def scan_cpp_operators(operators_dir: Path) -> list[dict]:
    """Find all .cpp/.mm files containing VIVID_REGISTER.

    Deduplicates by class name — prefers .mm over _stub.cpp so macOS-native
    implementations win over cross-platform stubs.
    """
    seen: dict[str, dict] = {}
    # Scan .mm first so native implementations are preferred over stubs
    for ext in ("*.mm", "*.cpp"):
        for src_path in sorted(operators_dir.rglob(ext)):
            text = src_path.read_text()
            m = re.search(r"VIVID_REGISTER\(\s*(\w+)\s*\)", text)
            if m:
                class_name = m.group(1)
                if class_name in seen:
                    continue
                rel = src_path.relative_to(operators_dir)
                parts = rel.parts
                domain = parts[0] if len(parts) >= 2 else "unknown"
                op_id = parts[1] if len(parts) >= 2 else src_path.stem
                seen[class_name] = {
                    "class_name": class_name,
                    "domain": domain,
                    "op_id": op_id,
                    "source": str(Path("operators") / rel),
                    "env_dep": ENV_DEPS.get(class_name, ""),
                }
    return sorted(seen.values(), key=lambda o: (o["domain"], o["class_name"]))


def scan_wgsl_filters(filters_dir: Path) -> list[dict]:
    """Parse WGSL filter headers for the name field."""
    results = []
    for wgsl_path in sorted(filters_dir.glob("*.wgsl")):
        text = wgsl_path.read_text()
        # Extract JSON header: /*{ ... }*/
        m = re.search(r"/\*\{(.*?)\}\*/", text, re.DOTALL)
        name = wgsl_path.stem.replace("_", " ").title().replace(" ", "")
        if m:
            try:
                header = json.loads("{" + m.group(1) + "}")
                name = header.get("name", name)
            except json.JSONDecodeError:
                pass
        results.append(
            {
                "name": name,
                "source": str(Path("filters") / wgsl_path.name),
            }
        )
    return results


# ---------------------------------------------------------------------------
# Cross-referencing
# ---------------------------------------------------------------------------


def cross_reference(
    graphs: list[dict],
    cpp_ops: list[dict],
    wgsl_ops: list[dict],
) -> dict[str, list[str]]:
    """Build operator_name -> list of graph paths that use it."""
    all_op_names = {op["class_name"] for op in cpp_ops} | {op["name"] for op in wgsl_ops}

    usage: dict[str, list[str]] = {name: [] for name in all_op_names}
    for g in graphs:
        for nt in g["node_types"]:
            if nt in usage:
                usage[nt].append(g["path"])
    return usage


# ---------------------------------------------------------------------------
# Markdown generation
# ---------------------------------------------------------------------------


def _table_row(cells: list[str]) -> str:
    return "| " + " | ".join(cells) + " |"


def write_graph_inventory(
    graphs: list[dict],
    commit: str,
    out_dir: Path,
) -> None:
    lines = [
        "# Sample Graph Inventory",
        "",
        f"Generated: {date.today()} | Commit: {commit} | Total: {len(graphs)}",
        "",
    ]

    # Group by collection, then folder
    from collections import OrderedDict

    by_collection: dict[str, dict[str, list[dict]]] = OrderedDict()
    for g in graphs:
        collection = g.get("collection", "graphs")
        folder = g["folder"]
        if collection not in by_collection:
            by_collection[collection] = OrderedDict((f, []) for f in FOLDER_ORDER)
        if folder not in by_collection[collection]:
            by_collection[collection][folder] = []
        by_collection[collection][folder].append(g)

    for collection, by_folder in by_collection.items():
        collection_count = sum(len(group) for group in by_folder.values())
        lines.append(f"## {collection}/ ({collection_count} graphs)")
        lines.append("")

        for folder, group in by_folder.items():
            if not group:
                continue
            lines.append(f"### {folder}/ ({len(group)} graphs)")
            lines.append("")
            header = ["File", "Title", "Difficulty", "Domains", "Env Deps", "Packages", "Featured", "Role"]
            lines.append(_table_row(header))
            lines.append(_table_row(["---"] * len(header)))
            for g in group:
                meta = g["meta"]
                lines.append(
                    _table_row(
                        [
                            g["file"],
                            meta.get("title", "--"),
                            meta.get("difficulty", "--"),
                            ", ".join(meta.get("domains", [])),
                            ", ".join(g["env_deps"]) if g["env_deps"] else "--",
                            ", ".join(g["requires_packages"]) if g["requires_packages"] else "--",
                            str(meta.get("featured_rank", "--")),
                            meta.get("role", "--"),
                        ]
                    )
                )
            lines.append("")

    # Environment-dependent graph summary
    lines.append("## Environment-Dependent Graphs")
    lines.append("")
    env_groups: dict[str, list[str]] = {}
    for g in graphs:
        for dep in g["env_deps"]:
            env_groups.setdefault(dep, []).append(g["path"])
    if env_groups:
        lines.append(_table_row(["Label", "Count", "Graphs"]))
        lines.append(_table_row(["---", "---", "---"]))
        for label in sorted(env_groups):
            paths = env_groups[label]
            lines.append(_table_row([label, str(len(paths)), ", ".join(paths)]))
        lines.append("")
    else:
        lines.append("None found.")
        lines.append("")

    # Package-dependent graph summary
    lines.append("## Package-Dependent Graphs")
    lines.append("")
    pkg_groups: dict[str, list[str]] = {}
    for g in graphs:
        for pkg in g["requires_packages"]:
            pkg_groups.setdefault(pkg, []).append(g["path"])
    if pkg_groups:
        lines.append(_table_row(["Package", "Count", "Graphs"]))
        lines.append(_table_row(["---", "---", "---"]))
        for pkg in sorted(pkg_groups):
            paths = pkg_groups[pkg]
            lines.append(_table_row([pkg, str(len(paths)), ", ".join(paths)]))
        lines.append("")
    else:
        lines.append("None found.")
        lines.append("")

    (out_dir / "sample-graph-inventory.md").write_text("\n".join(lines) + "\n")


def write_operator_inventory(
    cpp_ops: list[dict],
    wgsl_ops: list[dict],
    usage: dict[str, list[str]],
    commit: str,
    out_dir: Path,
) -> None:
    total = len(cpp_ops) + len(wgsl_ops)
    lines = [
        "# Operator Inventory",
        "",
        f"Generated: {date.today()} | Commit: {commit}",
        f"C++ Registered: {len(cpp_ops)} | WGSL Filters: {len(wgsl_ops)} | Total: {total}",
        "",
    ]

    # Group C++ operators by domain
    by_domain: dict[str, list[dict]] = {}
    for op in cpp_ops:
        by_domain.setdefault(op["domain"], []).append(op)

    domain_order = ["audio", "control", "gpu"]
    for domain in domain_order:
        group = by_domain.get(domain, [])
        if not group:
            continue
        lines.append(f"## {domain.title()} Operators ({len(group)})")
        lines.append("")
        header = ["Registered Name", "Source", "Env Dep", "Used In (count)", "Graphs"]
        lines.append(_table_row(header))
        lines.append(_table_row(["---"] * len(header)))
        for op in sorted(group, key=lambda o: o["class_name"]):
            graphs_using = usage.get(op["class_name"], [])
            graph_names = ", ".join(Path(p).stem for p in graphs_using[:5])
            if len(graphs_using) > 5:
                graph_names += f" (+{len(graphs_using) - 5} more)"
            lines.append(
                _table_row(
                    [
                        op["class_name"],
                        op["source"],
                        op["env_dep"] if op["env_dep"] else "--",
                        str(len(graphs_using)),
                        graph_names if graph_names else "--",
                    ]
                )
            )
        lines.append("")

    # Extra domains (if any)
    for domain, group in sorted(by_domain.items()):
        if domain in domain_order:
            continue
        lines.append(f"## {domain.title()} Operators ({len(group)})")
        lines.append("")
        header = ["Registered Name", "Source", "Env Dep", "Used In (count)", "Graphs"]
        lines.append(_table_row(header))
        lines.append(_table_row(["---"] * len(header)))
        for op in sorted(group, key=lambda o: o["class_name"]):
            graphs_using = usage.get(op["class_name"], [])
            graph_names = ", ".join(Path(p).stem for p in graphs_using[:5])
            if len(graphs_using) > 5:
                graph_names += f" (+{len(graphs_using) - 5} more)"
            lines.append(
                _table_row(
                    [
                        op["class_name"],
                        op["source"],
                        op["env_dep"] if op["env_dep"] else "--",
                        str(len(graphs_using)),
                        graph_names if graph_names else "--",
                    ]
                )
            )
        lines.append("")

    # WGSL filter operators
    lines.append(f"## WGSL Filter Operators ({len(wgsl_ops)})")
    lines.append("")
    header = ["Name", "Source", "Used In (count)", "Graphs"]
    lines.append(_table_row(header))
    lines.append(_table_row(["---"] * len(header)))
    for op in wgsl_ops:
        graphs_using = usage.get(op["name"], [])
        graph_names = ", ".join(Path(p).stem for p in graphs_using[:5])
        if len(graphs_using) > 5:
            graph_names += f" (+{len(graphs_using) - 5} more)"
        lines.append(
            _table_row(
                [
                    op["name"],
                    op["source"],
                    str(len(graphs_using)),
                    graph_names if graph_names else "--",
                ]
            )
        )
    lines.append("")

    # Operators not used in any graph
    unused_cpp = [op for op in cpp_ops if not usage.get(op["class_name"])]
    unused_wgsl = [op for op in wgsl_ops if not usage.get(op["name"])]
    lines.append(f"## Operators Not Used in Any Graph ({len(unused_cpp) + len(unused_wgsl)})")
    lines.append("")
    if unused_cpp or unused_wgsl:
        header = ["Name", "Domain/Type", "Source"]
        lines.append(_table_row(header))
        lines.append(_table_row(["---"] * len(header)))
        for op in sorted(unused_cpp, key=lambda o: o["class_name"]):
            lines.append(_table_row([op["class_name"], op["domain"], op["source"]]))
        for op in sorted(unused_wgsl, key=lambda o: o["name"]):
            lines.append(_table_row([op["name"], "wgsl_filter", op["source"]]))
        lines.append("")
    else:
        lines.append("All operators are used in at least one graph.")
        lines.append("")

    # Environment-dependent operator summary
    lines.append("## Environment-Dependent Operators")
    lines.append("")
    env_ops: dict[str, list[str]] = {}
    for op in cpp_ops:
        if op["env_dep"]:
            env_ops.setdefault(op["env_dep"], []).append(op["class_name"])
    if env_ops:
        lines.append(_table_row(["Label", "Operators"]))
        lines.append(_table_row(["---", "---"]))
        for label in sorted(env_ops):
            lines.append(_table_row([label, ", ".join(sorted(env_ops[label]))]))
        lines.append("")

    (out_dir / "operator-inventory.md").write_text("\n".join(lines) + "\n")


def write_environment_labels(
    graphs: list[dict],
    cpp_ops: list[dict],
    commit: str,
    out_dir: Path,
) -> None:
    lines = [
        "# Environment Labels",
        "",
        f"Generated: {date.today()} | Commit: {commit}",
        "",
        "## Label Definitions",
        "",
    ]

    label_defs = [
        ("camera", "Requires webcam access", "Yes, not in intro path"),
        ("microphone", "Requires microphone permission", "Yes, not in intro path"),
        ("midi_hardware", "Requires MIDI controller or IAC driver", "Yes"),
        ("midi_files", "Requires .mid files on disk", "Yes"),
        ("syphon", "Requires Syphon sender/receiver (macOS)", "Yes"),
        ("movie_media", "Requires movie file in media folder", "Conditional -- must degrade gracefully"),
        ("osc_network", "Requires OSC sender/receiver app", "Yes"),
        ("ir_files", "Requires impulse response audio files", "Conditional"),
        ("sample_files", "Requires audio sample files on disk", "Conditional"),
        ("filesystem", "Requires specific files/folders on disk", "Conditional"),
    ]

    lines.append(_table_row(["Label", "Meaning", "Skip OK for Beta?"]))
    lines.append(_table_row(["---", "---", "---"]))
    for label, meaning, skip in label_defs:
        lines.append(_table_row([label, meaning, skip]))
    lines.append("")

    # Cross-reference: label -> operators -> graphs -> in intro path?
    lines.append("## Cross-Reference")
    lines.append("")

    # Collect all labels from both operators and graphs
    all_labels = set()
    op_by_label: dict[str, list[str]] = {}
    for op in cpp_ops:
        if op["env_dep"]:
            all_labels.add(op["env_dep"])
            op_by_label.setdefault(op["env_dep"], []).append(op["class_name"])

    graph_by_label: dict[str, list[str]] = {}
    intro_affected: dict[str, bool] = {}
    for g in graphs:
        for dep in g["env_deps"]:
            all_labels.add(dep)
            graph_by_label.setdefault(dep, []).append(g["path"])
            if g["folder"] == "intro" or g["file"] in INTRO_GRAPHS:
                intro_affected[dep] = True

    lines.append(_table_row(["Label", "Operators", "Graph Count", "In Intro Path?"]))
    lines.append(_table_row(["---", "---", "---", "---"]))
    for label in sorted(all_labels):
        ops = ", ".join(sorted(op_by_label.get(label, [])))
        graph_count = len(graph_by_label.get(label, []))
        in_intro = "YES" if intro_affected.get(label, False) else "No"
        lines.append(_table_row([label, ops or "--", str(graph_count), in_intro]))
    lines.append("")

    # Detailed graph list per label
    lines.append("## Graphs by Label")
    lines.append("")
    for label in sorted(all_labels):
        graph_paths = graph_by_label.get(label, [])
        if graph_paths:
            lines.append(f"### {label} ({len(graph_paths)} graphs)")
            lines.append("")
            for p in sorted(graph_paths):
                lines.append(f"- `{p}`")
            lines.append("")

    (out_dir / "environment-labels.md").write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate beta readiness inventory")
    parser.add_argument("--graphs-dir", required=True, help="Path to graphs/ directory")
    parser.add_argument("--reference-graphs-dir", action="append", default=[],
                        help="Optional reference_graphs/ directory to include in graph inventory")
    parser.add_argument("--fixture-graphs-dir", action="append", default=[],
                        help="Optional fixture graph directory to include in graph inventory")
    parser.add_argument("--operators-dir", required=True, help="Path to operators/ directory")
    parser.add_argument("--filters-dir", required=True, help="Path to filters/ directory")
    parser.add_argument("--out-dir", required=True, help="Output directory for markdown files")
    args = parser.parse_args()

    graphs_dir = Path(args.graphs_dir)
    operators_dir = Path(args.operators_dir)
    filters_dir = Path(args.filters_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    commit = get_commit_hash()

    print("Scanning graphs...")
    graphs = scan_graphs(graphs_dir, "graphs")
    for ref_dir in args.reference_graphs_dir:
        graphs.extend(scan_graphs(Path(ref_dir), Path(ref_dir).name))
    for fixture_dir in args.fixture_graphs_dir:
        fixture_path = Path(fixture_dir)
        graphs.extend(scan_graphs(fixture_path, str(fixture_path)))
    print(f"  Found {len(graphs)} graphs")

    print("Scanning C++ operators...")
    cpp_ops = scan_cpp_operators(operators_dir)
    print(f"  Found {len(cpp_ops)} registered operators")

    print("Scanning WGSL filters...")
    wgsl_ops = scan_wgsl_filters(filters_dir)
    print(f"  Found {len(wgsl_ops)} filter operators")

    print("Cross-referencing...")
    usage = cross_reference(graphs, cpp_ops, wgsl_ops)

    print("Writing sample-graph-inventory.md...")
    write_graph_inventory(graphs, commit, out_dir)

    print("Writing operator-inventory.md...")
    write_operator_inventory(cpp_ops, wgsl_ops, usage, commit, out_dir)

    print("Writing environment-labels.md...")
    write_environment_labels(graphs, cpp_ops, commit, out_dir)

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
