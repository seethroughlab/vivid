# Libcurl HTTP Fetch Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself approve adding libcurl.

## Goal

Replace shell-based `curl` fetches with a small in-process HTTP fetch helper built on libcurl's blocking easy interface. Keep the existing asynchronous shape: Vivid already performs catalog and appcast fetches on background threads, so the helper can stay synchronous and simple.

Primary targets:

- `src/runtime/packages/package_catalog.cpp`
- `src/runtime/platform/app_update_manager.cpp`

Non-goals:

- Do not replace IXWebSocket or the control server.
- Do not change catalog JSON parsing or appcast parsing semantics in this step.
- Do not introduce a general async/network framework.

Reference: CMake's `FindCURL` module defines the imported target [`CURL::libcurl`](https://cmake.org/cmake/help/v3.31/module/FindCURL.html) when curl is found.

## Dependency Integration

Add libcurl as a required runtime dependency in `cmake/dependencies.cmake`:

```cmake
find_package(CURL REQUIRED)
```

Then link the executable and runtime test library consumers that compile the fetch helper:

- Link `vivid` against `CURL::libcurl`.
- Link the relevant test target(s) or shared `vivid_runtime_testlib` path against `CURL::libcurl` so `test_package_catalog` and `test_app_update_manager` continue to link.

Prefer the system libcurl on macOS first. If binary distribution later needs a bundled libcurl, make that a separate packaging task rather than mixing it into the code migration.

## Helper Shape

Add a small runtime helper, for example:

- `src/runtime/net/http_fetch.h`
- `src/runtime/net/http_fetch.cpp`

Suggested API:

```cpp
namespace vivid {

struct HttpFetchResult {
    bool ok = false;
    long http_status = 0;          // 0 for non-HTTP schemes such as file://
    std::string body;
    std::string error;             // stable, user-facing enough for diagnostics
};

HttpFetchResult http_get(const std::string& url, long timeout_seconds);

} // namespace vivid
```

Required behavior:

- Use `curl_easy_init`, `curl_easy_setopt`, `curl_easy_perform`, and `curl_easy_cleanup`.
- Store response bytes through `CURLOPT_WRITEFUNCTION` and `CURLOPT_WRITEDATA`.
- Set `CURLOPT_TIMEOUT` to the supplied timeout.
- Set `CURLOPT_FOLLOWLOCATION` to `1L` for HTTP redirects.
- Preserve support for `file://` URLs because `test_app_update_manager` uses a local appcast file via `VIVID_APPCAST_URL`.
- Return `ok=false` with a stable error string on curl initialization or transfer failure.
- For HTTP/HTTPS, treat non-2xx status as failure and include the status in `http_status`.
- Limit response size to 1 MB, matching the package catalog's current runaway-output guard. If the limit is exceeded, fail with an explicit error rather than accumulating unbounded data.

Callers should not see curl-specific handles or error codes.

## Migration Steps

1. Add the helper and CMake linkage.
2. Migrate `PackageCatalog::fetch_thread_fn()` first:
   - Remove the local shell `quote()` helper if it becomes unused.
   - Preserve cache-first behavior.
   - Preserve `VIVID_SKIP_PACKAGE_CATALOG_NETWORK`.
   - Preserve deterministic fallback URL order from `catalog_urls()`.
   - Preserve `VIVID_PACKAGE_CATALOG_URL` override behavior.
   - Preserve "first successful parse wins."
   - Keep `parse_index_json()` unchanged.
   - Keep the existing error policy: if remote fetch fails but cache exists, keep cached entries and mark state ready; if no cache exists, mark state error.
3. Migrate `AppUpdateManager::fetch_thread_fn()`:
   - Replace the `posix_spawnp("curl", ...)` block with `http_get(appcast_url(), 10)`.
   - Preserve `VIVID_APPCAST_URL`.
   - Preserve `VIVID_APP_UPDATE_TEST_DELAY_MS` and worker concurrency metrics.
   - Keep `parse_appcast()` unchanged for this libcurl step.
   - Preserve the existing state transition and error handling behavior.
4. Remove now-unused process/spawn includes from migrated files.

## Testing

Update or add tests around existing suites:

- `test_package_catalog`
  - Disabled network with no cache returns an error.
  - Disabled network with cache keeps cached entries and marks ready.
  - `VIVID_PACKAGE_CATALOG_URL` with an embedded single quote no longer exercises shell quoting; update the regression to assert graceful fetch failure or parse failure without shell involvement.
  - Fallback URL order remains deterministic; this may be covered with a helper seam or by keeping `catalog_urls()` directly testable.
- `test_app_update_manager`
  - `file://` appcast URL still fetches and parses.
  - Failed appcast fetch sets `Error` with a useful message.
  - Repeated refresh calls still avoid concurrent workers; `max_concurrent_workers_for_test()` remains 1.
  - Existing `parse_appcast_for_test()` tests remain unchanged.

Verification commands:

```bash
cmake --build build --target test_package_catalog test_app_update_manager
ctest --test-dir build --output-on-failure -R "test_package_catalog|test_app_update_manager"
```

## Acceptance Criteria

- No runtime code shells out to `curl`.
- Catalog fetch behavior, cache fallback, environment overrides, and appcast update checks remain externally unchanged.
- Tests cover the former shell-quoting regression without relying on shell behavior.
- The helper is narrow enough that future fetch users do not need to know libcurl directly.

