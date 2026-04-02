#!/usr/bin/env python3
"""Generate site/packages.json from repos.json + vivid-package.json manifests.

By default, fetches manifests from GitHub. Use --local to read from sibling
repositories (../vivid-{name}/vivid-package.json) instead.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from datetime import date
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPOS_JSON = SCRIPT_DIR / "repos.json"
OUTPUT_JSON = SCRIPT_DIR / "packages.json"

MANIFEST_FIELDS = [
    "name",
    "description",
    "version",
    "vivid_core",
    "author",
    "category",
    "description_short",
    "tags",
    "operators",
    "gpu_operators",
    "build",
    "site_docs",
]

REPO_META_FIELDS = [
    "status",
    "status_note",
    "preview_image_url",
    "homepage_url",
    "default_ref",
]

RAW_URL_TEMPLATE = (
    "https://raw.githubusercontent.com/seethroughlab/{name}/{ref}/vivid-package.json"
)


def fetch_manifest_remote(name: str, ref: str) -> dict | None:
    url = RAW_URL_TEMPLATE.format(name=name, ref=ref)
    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            return json.loads(resp.read())
    except Exception as exc:
        print(f"  warning: failed to fetch {name}@{ref}: {exc}", file=sys.stderr)
        return None


def fetch_manifest_local(name: str) -> dict | None:
    path = SCRIPT_DIR.parent.parent / name / "vivid-package.json"
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        print(f"  warning: {path} not found", file=sys.stderr)
        return None
    except Exception as exc:
        print(f"  warning: failed to read {path}: {exc}", file=sys.stderr)
        return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--local",
        action="store_true",
        help="Read manifests from sibling directories instead of GitHub",
    )
    args = parser.parse_args()

    repos = json.loads(REPOS_JSON.read_text())
    packages = []

    for repo in repos:
        name = repo["name"]
        ref = repo.get("default_ref", "master")
        source = "local sibling repos" if args.local else f"GitHub@{ref}"
        print(f"Reading {name} ({source})...", file=sys.stderr)

        if args.local:
            manifest = fetch_manifest_local(name)
        else:
            manifest = fetch_manifest_remote(name, ref)

        if manifest is None:
            continue

        entry = {field: manifest.get(field, [] if field in {"tags", "operators", "gpu_operators"} else {}) if field == "site_docs" else manifest.get(field, "") for field in MANIFEST_FIELDS}
        entry["url"] = repo["url"]

        for field in REPO_META_FIELDS:
            if field in repo:
                entry[field] = repo[field]

        url = repo["url"]
        entry["repo_url"] = url.removesuffix(".git")
        entry["install_url"] = url

        if not entry.get("preview_image_url"):
            derived_url = f"https://raw.githubusercontent.com/seethroughlab/{name}/{ref}/docs/images/preview.png"
            if args.local:
                preview_path = SCRIPT_DIR.parent.parent / name / "docs" / "images" / "preview.png"
                if preview_path.exists():
                    entry["preview_image_url"] = derived_url
                else:
                    print(f"  note: {name} has no docs/images/preview.png, skipping thumbnail", file=sys.stderr)
            else:
                entry["preview_image_url"] = derived_url

        packages.append(entry)

    catalog = {
        "schema_version": 1,
        "generated_at": date.today().isoformat(),
        "packages": packages,
    }

    OUTPUT_JSON.write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"Wrote {len(packages)} packages to {OUTPUT_JSON}", file=sys.stderr)


if __name__ == "__main__":
    main()
