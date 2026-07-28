#!/usr/bin/env bash
set -euo pipefail

# Upload one processed showcase clip to S3 (served via CloudFront), with correct content-types and
# cache headers. Uses the AWS CLI (no node/aws-sdk dependency).
#
#   ./site/scripts/upload-showcase-video.sh <slug>
#
# Reads processed/showcase/<slug>/ (from process-showcase-video.sh) and pushes it to
#   s3://$S3_BUCKET/videos/<slug>/ -> https://$CF_DOMAIN/videos/<slug>/index.m3u8
#
# Env (defaults target the Vivid showcase media infra in account 413235161960):
#   S3_BUCKET   default vivid-showcase-media
#   AWS_REGION  default us-east-1
#   CF_DOMAIN   default d2at399jej1hdi.cloudfront.net   (the showcase CloudFront distribution)

SLUG="${1:-}"
[ -n "$SLUG" ] || { echo "Usage: $0 <slug>"; exit 1; }

S3_BUCKET="${S3_BUCKET:-vivid-showcase-media}"
AWS_REGION="${AWS_REGION:-us-east-1}"
CF_DOMAIN="${CF_DOMAIN:-d2at399jej1hdi.cloudfront.net}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOCAL="${HERE}/processed/showcase/${SLUG}"
[ -d "$LOCAL" ] || { echo "Error: '$LOCAL' not found — run process-showcase-video.sh first"; exit 1; }
command -v aws >/dev/null 2>&1 || { echo "Error: aws CLI not found"; exit 1; }

ct_for() { case "$1" in
  *.m3u8) echo "application/vnd.apple.mpegurl";;
  *.ts)   echo "video/mp2t";;
  *.jpg|*.jpeg) echo "image/jpeg";;
  *.png)  echo "image/png";;
  *)      echo "application/octet-stream";; esac; }
# Playlists change on re-encode (short cache); segments + poster are immutable.
cc_for() { case "$1" in
  *.m3u8) echo "public, max-age=3600";;
  *)      echo "public, max-age=31536000, immutable";; esac; }

echo "Uploading ${LOCAL} -> s3://${S3_BUCKET}/videos/${SLUG}/"
( cd "$LOCAL" && find . -type f | sed 's|^\./||' | while read -r rel; do
    ct="$(ct_for "$rel")"; cc="$(cc_for "$rel")"
    aws s3 cp "$rel" "s3://${S3_BUCKET}/videos/${SLUG}/${rel}" \
      --region "$AWS_REGION" --content-type "$ct" --cache-control "$cc" --only-show-errors
    echo "  ✓ videos/${SLUG}/${rel}  (${ct})"
  done )

echo
echo "Done. Clip URL:"
echo "  https://${CF_DOMAIN}/videos/${SLUG}/index.m3u8"
echo "  poster: https://${CF_DOMAIN}/videos/${SLUG}/poster.jpg"
