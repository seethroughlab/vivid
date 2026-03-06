# Release Checklist (Milestone 6)

This is the operational checklist to ship a macOS release for Vivid with signed/notarized artifacts and core update metadata.

## A. Repo State (must pass before tagging)

- [ ] All Milestone 6 files are committed on `master`.
- [ ] `git status --short` is clean (or only intentionally unrelated changes).
- [ ] `CMakeLists.txt` version is final (`project(vivid VERSION X.Y.Z)`).
- [ ] Version surfaces match:
  - `src/runtime/main.cpp` fallback `VIVID_CORE_VERSION`
  - `src/ui/node_graph_draw.cpp` fallback `VIVID_CORE_VERSION`

Commands:

```bash
git status --short
gh auth status
```

## B. GitHub Configuration

- [ ] Release workflow exists: `.github/workflows/release-macos.yml`
- [ ] Validation workflow exists: `.github/workflows/release-macos-validate.yml`
- [ ] Version guard workflow exists: `.github/workflows/version-guard.yml`
- [ ] Required secrets are configured in GitHub repo:
  - `APPLE_CERT_P12_B64`
  - `APPLE_CERT_PASSWORD`
  - `APPLE_CODESIGN_IDENTITY`
  - `APPLE_ID`
  - `APPLE_TEAM_ID`
  - `APPLE_APP_PASSWORD`
  - `VIVID_SPARKLE_PUBLIC_KEY`

Command:

```bash
gh secret list
```

## C. Local Build/Test Preflight

- [ ] Configure/build succeeds
- [ ] Update-related tests pass
- [ ] Control server test passes
- [ ] CLI update check works (using local appcast override if DNS is unavailable)

Commands:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target vivid test_app_update_manager test_control_server -j8
ctest --test-dir build -R "test_app_update_manager|test_package_update_logic|test_control_server" --output-on-failure
```

Local appcast sanity:

```bash
python3 scripts/release/generate_appcast.py \
  --version 0.1.1 \
  --url https://example.com/Vivid-0.1.1.zip \
  --length 123 \
  --title Vivid \
  --output /tmp/vivid_test_appcast.xml

VIVID_APPCAST_URL=file:///tmp/vivid_test_appcast.xml ./build/vivid check-core-updates --force
```

## D. Pages/Appcast Readiness

- [ ] `catalog/appcast.xml` exists in repo
- [ ] Pages workflow green
- [ ] Live appcast URL responds:
  - `https://vivid.seethroughlab.com/appcast.xml`

Command:

```bash
curl -I -L --max-time 20 https://vivid.seethroughlab.com/appcast.xml
```

## E. Validation (No Tag)

- [ ] Run `release-macos-validate.yml` on `master` (or target ref)
- [ ] Validate run succeeds through notarize/staple/verify
- [ ] Download workflow artifact and sanity launch locally
- [ ] Confirm no GitHub Release was created
- [ ] Confirm `catalog/appcast.xml` was not changed by validation run

## F. Publish Rolling-Alpha Checkpoint

- [ ] Commit + push final release changes
- [ ] Create intentional public tag `vX.Y.Z` (no CI-only retry tags)
- [ ] Push tag
- [ ] Watch release workflow to completion
- [ ] Confirm GitHub Release has:
  - `Vivid-X.Y.Z-macos-arm64.zip`
  - `appcast.xml`

Commands:

```bash
git tag vX.Y.Z
git push origin vX.Y.Z
gh run list --workflow "Release macOS" --limit 5
gh run watch <run-id>
```

## G. Post-Release Validation

- [ ] Download release zip on a clean macOS machine
- [ ] Launch app; Gatekeeper acceptance is clean
- [ ] In-app `Check for Updates...` works
- [ ] Startup update check is non-blocking and no crash on offline/error path

## H. Rollback / Reissue

- [ ] If release is bad, cut a new patch release `vX.Y.(Z+1)` (do not rewrite existing tag)
- [ ] Use validation workflow before re-publishing
- [ ] Re-run publish workflow via new tag
- [ ] Verify `catalog/appcast.xml` points to the latest good artifact
