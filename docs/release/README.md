# Release runbook (P4.5)

The release pipeline for the macOS app. **Status: exercised end-to-end; auto-update
deferred.** Per [ADR-0040](../decisions/ADR-0040-mcp-native-creative-coding-is-the-public-promise.md)
the release CI has produced a **signed + notarized** DMG on the self-hosted macOS runner
(`spctl --assess` → "accepted, Notarized Developer ID"; ticket stapled), and the PR gate
runs on every PR. The one piece that is **not** wired is the Sparkle auto-updater — it is a
no-op stub, deferred to a post-first-release step. Signing/notarization still require a
maintainer's Apple Developer ID to run.

## What's verified vs. deferred

| Piece | File | State |
|-------|------|-------|
| Production gate | `scripts/run_production_gate.sh`, `tools/production_gate_*` | **verified** (runs locally + on every PR) |
| Version guard | `tools/check_version.py` | **verified** |
| Appcast generator | `scripts/release/generate_appcast.py` | **verified** (`--selftest`) |
| PR-comment formatter | `tools/format_pr_comment.py` | **verified** (`--selftest`) |
| Sign + notarize + DMG | `scripts/release/sign_and_notarize.sh` | **exercised** (ADR-0040); tag builds **require** notarization (`REQUIRE_NOTARIZE`) |
| Release verification | `scripts/run_release_verification.sh` | **verified** — the release re-runs the sanitizer + audio legs before signing |
| PR-gate CI | `.github/workflows/production-gate-pr.yml` | **active** — runs on every PR (self-hosted macOS runner) |
| Release CI | `.github/workflows/release-macos.yml` | **exercised** (ADR-0040) — sign/notarize/DMG/appcast on a tag |
| Auto-update bridge | `app/src/platform/sparkle_bridge.h` (+ stub) | **stub — deferred**, not wired for first release |

## Versioning

One source of truth: `project(vivid VERSION X.Y.Z)` in `app/CMakeLists.txt`, from which
`version.h` is generated (and the macOS bundle version strings + the `get_version`
endpoint read). To cut a release: bump that line, commit, tag `vX.Y.Z`. `version-guard.yml`
asserts the tag matches.

## Cutting a release (when infra is present)

1. Bump `project(... VERSION X.Y.Z)`, commit, `git tag vX.Y.Z && git push --tags`.
2. `release-macos.yml` fires on the tag: version-guard → build → **production gate** →
   **release verification** (`run_release_verification.sh`: ASan/UBSan + ThreadSanitizer +
   audio-engine + audio-thread-sanitizer must pass) → `sign_and_notarize.sh` (notarization
   **required** on a tag) → `generate_appcast.py` → upload the DMG + appcast.
3. Attach the DMG to the GitHub release; publish `appcast.xml` to the update feed.

## Reproducing the CI test legs locally

The PR gate and the release build run these legs; reproduce them locally:

- `VIVID_BUILD_DIR=app/build scripts/run_production_gate.sh core` — the `HEADLESS_SMOKE`
  gate with its min-test guard (build `app/build` with `-DVIVID_BUILD_APP=OFF` first).
- `scripts/run_release_verification.sh` — the release-critical legs run before signing:
  headless **ASan/UBSan**, **ThreadSanitizer** (`ctest -L THREAD`), **audio-engine**
  (`ctest -L AUDIO_ENGINE`), and **audio-thread-sanitizer** (`ctest -L AUDIO_THREAD`). It
  builds into `app/build-verify-*` dirs and fails on any red.

Build-dir naming (so local matches CI): the PR gate uses `app/build` (app-OFF),
`app/build-audio` (audio-engine, app-ON), `app/build-tsan-audio` (audio TSan); the portable
headless CI job uses top-level `build` / `build-tsan`.

### Signing + notarization credentials

Codesigning uses a **Developer ID Application** cert in the keychain — find its identity
string with `security find-identity -v -p codesigning`, and pass it as
`APPLE_CODESIGN_IDENTITY`.

Notarization has two modes; the **keychain profile is preferred** because no password ever
appears in env, a script, or CI logs. Create it once:

```sh
xcrun notarytool store-credentials vivid-notary \
    --apple-id you@example.com --team-id 7JL9RZ9C8P --password <app-specific-password>
```

then run the pipeline with `NOTARY_PROFILE=vivid-notary`. The fallback is the explicit trio
`APPLE_ID` / `APPLE_TEAM_ID` / `APPLE_APP_PASSWORD` (app-specific password, not the account
password). `SKIP_NOTARIZE=1` signs + DMGs without notarizing, to exercise the signing path.

On a CI runner, store the same as repo secrets (`APPLE_CODESIGN_IDENTITY`, plus either
`NOTARY_PROFILE` on the runner or the explicit trio as secrets). For Sparkle signature
validation, also an EdDSA signing key.

### Running it locally

```sh
cmake -S app -B app/build-release -DCMAKE_BUILD_TYPE=Release && cmake --build app/build-release -j
APPLE_CODESIGN_IDENTITY="Developer ID Application: … (TEAMID)" NOTARY_PROFILE=vivid-notary \
    scripts/release/sign_and_notarize.sh app/build-release/vivid.app build/dist
```

## Wiring real auto-update

`sparkle_bridge.h` is backed by a no-op stub today. To enable updates: embed
`Sparkle.framework` in `Contents/Frameworks/` at build time, replace the stub with a `.mm`
that drives `SPUStandardUpdaterController` (guarded `#ifdef __APPLE__`), and point it at the
published appcast. The stub keeps the dev build free of the framework dependency until then.
