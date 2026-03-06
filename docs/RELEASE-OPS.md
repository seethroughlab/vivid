# Release Operations (macOS)

This document defines the Milestone 6 release flow for signed/notarized macOS builds and appcast updates.

## Required GitHub Secrets

- `APPLE_CERT_P12_B64` — base64-encoded Developer ID Application cert (.p12)
- `APPLE_CERT_PASSWORD` — password for the .p12
- `APPLE_CODESIGN_IDENTITY` — codesign identity (for example `Developer ID Application: ...`)
- `APPLE_ID` — Apple account email for notarization
- `APPLE_TEAM_ID` — Apple Developer Team ID
- `APPLE_APP_PASSWORD` — app-specific password for notarytool
- `VIVID_SPARKLE_PUBLIC_KEY` — Sparkle EdDSA public key used in `SUPublicEDKey`

## Workflow

Two-workflow model:

- `release-macos-validate.yml` (manual `workflow_dispatch`)
  - Build/sign/notarize/staple/verify using a chosen ref.
  - Uploads validation zip artifact only.
  - Does **not** create GitHub Releases.
  - Does **not** update `catalog/appcast.xml`.
- `release-macos.yml` (publish-only)
  - Trigger: push intentional public tag `vX.Y.Z`.
  - Validates tag/version consistency against `project(vivid VERSION ...)`.
  - Builds, signs, notarizes, staples, and validates.
  - Packages release zip + generates `appcast.xml`.
  - Publishes assets to GitHub Releases.
  - Updates `catalog/appcast.xml` on `master` for Pages-hosted feed.

Policy:

- Do not cut public tags for CI-only fixes.
- Use validate workflow for iteration.
- Tag only when promoting a user-facing rolling-alpha checkpoint.

## Rollback / Reissue

- If a tag release is invalid, create a new patch tag (for example `v0.1.1`) and re-run release.
- Do not rewrite an existing published tag.
- If appcast entry is incorrect, regenerate and commit `catalog/appcast.xml` with the corrected enclosure metadata.

## Notes

- `version-guard.yml` enforces version fallback macros match CMake version.
- Runtime app update checks read `https://vivid.seethroughlab.com/appcast.xml` by default.
- Override feed for testing via `VIVID_APPCAST_URL`.
