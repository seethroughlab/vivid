# Site Build

This directory contains the source data and build scripts for the published Vivid site.

## Source of truth

Repo-authored site inputs live here:

- `src/templates/` — HTML templates used by the static site build
- `src/assets/` — shared CSS and any optional enhancement assets
- `operators/*.json` — operator metadata inputs
- `operators/index.json` — operator index/order input
- `repos.json` — package repo input
- `packages.json` — generated package catalog data
- `generate.py` — builds `packages.json` from `repos.json` + package manifests
- `build_site.py` — renders the publishable static HTML site

## Generated output

Generated site output is not checked in.

- local preview output: `site/dist/`
- CI publish output: `_site/`

Do not hand-edit generated HTML.

## Local build

From the repo root:

```bash
python3 site/build_site.py
python3 -m http.server --directory site/dist 8000
```

If you need to refresh package metadata first:

```bash
python3 site/generate.py --local
python3 site/build_site.py
```

## Published artifacts

The Pages build publishes:

- the generated HTML pages
- shared assets
- `packages.json`
- `appcast.xml`
