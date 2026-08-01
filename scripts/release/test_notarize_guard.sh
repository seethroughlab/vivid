#!/usr/bin/env bash
# Ph6 audit P1-02 regression test: sign_and_notarize.sh must FAIL LOUD (not silently ship an
# un-notarized DMG) when REQUIRE_NOTARIZE is set but no notarization credentials are present. The
# guard runs before any signing, so this is portable (never reaches macOS-only codesign/notarytool).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/sign_and_notarize.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAKE_APP="$TMP/Vivid.app"
mkdir -p "$FAKE_APP/Contents/MacOS"

fail=0
check() { if [ "$1" = "$2" ]; then echo "ok: $3"; else echo "FAIL: $3 (got '$1', want '$2')" >&2; fail=1; fi; }

# Scenario A: REQUIRE_NOTARIZE set, no creds (and SKIP not set) -> exit non-zero AND name the reason.
out="$(env -u NOTARY_PROFILE -u APPLE_ID -u APPLE_TEAM_ID -u APPLE_APP_PASSWORD -u SKIP_NOTARIZE \
        APPLE_CODESIGN_IDENTITY="dummy" REQUIRE_NOTARIZE=1 \
        bash "$SCRIPT" "$FAKE_APP" "$TMP/out" 2>&1)" && rc=0 || rc=$?
check "$rc" "1" "REQUIRE_NOTARIZE + no creds exits 1 (before any signing)"
case "$out" in
  *"REQUIRE_NOTARIZE is set but notarization is not configured"*) echo "ok: names the missing-notarization reason" ;;
  *) echo "FAIL: expected the REQUIRE_NOTARIZE error message, got:" >&2; echo "$out" >&2; fail=1 ;;
esac

# Scenario B: WITHOUT REQUIRE_NOTARIZE, the guard must NOT fire (the un-notarized path stays allowed
# for local/dev + dispatch-validation builds). It fails later at the real codesign, but the guard's
# message must be absent — proving the guard is scoped to REQUIRE_NOTARIZE only.
out="$(env -u NOTARY_PROFILE -u APPLE_ID -u APPLE_TEAM_ID -u APPLE_APP_PASSWORD -u REQUIRE_NOTARIZE \
        APPLE_CODESIGN_IDENTITY="dummy" SKIP_NOTARIZE=1 \
        bash "$SCRIPT" "$FAKE_APP" "$TMP/out" 2>&1)" || true
case "$out" in
  *"REQUIRE_NOTARIZE is set but notarization is not configured"*)
    echo "FAIL: guard fired without REQUIRE_NOTARIZE set" >&2; fail=1 ;;
  *) echo "ok: guard does not fire when REQUIRE_NOTARIZE is unset" ;;
esac

if [ "$fail" -eq 0 ]; then echo "test_notarize_guard: PASS"; else echo "test_notarize_guard: FAIL" >&2; fi
exit "$fail"
