# Package Discovery Spec (Phase 4)

Date: 2026-03-05
Status: Accepted for Milestone 3 Phase 4

## Decision

For Vivid 1.0, package discovery uses a **GitHub-hosted hybrid model**:

- Runtime: lightweight curated package index + install flow.
- Website: richer browsing/discovery experience (screenshots, tags, examples, docs).
- Shared source of truth: one curated catalog JSON in GitHub.

Explicit 1.0 constraint:
- Do **not** move CEF into vivid-core.

Primary website host:
- GitHub Pages for the `vivid` repo.

Fallback website host:
- `seethroughlab.github.io` (if GitHub Pages on `vivid` is constrained).

## Why this model

- Keeps runtime focused and stable.
- Enables richer discovery UX without shipping heavy UI work in core runtime.
- Uses one metadata source for both runtime and web.
- Fully compatible with GitHub-native hosting/workflows.

## 1.0 Minimal Implementation

### Catalog source

- Curated JSON file in `vivid` repo, e.g. `catalog/packages.json`.
- Updated via PRs only (no open write API for 1.0).

### Runtime surface

- Runtime consumes catalog JSON via existing package catalog plumbing.
- UI/CLI show package list, short description, version, and install URL.
- Detail links open package repo/homepage in browser for richer content.
- Runtime includes a lightweight bridge for direct install from discovery links.

### Website surface

- Static site generated from `catalog/packages.json`.
- Render package cards with:
  - name
  - short description
  - tags
  - preview image
  - repo/homepage links
  - install command snippet
  - direct install action (bridge-friendly link/button)

## Install Bridge Strategy (No Core CEF)

1.0 path (preferred):
- Open discovery website in the system browser from Vivid.
- Support direct install back into runtime via one of:
  - custom URL scheme (e.g. `vivid://install?url=...`)
  - local control-server endpoint with explicit user confirmation.

Deferred path:
- Optional in-app embedded web surface via platform-native webview (e.g. macOS `WKWebView`) if needed.
- Revisit CEF only after validating native-webview limits and post-1.0 tradeoffs.

### Governance

- Curation owner: `seethroughlab` maintainers.
- New package additions/updates reviewed via standard PR process.

## Catalog Schema (v1)

Each package entry should include:

- `name`
- `version`
- `vivid_core`
- `description_short`
- `repo_url`
- `homepage_url` (optional)
- `install_url`
- `tags` (array)
- `preview_image_url`
- `maintainer`

Optional:

- `examples` (array of graph/demo links)
- `license`
- `status` (`stable|experimental|deprecated`)

## Screenshots/Preview Policy

To support website discovery, each package repo should provide at least one preview image.

Recommended per-package convention:

- Store at `docs/images/preview.png` (or `.jpg`).
- Reference in package README.
- Publish stable URL (raw GitHub or Pages URL) and include in catalog `preview_image_url`.

Initial required rollout targets:

- `vivid-3d`
- `vivid-glitch`
- `vivid-wavetable`
- `vivid-drums`
- `vivid-plexus`
- `vivid-sequencers`

## Rollout Plan

1. Add `catalog/packages.json` to `vivid` with current package set.
2. Add `preview_image_url` entries for all sibling repos.
3. Add static catalog website (GitHub Pages in `vivid`; fallback `seethroughlab.github.io`).
4. Add runtime action to open discovery site.
5. Add direct install bridge (URL scheme or control-server route with confirmation).
6. Wire runtime browse/install UI to the curated catalog feed.
7. If needed, move site publishing to `seethroughlab.github.io` with same data source.

## Non-goals (1.0)

- No user-submitted package publishing portal.
- No automated package moderation pipeline.
- No package rating/review system.
- No CEF dependency in vivid-core.
