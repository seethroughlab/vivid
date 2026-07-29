#!/usr/bin/env bash
set -euo pipefail

# Encode a showcase clip to HLS (adaptive) + poster, KEEPING the AV-synced audio track.
#
#   ./site/scripts/process-showcase-video.sh <input.mp4> <slug>
#
# Outputs processed/showcase/<slug>/ :  index.m3u8 (master) + 720p/ 480p/ 360p/ (.m3u8 + .ts) + poster.jpg
# The showcase source clips are 1280x720, so the ladder tops out at the native 720p (no upscaling).
# Mirrors ../seethroughlab.github.io/scripts/process-video.sh, adapted for Vivid's 720p AV clips.
# (processed/ is gitignored — the artifacts are uploaded to S3/CloudFront, not committed.)

if [ $# -ne 2 ]; then
  echo "Usage: $0 <input-video> <slug>"; exit 1
fi

INPUT="$1"; SLUG="$2"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"    # repo root
OUTDIR="${HERE}/processed/showcase/${SLUG}"

[ -f "$INPUT" ] || { echo "Error: input '$INPUT' not found"; exit 1; }
command -v ffmpeg  >/dev/null 2>&1 || { echo "Error: ffmpeg not found";  exit 1; }
command -v ffprobe >/dev/null 2>&1 || { echo "Error: ffprobe not found"; exit 1; }

HAS_AUDIO=$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$INPUT" | head -1 || true)
echo "Processing '$INPUT' -> ${OUTDIR}/  (audio: ${HAS_AUDIO:+yes}${HAS_AUDIO:-none})"

rm -rf "$OUTDIR"; mkdir -p "${OUTDIR}/720p" "${OUTDIR}/480p" "${OUTDIR}/360p"

# Poster: a bright, representative frame (3s in — past the warm-up, mid-phrase).
ffmpeg -y -ss 3 -i "$INPUT" -frames:v 1 -q:v 2 "${OUTDIR}/poster.jpg" 2>/dev/null

# Three renditions in one pass. Scale by height, keep aspect (-2). `-map 0:a?` keeps audio if present
# (shader-edit is silent — video-only HLS is fine). AAC 128k/128k/96k for the AV-synced master audio.
ffmpeg -y -i "$INPUT" \
  -filter_complex "[0:v]split=3[v1][v2][v3]; \
    [v1]scale=-2:720[v1out]; [v2]scale=-2:480[v2out]; [v3]scale=-2:360[v3out]" \
  -map "[v1out]" -map 0:a? \
  -c:v:0 libx264 -b:v:0 3M -maxrate:v:0 3.3M -bufsize:v:0 6M -preset slow -profile:v high -level 4.0 \
  -c:a aac -b:a 128k -ac 2 \
  -hls_time 4 -hls_list_size 0 -hls_segment_type mpegts \
  -hls_segment_filename "${OUTDIR}/720p/seg_%03d.ts" "${OUTDIR}/720p/index.m3u8" \
  -map "[v2out]" -map 0:a? \
  -c:v:0 libx264 -b:v:0 1400k -maxrate:v:0 1540k -bufsize:v:0 2800k -preset slow -profile:v main -level 3.1 \
  -c:a aac -b:a 128k -ac 2 \
  -hls_time 4 -hls_list_size 0 -hls_segment_type mpegts \
  -hls_segment_filename "${OUTDIR}/480p/seg_%03d.ts" "${OUTDIR}/480p/index.m3u8" \
  -map "[v3out]" -map 0:a? \
  -c:v:0 libx264 -b:v:0 700k -maxrate:v:0 770k -bufsize:v:0 1400k -preset slow -profile:v main -level 3.0 \
  -c:a aac -b:a 96k -ac 2 \
  -hls_time 4 -hls_list_size 0 -hls_segment_type mpegts \
  -hls_segment_filename "${OUTDIR}/360p/seg_%03d.ts" "${OUTDIR}/360p/index.m3u8"

# Master playlist with the actual encoded resolutions.
res() { ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$1" | head -1 | tr ',' 'x'; }
R720=$(res "${OUTDIR}/720p/seg_000.ts"); R480=$(res "${OUTDIR}/480p/seg_000.ts"); R360=$(res "${OUTDIR}/360p/seg_000.ts")
cat > "${OUTDIR}/index.m3u8" << MASTER
#EXTM3U
#EXT-X-VERSION:3

#EXT-X-STREAM-INF:BANDWIDTH=3300000,RESOLUTION=${R720}
720p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=1540000,RESOLUTION=${R480}
480p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=770000,RESOLUTION=${R360}
360p/index.m3u8
MASTER

echo "Done: ${OUTDIR}/  (renditions ${R720}, ${R480}, ${R360})"
