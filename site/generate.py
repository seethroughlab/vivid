#!/usr/bin/env python3
"""Generate site/packages.json from repos.json + vivid-package.json manifests.

By default, fetches manifests from GitHub. Use --local to read from sibling
directories (../vivid-{name}/vivid-package.json) instead.
"""

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
    "name", "description", "version", "vivid_core", "author",
    "category", "description_short",
]

REPO_META_FIELDS = ["status", "status_note", "preview_image_url", "homepage_url"]

RAW_URL_TEMPLATE = (
    "https://raw.githubusercontent.com/seethroughlab/{name}/master/vivid-package.json"
)


def fetch_manifest_remote(name: str) -> dict | None:
    url = RAW_URL_TEMPLATE.format(name=name)
    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            return json.loads(resp.read())
    except Exception as exc:
        print(f"  warning: failed to fetch {name}: {exc}", file=sys.stderr)
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

    fetch = fetch_manifest_local if args.local else fetch_manifest_remote
    source = "local sibling repos" if args.local else "GitHub"

    repos = json.loads(REPOS_JSON.read_text())

    packages = []
    for repo in repos:
        name = repo["name"]
        print(f"Reading {name} ({source})...", file=sys.stderr)
        manifest = fetch(name)
        if manifest is None:
            continue
        entry = {field: manifest.get(field, "") for field in MANIFEST_FIELDS}
        entry["url"] = repo["url"]

        # Merge catalog metadata from repos.json
        for field in REPO_META_FIELDS:
            if field in repo:
                entry[field] = repo[field]

        # Derive repo_url (strip .git suffix) and install_url
        url = repo["url"]
        entry["repo_url"] = url.removesuffix(".git")
        entry["install_url"] = url

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
