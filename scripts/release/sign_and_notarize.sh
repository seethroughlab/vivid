#!/usr/bin/env bash
# ============================================================================
# Sign + notarize + DMG a Vivid.app release (P4.5).
#
# Codesigning uses a Developer ID Application cert in the keychain (its private key —
# macOS may prompt to allow access). Notarization needs Apple credentials; the preferred,
# password-free path is a notarytool KEYCHAIN PROFILE you create once:
#
#   xcrun notarytool store-credentials vivid-notary \
#       --apple-id you@example.com --team-id 7JL9RZ9C8P --password <app-specific-password>
#
# then run with NOTARY_PROFILE=vivid-notary. If no notarization credentials are present
# (or SKIP_NOTARIZE=1), the script still signs + builds a DMG and skips notarize/staple,
# so the signing pipeline can be exercised without an app-specific password.
#
# Env:
#   APPLE_CODESIGN_IDENTITY   (required) e.g. "Developer ID Application: Name (TEAMID)"
#   NOTARY_PROFILE            notarytool keychain-profile name (preferred), OR
#   APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD   (the explicit-credential fallback)
#   SKIP_NOTARIZE=1           sign + DMG only
# Args: $1 = path to Vivid.app   $2 = output dir (default build/dist)
# ============================================================================
set -euo pipefail

APP="${1:?usage: sign_and_notarize.sh <Vivid.app> [out_dir]}"
OUT_DIR="${2:-build/dist}"
: "${APPLE_CODESIGN_IDENTITY:?set APPLE_CODESIGN_IDENTITY (see: security find-identity -v -p codesigning)}"
[ -d "$APP" ] || { echo "error: not a bundle: $APP" >&2; exit 1; }

APP_NAME="$(basename "$APP" .app)"
mkdir -p "$OUT_DIR"

# Decide the notarization mode up front so we can report it + gate stapling.
NOTARIZE="none"
if [ "${SKIP_NOTARIZE:-0}" != "1" ]; then
  if [ -n "${NOTARY_PROFILE:-}" ]; then
    NOTARIZE="profile"
  elif [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
    NOTARIZE="creds"
  fi
fi
echo "==> identity: $APPLE_CODESIGN_IDENTITY"
echo "==> notarization: $NOTARIZE"

sign() { codesign --force --options runtime --timestamp --sign "$APPLE_CODESIGN_IDENTITY" "$@"; }

echo "==> codesign (inside-out: nested dylibs/plugins/frameworks, then the app)"
# Sign every nested Mach-O before the outer bundle, each with the hardened runtime +
# a secure timestamp, or notarization rejects the app. Explicit (not --deep, which Apple
# discourages for signing).
while IFS= read -r -d '' lib; do sign "$lib"; done < <(
  find "$APP/Contents/MacOS" "$APP/Contents/PlugIns" -type f \
    \( -name "*.dylib" -o -name "*.so" -o -name "*.bundle" \) -print0 2>/dev/null)
while IFS= read -r -d '' fw; do sign "$fw"; done < <(
  find "$APP/Contents/Frameworks" -maxdepth 1 -name "*.framework" -print0 2>/dev/null)
sign "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

notarize() {  # $1 = artifact to submit (zip or dmg)
  case "$NOTARIZE" in
    profile) xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait ;;
    creds)   xcrun notarytool submit "$1" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
                    --password "$APPLE_APP_PASSWORD" --wait ;;
  esac
}

if [ "$NOTARIZE" != "none" ]; then
  echo "==> notarize the app (zip -> notarytool --wait -> staple)"
  ZIP="$OUT_DIR/$APP_NAME.zip"
  ditto -c -k --keepParent "$APP" "$ZIP"
  notarize "$ZIP"
  xcrun stapler staple "$APP"
  rm -f "$ZIP"
else
  echo "==> skipping notarization (no credentials / SKIP_NOTARIZE) — DMG will be signed but not notarized"
fi

echo "==> DMG (create, sign$( [ "$NOTARIZE" != none ] && echo ", notarize, staple"))"
DMG="$OUT_DIR/$APP_NAME.dmg"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -format UDZO -volname "$APP_NAME" -srcfolder "$STAGE" -ov "$DMG"
rm -rf "$STAGE"
codesign --force --timestamp --sign "$APPLE_CODESIGN_IDENTITY" "$DMG"
if [ "$NOTARIZE" != "none" ]; then
  notarize "$DMG"
  xcrun stapler staple "$DMG"
fi

echo "==> validate"
codesign --verify --deep --strict --verbose=2 "$APP"
if [ "$NOTARIZE" != "none" ]; then
  spctl --assess --type execute -vv "$APP"      # Gatekeeper: requires notarization
  xcrun stapler validate "$DMG"
else
  echo "    (un-notarized: skipping spctl/stapler validate — Gatekeeper would reject on another Mac)"
fi
echo "done: $DMG"
