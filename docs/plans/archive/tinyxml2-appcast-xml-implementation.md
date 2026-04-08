# Tinyxml2 Appcast XML Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself approve or complete adding TinyXML-2.

## Goal

Replace the regex-based appcast XML parsing inside `AppUpdateManager` with [TinyXML-2](https://leethomason.github.io/tinyxml2/) while preserving the current app update public behavior.

Primary target:

- `src/runtime/platform/app_update_manager.cpp`

Public behavior to preserve:

- `AppUpdateManager::parse_appcast_for_test()`
- `AppUpdateInfo`
- `compare_semver()`-based update selection
- `VIVID_APPCAST_URL`, background worker state, and fetch concurrency behavior

Non-goals:

- Do not change appcast fetching. The fetch migration belongs to [Libcurl HTTP Fetch Implementation Plan](libcurl-http-fetch-implementation.md).
- Do not change Sparkle UI behavior or replace Sparkle's user-facing update flow.
- Do not expose TinyXML-2 types in `app_update_manager.h`.
- Do not add broader XML parsing helpers unless a second caller appears.

## Dependency Integration

Add TinyXML-2 as a pinned dependency in `cmake/dependencies.cmake`.

Preferred integration options:

- Use FetchContent with a pinned TinyXML-2 tag or commit if the upstream CMake target integrates cleanly.
- Otherwise vendor a pinned `tinyxml2.cpp`/`tinyxml2.h` pair under `deps/tinyxml2` and build a small static library target.

Link only the targets that compile `app_update_manager.cpp`, including `test_app_update_manager`. Do not introduce a Homebrew, vcpkg, or other system package dependency for the core build.

When the dependency is actually added, update the dependency manifest in `docs/ARCHITECTURE.md`. The docs-only plan does not need to change that manifest.

## Parser Behavior

Replace only `AppUpdateManager::parse_appcast()` internals.

Implementation outline:

- Parse the input string with `tinyxml2::XMLDocument::Parse`.
- Traverse `rss` -> `channel` -> each `item`.
- For each item, inspect its `enclosure` element.
- Extract the enclosure URL from the literal `url` attribute.
- Extract the version from literal Sparkle-prefixed attributes, preferring `sparkle:shortVersionString` and falling back to `sparkle:version`.
- Extract `sparkle:minimumSystemVersion` from the enclosure when present.
- Extract item text fields from child elements:
  - `title`
  - `pubDate`
  - `sparkle:releaseNotesLink`
- Trim text fields to preserve the current whitespace behavior.

Preserve current selection semantics:

- Ignore items without a usable version.
- Ignore items without an enclosure URL.
- Choose the highest semver item according to the existing `compare_semver()` behavior.
- Set `update_available` with `compare_semver(current_version, best_version) < 0`.
- Keep the existing error string or a close equivalent for no item: `no <item> found in appcast`.
- Keep the existing error string or a close equivalent for no valid version/enclosure: `no valid enclosure/version in appcast`.
- For invalid XML, return `false` with a stable parse error such as `failed to parse appcast XML`.

TinyXML-2 should be private to the parser. Callers should continue to use `parse_appcast_for_test()` and `AppUpdateInfo` without knowing which XML backend is used.

## Migration Steps

1. Add the pinned TinyXML-2 dependency and link the app update manager/test target.
2. Add parser-focused tests first so the current behavior is explicit.
3. Replace the regex-based item and field extraction with TinyXML-2 traversal.
4. Keep `parse_semver_triplet()`, `compare_semver()`, and `trim_copy()` unless the implementation no longer needs `trim_copy()`.
5. Remove now-unused `<regex>` usage only if no other helper in the file needs it.
6. Leave appcast fetching untouched, including `VIVID_APPCAST_URL`, `VIVID_APP_UPDATE_TEST_DELAY_MS`, and worker metrics.

## Testing

Extend `test_app_update_manager` parser cases to cover:

- Attribute order changes in `<enclosure>`.
- Multiline enclosure attributes.
- Sparkle-prefixed fields and attributes:
  - `sparkle:shortVersionString`
  - `sparkle:version`
  - `sparkle:minimumSystemVersion`
  - `sparkle:releaseNotesLink`
- XML entity decoding in title or release notes URL text.
- Multiple item selection still chooses the highest semver version.
- Missing `<item>` returns an error.
- Missing enclosure URL returns an error.
- Missing version returns an error.
- Invalid XML returns an error.
- `update_available` remains false when the current version equals or exceeds the best appcast version.

Keep existing fetch and worker-concurrency tests intact. No network behavior changes are part of this migration.

Verification commands:

```bash
cmake --build build --target test_app_update_manager
ctest --test-dir build --output-on-failure -R "test_app_update_manager"
```

## Acceptance Criteria

- `parse_appcast()` no longer uses regex to parse XML structure.
- `app_update_manager.h` remains unchanged.
- `AppUpdateInfo` fields and update-selection behavior remain compatible with the current tests.
- Appcast fetch behavior remains unchanged and separate from the libcurl plan.
- Tests cover namespace-prefixed Sparkle fields, attribute ordering, invalid XML, missing required fields, and unchanged update availability behavior.
