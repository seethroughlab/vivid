# Package Catalog

This directory contains the curated package discovery catalog used by Vivid discovery surfaces.

## Source of truth

- `packages.json` is the canonical curated index for package metadata.
- Runtime parsers currently require (at minimum):
  - `name`
  - `description`
  - `version`
  - `url` (install URL)
  - optional: `vivid_core`, `author`, `category`, `tags`

## Extended discovery fields

The catalog also includes website/discovery fields not yet consumed by all runtime paths:

- `install_url`
- `repo_url`
- `homepage_url`
- `description_short`
- `preview_image_url`
- `maintainer`
- `status`

## Notes

- Preview image URLs may initially point to placeholders.
- Package additions/changes should be reviewed via PR.
