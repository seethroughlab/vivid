#!/usr/bin/env bash
# ============================================================================
# Sign + notarize + DMG a Vivid.app release (P4.5) — UNVERIFIED SCAFFOLD.
#
# This script is NOT exercised in development: it requires an Apple Developer ID
# certificate in the keychain + notarization credentials, which the dev/CI-on-this-
# machine setup does not have. It encodes the standard macOS release flow so a real
# release runner (or a maintainer with a cert) can run it. Treat it as documentation
# that happens to be executable; the credential-free pieces (the gate, the appcast
# generator, version-guard) are what's actually verified.
#
# Required environment:
#   APPLE_CODESIGN_IDENTITY  "Developer ID Application: Name (TEAMID)"
#   APPLE_ID                 Apple ID email
#   APPLE_TEAM_ID            App Store Connect team id
#   APPLE_APP_PASSWORD       app-specific password (NOT the account password)
# Args: $1 = path to the built Vivid.app   $2 = output dir (default: build/dist)
# ============================================================================
set -euo pipefail

APP="${1:?usage: sign_and_notarize.sh <Vivid.app> [out_dir]}"
OUT_DIR="${2:-build/dist}"
: "${APPLE_CODESIGN_IDENTITY:?set APPLE_CODESIGN_IDENTITY}"
: "${APPLE_ID:?set APPLE_ID}"
: "${APPLE_TEAM_ID:?set APPLE_TEAM_ID}"
: "${APPLE_APP_PASSWORD:?set APPLE_APP_PASSWORD}"

APP_NAME="$(basename "$APP" .app)"
mkdir -p "$OUT_DIR"

sign() { codesign --force --options runtime --timestamp --sign "$APPLE_CODESIGN_IDENTITY" "$@"; }

echo "==> codesign (inside-out: frameworks, plugins, then the app)"
# Embedded frameworks (e.g. Sparkle) + loadable operators must be signed before the
# outer bundle, with the hardened runtime, or notarization rejects the app.
find "$APP/Contents/Frameworks" -maxdepth 1 -name "*.framework" -print0 2>/dev/null \
  | while IFS= read -r -d '' fw; do sign "$fw"; done
find "$APP/Contents/PlugIns" -maxdepth 1 \( -name "*.dylib" -o -name "*.bundle" \) -print0 2>/dev/null \
  | while IFS= read -r -d '' p; do sign "$p"; done
sign --deep "$APP"

echo "==> notarize (zip -> notarytool submit --wait -> staple)"
ZIP="$OUT_DIR/$APP_NAME.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" \
  --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" \
  --wait
xcrun stapler staple "$APP"
rm -f "$ZIP"

echo "==> DMG (create, sign, notarize, staple)"
DMG="$OUT_DIR/$APP_NAME.dmg"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -format UDZO -volname "$APP_NAME" -srcfolder "$STAGE" -ov "$DMG"
rm -rf "$STAGE"
codesign --force --timestamp --sign "$APPLE_CODESIGN_IDENTITY" "$DMG"
xcrun notarytool submit "$DMG" \
  --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" \
  --wait
xcrun stapler staple "$DMG"

echo "==> validate"
spctl --assess --type execute -v "$APP"
xcrun stapler validate "$DMG"
echo "done: $DMG"
