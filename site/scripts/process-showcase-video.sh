#!/usr/bin/env bash
# process-showcase-video.sh <mp4> <id> — encode an AV clip to HLS (720/480/360 + master + poster),
# keeping the AAC audio, into processed/showcase/<id>/. Recreated to match the pre-existing layout.
set -euo pipefail
MP4="$1"; ID="$2"
OUT="processed/showcase/$ID"
rm -rf "$OUT"; mkdir -p "$OUT/720p" "$OUT/480p" "$OUT/360p"
enc() {  # <height> <v_bitrate> <maxrate> <bufsize> <dir>
  ffmpeg -y -i "$MP4" -vf "scale=-2:$1" -c:v libx264 -profile:v main -preset veryfast -pix_fmt yuv420p \
    -b:v "$2" -maxrate "$3" -bufsize "$4" -c:a aac -b:a 128k -ac 2 \
    -f hls -hls_time 6 -hls_playlist_type vod -hls_segment_filename "$OUT/$5/seg_%03d.ts" \
    "$OUT/$5/index.m3u8" -loglevel error
}
enc 720 3000k 3300k 6600k 720p
enc 480 1400k 1540k 3080k 480p
enc 360 700k  770k  1540k 360p
cat > "$OUT/index.m3u8" <<EOF
#EXTM3U
#EXT-X-VERSION:3

#EXT-X-STREAM-INF:BANDWIDTH=3300000,RESOLUTION=1280x720
720p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=1540000,RESOLUTION=854x480
480p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=770000,RESOLUTION=640x360
360p/index.m3u8
EOF
ffmpeg -y -ss 3 -i "$MP4" -vf "scale=-2:720" -vframes 1 -q:v 3 "$OUT/poster.jpg" -loglevel error
echo "processed -> $OUT ($(find "$OUT" -name '*.ts' | wc -l | tr -d ' ') segments)"
