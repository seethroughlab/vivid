#!/usr/bin/env python3
"""Extract structured /** */ doc comments from Vivid operator source files.

Walks the operators/ directory, finds files containing VIVID_REGISTER(),
extracts doc blocks placed before the struct definition, and outputs JSON
for the website.

Usage:
    python scripts/extract_operator_docs.py \
        --operators-dir operators/ \
        --out-dir site/operators/ \
        [--runtime-json operators_runtime.json] \
        [--warn-missing]
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path


def find_operator_files(operators_dir: Path) -> list[dict]:
    """Find all .cpp files containing VIVID_REGISTER and extract the class name."""
    results = []
    for cpp_path in sorted(operators_dir.rglob("*.cpp")):
        text = cpp_path.read_text()
        m = re.search(r"VIVID_REGISTER\(\s*(\w+)\s*\)", text)
        if m:
            class_name = m.group(1)
            # Derive domain and id from path: operators/{domain}/{id}/...
            rel = cpp_path.relative_to(operators_dir)
            parts = rel.parts
            domain = parts[0] if len(parts) >= 2 else "unknown"
            op_id = parts[1] if len(parts) >= 2 else cpp_path.stem
            results.append({
                "class_name": class_name,
                "cpp_path": cpp_path,
                "domain": domain,
                "id": op_id,
            })
    return results


def find_struct_file(op: dict) -> tuple[Path, list[str]] | None:
    """Find the file containing 'struct ClassName :' and return (path, lines)."""
    class_name = op["class_name"]
    pattern = re.compile(rf"^\s*struct\s+{re.escape(class_name)}\s*:")

    # Check .cpp first
    cpp_lines = op["cpp_path"].read_text().splitlines()
    for i, line in enumerate(cpp_lines):
        if pattern.match(line):
            return op["cpp_path"], cpp_lines

    # Check sibling .h files
    directory = op["cpp_path"].parent
    for h_path in sorted(directory.glob("*.h")):
        h_lines = h_path.read_text().splitlines()
        for i, line in enumerate(h_lines):
            if pattern.match(line):
                return h_path, h_lines

    # Follow #include directives — search operators tree for the header
    operators_root = op["cpp_path"]
    while operators_root.name != "operators" and operators_root != operators_root.parent:
        operators_root = operators_root.parent
    for line in cpp_lines:
        inc_match = re.match(r'#include\s+"([^"]+\.h)"', line)
        if inc_match:
            inc_name = Path(inc_match.group(1)).name
            for h_path in operators_root.rglob(inc_name):
                h_lines = h_path.read_text().splitlines()
                for i, h_line in enumerate(h_lines):
                    if pattern.match(h_line):
                        return h_path, h_lines

    return None


def extract_doc_block(lines: list[str], class_name: str) -> str | None:
    """Extract the /** ... */ block immediately before 'struct ClassName :'."""
    pattern = re.compile(rf"^\s*struct\s+{re.escape(class_name)}\s*:")

    struct_line = None
    for i, line in enumerate(lines):
        if pattern.match(line):
            struct_line = i
            break

    if struct_line is None:
        return None

    # Scan backwards to find */ then /**
    end_line = None
    for i in range(struct_line - 1, -1, -1):
        stripped = lines[i].strip()
        if stripped == "":
            continue
        if stripped.endswith("*/"):
            end_line = i
            break
        else:
            return None  # Non-blank, non-comment line before struct = no doc block

    if end_line is None:
        return None

    # Scan backwards from end_line to find /**
    start_line = None
    for i in range(end_line, -1, -1):
        if "/**" in lines[i]:
            start_line = i
            break

    if start_line is None:
        return None

    # Extract and clean the block
    raw_lines = lines[start_line : end_line + 1]
    cleaned = []
    for line in raw_lines:
        s = line.strip()
        # Remove opening /** and closing */
        s = re.sub(r"^/\*\*\s?", "", s)
        s = re.sub(r"\s?\*/$", "", s)
        # Remove leading * prefix
        s = re.sub(r"^\*\s?", "", s)
        # Remove standalone * on blank lines
        if s == "*":
            s = ""
        cleaned.append(s)

    # Remove empty leading/trailing lines
    while cleaned and cleaned[0].strip() == "":
        cleaned.pop(0)
    while cleaned and cleaned[-1].strip() == "":
        cleaned.pop()

    return "\n".join(cleaned)


def parse_doc_block(raw: str) -> dict:
    """Parse a cleaned doc block into structured fields."""
    result = {
        "brief": None,
        "description": None,
        "tips": [],
        "related": [],
        "params": {},
        "inputs": {},
        "outputs": {},
    }

    lines = raw.split("\n")
    current_tag = None
    current_name = None
    body_lines = []

    def flush_body():
        nonlocal body_lines
        if body_lines:
            text = "\n".join(body_lines).strip()
            if text:
                result["description"] = text
            body_lines = []

    for line in lines:
        # Check for @tag at start of line
        tag_match = re.match(r"^@(\w+)\s*(.*)", line)

        if tag_match:
            tag = tag_match.group(1)
            rest = tag_match.group(2).strip()

            if tag == "brief":
                # Flush any accumulated body before brief (shouldn't happen, but safe)
                flush_body()
                result["brief"] = rest
                current_tag = "body"  # Lines after @brief are body until next tag
                current_name = None

            elif tag == "tip":
                flush_body()
                result["tips"].append(rest)
                current_tag = "tip"
                current_name = len(result["tips"]) - 1

            elif tag == "see":
                flush_body()
                names = [n.strip() for n in rest.split(",") if n.strip()]
                result["related"] = names
                current_tag = "see"
                current_name = None

            elif tag in ("param", "input", "output"):
                flush_body()
                # Extract name token
                parts = rest.split(None, 1)
                if parts:
                    name = parts[0]
                    doc = parts[1] if len(parts) > 1 else ""
                    target = {"param": "params", "input": "inputs", "output": "outputs"}[tag]
                    result[target][name] = doc
                    current_tag = tag
                    current_name = name
                else:
                    current_tag = None
                    current_name = None
            else:
                # Unknown tag, treat as body
                if current_tag == "body":
                    body_lines.append(line)
        else:
            # Continuation line
            if current_tag == "body":
                body_lines.append(line)
            elif current_tag == "tip" and current_name is not None:
                result["tips"][current_name] += " " + line.strip()
            elif current_tag in ("param", "input", "output") and current_name is not None:
                target = {"param": "params", "input": "inputs", "output": "outputs"}[current_tag]
                result[target][current_name] += " " + line.strip()
            elif current_tag is None:
                # Before any tag — treat as body (shouldn't happen with well-formed blocks)
                body_lines.append(line)

    flush_body()

    # Clean up whitespace in all string values
    for key in ("params", "inputs", "outputs"):
        for name in result[key]:
            result[key][name] = result[key][name].strip()

    return result


def load_runtime_metadata(runtime_json_path: Path) -> dict:
    """Load runtime list_types() JSON and index by operator name."""
    data = json.loads(runtime_json_path.read_text())
    # Handle both direct format and nested {ok, result} format
    types = data
    if isinstance(data, dict):
        if "result" in data:
            types = data["result"]
        if isinstance(types, dict) and "types" in types:
            types = types["types"]

    index = {}
    for t in types:
        name = t.get("name")
        if name:
            index[name] = t
    return index


def merge_runtime(doc: dict, runtime: dict) -> dict:
    """Merge runtime metadata into the doc entry."""
    rt = runtime.get(doc["name"])
    if not rt:
        return doc

    # Merge params: runtime is authoritative for specs, doc block provides narrative
    param_docs = doc.get("param_docs", {})
    merged_params = []
    for p in rt.get("params", []):
        entry = {
            "name": p.get("name"),
            "type": p.get("type"),
            "default": p.get("default"),
            "min": p.get("min"),
            "max": p.get("max"),
            "choices": p.get("choices"),
            "semantic_tag": p.get("semantic_tag"),
            "semantic_shape": p.get("semantic_shape"),
            "semantic_unit": p.get("semantic_unit"),
            "semantic_intent": p.get("semantic_intent"),
            "doc": param_docs.get(p.get("name")),
        }
        merged_params.append(entry)
    doc["params"] = merged_params

    # Merge ports
    input_docs = doc.get("input_docs", {})
    output_docs = doc.get("output_docs", {})
    merged_inputs = []
    merged_outputs = []
    for p in rt.get("inputs", []):
        merged_inputs.append({
            "name": p.get("name"),
            "type": p.get("type"),
            "doc": input_docs.get(p.get("name")),
        })
    for p in rt.get("outputs", []):
        merged_outputs.append({
            "name": p.get("name"),
            "type": p.get("type"),
            "doc": output_docs.get(p.get("name")),
        })
    doc["inputs"] = merged_inputs
    doc["outputs"] = merged_outputs

    # Remove intermediate fields
    doc.pop("param_docs", None)
    doc.pop("input_docs", None)
    doc.pop("output_docs", None)

    return doc


def build_operator_doc(op: dict, runtime: dict | None) -> dict:
    """Build the full doc entry for one operator."""
    result = find_struct_file(op)
    if result is None:
        source_file = str(op["cpp_path"])
    else:
        source_file = str(result[0])

    entry = {
        "name": op["class_name"],
        "id": op["id"],
        "domain": op["domain"],
        "source_file": source_file,
        "brief": None,
        "description": None,
        "tips": [],
        "related": [],
        "has_docs": False,
    }

    if result is not None:
        path, lines = result
        raw = extract_doc_block(lines, op["class_name"])
        if raw:
            parsed = parse_doc_block(raw)
            entry["brief"] = parsed["brief"]
            entry["description"] = parsed["description"]
            entry["tips"] = parsed["tips"]
            entry["related"] = parsed["related"]
            entry["has_docs"] = parsed["brief"] is not None

            # Store doc narratives for runtime merge
            entry["param_docs"] = parsed["params"]
            entry["input_docs"] = parsed["inputs"]
            entry["output_docs"] = parsed["outputs"]

    if runtime:
        entry = merge_runtime(entry, runtime)
    else:
        # Without runtime, convert doc narratives to simple param/port lists
        param_docs = entry.pop("param_docs", {})
        input_docs = entry.pop("input_docs", {})
        output_docs = entry.pop("output_docs", {})
        entry["params"] = [{"name": k, "doc": v} for k, v in param_docs.items()]
        entry["inputs"] = [{"name": k, "doc": v} for k, v in input_docs.items()]
        entry["outputs"] = [{"name": k, "doc": v} for k, v in output_docs.items()]

    # Make source_file relative
    try:
        entry["source_file"] = str(Path(entry["source_file"]).relative_to(Path.cwd()))
    except ValueError:
        pass

    return entry


def main():
    parser = argparse.ArgumentParser(description="Extract operator doc comments to JSON")
    parser.add_argument("--operators-dir", required=True, help="Path to operators/ directory")
    parser.add_argument("--out-dir", required=True, help="Output directory for JSON files")
    parser.add_argument("--runtime-json", help="Optional list_types() JSON dump for metadata merge")
    parser.add_argument("--warn-missing", action="store_true", help="Print operators without doc blocks")
    args = parser.parse_args()

    operators_dir = Path(args.operators_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    runtime = None
    if args.runtime_json:
        runtime = load_runtime_metadata(Path(args.runtime_json))

    ops = find_operator_files(operators_dir)
    print(f"Found {len(ops)} operators")

    entries = []
    missing = []
    for op in ops:
        entry = build_operator_doc(op, runtime)
        entries.append(entry)
        if not entry["has_docs"]:
            missing.append(entry)

    # Write index
    index = {
        "schema_version": 1,
        "operators": [
            {
                "name": e["name"],
                "id": e["id"],
                "domain": e["domain"],
                "brief": e["brief"],
                "has_docs": e["has_docs"],
                "related": e["related"],
            }
            for e in entries
        ],
    }
    index_path = out_dir / "index.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n")
    print(f"Wrote {index_path}")

    # Write per-operator detail files
    documented = 0
    for entry in entries:
        if entry["has_docs"]:
            detail_path = out_dir / f"{entry['id']}.json"
            detail_path.write_text(json.dumps(entry, indent=2) + "\n")
            documented += 1

    print(f"Wrote {documented} detail files ({len(entries) - documented} undocumented)")

    if args.warn_missing and missing:
        print(f"\nMissing doc blocks ({len(missing)}):")
        for m in missing:
            print(f"  {m['domain']}/{m['id']} ({m['name']})")

    return 0 if not args.warn_missing else (1 if missing else 0)


if __name__ == "__main__":
    sys.exit(main())
