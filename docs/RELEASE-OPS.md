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

- Trigger: push tag `vX.Y.Z` (or manual `workflow_dispatch` with tag input)
- `release-macos.yml` validates tag/version consistency against `project(vivid VERSION ...)`
- Builds Release app bundle
- Codesigns, notarizes, and staples `Vivid.app`
- Embeds Sparkle framework into `Vivid.app/Contents/Frameworks`
- Verifies signature/stapling (`codesign --verify`, `spctl --assess`)
- Packages zip artifact `Vivid-<version>-macos-arm64.zip`
- Generates `appcast.xml` for Sparkle consumers
- Publishes both files to GitHub Releases
- Updates `catalog/appcast.xml` on `master` for Pages-hosted feed

## Rollback / Reissue

- If a tag release is invalid, create a new patch tag (for example `v0.1.1`) and re-run release.
- Do not rewrite an existing published tag.
- If appcast entry is incorrect, regenerate and commit `catalog/appcast.xml` with the corrected enclosure metadata.

## Notes

- `version-guard.yml` enforces version fallback macros match CMake version.
- Runtime app update checks read `https://vivid.seethroughlab.com/appcast.xml` by default.
- Override feed for testing via `VIVID_APPCAST_URL`.
