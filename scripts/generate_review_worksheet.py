#!/usr/bin/env python3
"""Generate Phase 3 A/V review worksheet from graph metadata."""

import json
import os
from pathlib import Path


def main():
    graphs_dir = Path("graphs")
    folders = ["intro", "audio", "gpu", "filters", "media/movie_file", "io"]

    lines = [
        "# Phase 3: Curated Graph A/V Review Worksheet",
        "",
        "Reviewer: ____________________",
        "Date: ____________________",
        "Commit: e1f2c5b0",
        "Audio device: ____________________",
        "Listening level: ____________________",
        "",
        "## Classification Key",
        "",
        "- **ready** -- no issues",
        "- **minor polish** -- small cosmetic/balance issues, not blocking",
        "- **confusing but usable** -- works but could mislead a beginner",
        "- **blocking** -- must fix or remove from beta surface",
        "- **env skip** -- requires hardware/media not present, labeled correctly",
        "",
    ]

    total = 0
    env_types = {
        "WebcamIn", "MicInput", "MidiInput", "MovieFileIn", "MovieFileAudio",
        "SyphonIn", "SyphonOut", "OscIn", "OscOut", "Sampler", "ConvolutionReverb",
    }

    for folder in folders:
        folder_path = graphs_dir / folder.replace("/", os.sep)
        json_files = sorted(folder_path.glob("*.json"))

        if not json_files:
            continue

        entries = []
        for jf in json_files:
            data = json.loads(jf.read_text())
            meta = data.get("meta", {})
            nodes = data.get("nodes", {})
            node_types = {n.get("type", "") for n in nodes.values()}

            env_deps = sorted(t for t in node_types if t in env_types)

            has_audio = "audio" in meta.get("domains", [])
            has_gpu = "gpu" in meta.get("domains", [])

            if has_audio and has_gpu:
                domain_label = "A/V"
            elif has_audio:
                domain_label = "Audio"
            elif has_gpu:
                domain_label = "Visual"
            else:
                domain_label = "Control"

            entries.append({
                "file": jf.name,
                "title": meta.get("title", jf.stem),
                "difficulty": meta.get("difficulty", "—"),
                "domain": domain_label,
                "env_deps": ", ".join(env_deps) if env_deps else "—",
                "packages": ", ".join(meta.get("requires_packages", [])) or "—",
                "description": meta.get("description", ""),
            })

        lines.append(f"## {folder}/ ({len(entries)} graphs)")
        lines.append("")

        for i, e in enumerate(entries):
            title = e["title"]
            fname = e["file"]
            lines.append(f"### {i+1}. {title} (`{folder}/{fname}`)")
            lines.append("")
            lines.append(
                f"**Type:** {e['domain']} | "
                f"**Difficulty:** {e['difficulty']} | "
                f"**Env:** {e['env_deps']} | "
                f"**Packages:** {e['packages']}"
            )
            if e["description"]:
                lines.append("")
                lines.append(f"> {e['description']}")
            lines.append("")
            lines.append("| Check | Result | Notes |")
            lines.append("|-------|--------|-------|")
            lines.append("| First-load | | |")
            if "Audio" in e["domain"] or "A/V" in e["domain"]:
                lines.append("| Audio | | |")
            if "Visual" in e["domain"] or "A/V" in e["domain"]:
                lines.append("| Visual | | |")
            if "A/V" in e["domain"]:
                lines.append("| A/V sync | | |")
            lines.append("| **Result** | | |")
            lines.append("")
            total += 1

    out_path = Path("docs/plans/beta-release-readiness/phase-3-review-worksheet.md")
    out_path.write_text("\n".join(lines) + "\n")
    print(f"Generated worksheet with {total} graph entries")


if __name__ == "__main__":
    main()
