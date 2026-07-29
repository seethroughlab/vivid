#!/usr/bin/env bash
# upload-showcase-video.sh <id> — push processed/showcase/<id>/ to s3://<bucket>/videos/<id>/ with the
# right content-types + cache headers (segments immutable, playlists short-lived). Env-overridable.
set -euo pipefail
ID="$1"
BUCKET="${S3_BUCKET:-vivid-showcase-media}"
SRC="processed/showcase/$ID"; DST="s3://$BUCKET/videos/$ID"
aws s3 cp "$SRC" "$DST" --recursive --exclude "*" --include "*.ts" \
  --content-type "video/mp2t" --cache-control "public, max-age=31536000, immutable" --only-show-errors
aws s3 cp "$SRC" "$DST" --recursive --exclude "*" --include "*.m3u8" \
  --content-type "application/vnd.apple.mpegurl" --cache-control "public, max-age=3600" --only-show-errors
aws s3 cp "$SRC/poster.jpg" "$DST/poster.jpg" \
  --content-type "image/jpeg" --cache-control "public, max-age=86400" --only-show-errors
echo "uploaded -> $DST"
