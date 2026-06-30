# Release runbook (P4.5)

The release pipeline for the macOS app. **Status: scaffolded, partly unverified.** The
credential-free pieces are tested and run anywhere; the signing/notarization/CI pieces are
encoded but need infrastructure this repo's dev/CI environment doesn't have (an Apple
Developer ID + a self-hosted macOS runner). They're written so a maintainer with those can
run them, and clearly marked as not-yet-exercised.

## What's verified vs. scaffolded

| Piece | File | State |
|-------|------|-------|
| Production gate | `scripts/run_production_gate.sh`, `tools/production_gate_*` | **verified** (runs locally) |
| Version guard | `tools/check_version.py` | **verified** |
| Appcast generator | `scripts/release/generate_appcast.py` | **verified** (`--selftest`) |
| PR-comment formatter | `tools/format_pr_comment.py` | **verified** (`--selftest`) |
| Sign + notarize + DMG | `scripts/release/sign_and_notarize.sh` | scaffold — needs Apple Developer ID |
| PR-gate CI | `.github/workflows/production-gate-pr.yml` | scaffold — needs a self-hosted macOS runner |
| Release CI | `.github/workflows/release-macos.yml` | scaffold — needs runner + Apple secrets |
| Auto-update bridge | `app/src/platform/sparkle_bridge.h` (+ stub) | stub — real Sparkle wired at release time |

## Versioning

One source of truth: `project(vivid_poc VERSION X.Y.Z)` in `app/CMakeLists.txt`, from which
`version.h` is generated (and the macOS bundle version strings + the `get_version`
endpoint read). To cut a release: bump that line, commit, tag `vX.Y.Z`. `version-guard.yml`
asserts the tag matches.

## Cutting a release (when infra is present)

1. Bump `project(... VERSION X.Y.Z)`, commit, `git tag vX.Y.Z && git push --tags`.
2. `release-macos.yml` fires on the tag: version-guard → build → **production gate must
   pass** → `sign_and_notarize.sh` → `generate_appcast.py` → upload the DMG + appcast.
3. Attach the DMG to the GitHub release; publish `appcast.xml` to the update feed.

### Required secrets (release runner)

`APPLE_CODESIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`
(an app-specific password, not the account password). For Sparkle signature validation,
also a `VIVID_SPARKLE_PUBLIC_KEY` / EdDSA signing key.

## Wiring real auto-update

`sparkle_bridge.h` is backed by a no-op stub today. To enable updates: embed
`Sparkle.framework` in `Contents/Frameworks/` at build time, replace the stub with a `.mm`
that drives `SPUStandardUpdaterController` (guarded `#ifdef __APPLE__`), and point it at the
published appcast. The stub keeps the dev build free of the framework dependency until then.
