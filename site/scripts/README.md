# Showcase clip pipeline (HLS → S3/CloudFront)

The showcase card videos are **AV-synced clips** (the audio is Vivid making the sound that drives the
picture — the point of the demo). They are **not committed**; they're HLS-encoded and hosted on
S3/CloudFront, mirroring the `../seethroughlab.github.io` media pipeline.

## Flow

```
runner.py --video            examples/demos/showcase/heroes/<id>.mp4   (gitignored source clip)
   │
   ▼  process-showcase-video.sh <mp4> <id>
processed/showcase/<id>/     index.m3u8 + 720p/480p/360p (.m3u8 + .ts) + poster.jpg   (gitignored)
   │
   ▼  upload-showcase-video.sh <id>
s3://vivid-showcase-media/videos/<id>/   →   https://<CF_DOMAIN>/videos/<id>/index.m3u8
   │
   ▼  site/content.json  "showcase_video_base": "https://<CF_DOMAIN>/videos"
site build emits a poster + hls.js <video> player (site/build.py showcase_media_html + SHOWCASE_PLAYER_JS)
```

## Re-shoot + republish

```sh
# 1. (re)generate the clips — with the app running
uv run examples/demos/showcase/runner.py --video --heroes examples/demos/showcase/heroes

# 2. encode + upload each showcase (ids: first-project pulse-song mirror-bridge shader-edit neon-song)
for id in first-project pulse-song mirror-bridge shader-edit neon-song; do
  ./site/scripts/process-showcase-video.sh examples/demos/showcase/heroes/$id.mp4 $id
  ./site/scripts/upload-showcase-video.sh $id
done

# 3. rebuild the site (picks up the CloudFront URLs from content.json)
uv run --project site site/build.py --output site/dist
```

If you re-encode existing slugs, invalidate the CloudFront playlist cache (segments are immutable, but
`index.m3u8` has a 1 h TTL):

```sh
aws cloudfront create-invalidation --distribution-id <DIST_ID> --paths '/videos/*/index.m3u8'
```

## Infrastructure (account 413235161960)

- **S3 bucket** `vivid-showcase-media` (us-east-1), private — read only via the CloudFront OAC.
- **CloudFront** distribution `E2YRMGLQIEW38J` → `d2at399jej1hdi.cloudfront.net`, HTTPS-only,
  SimpleCORS response-headers policy (so hls.js can load cross-origin from the site).

Both `upload-showcase-video.sh` and the site build are parameterized: override `S3_BUCKET` /
`AWS_REGION` / `CF_DOMAIN` (env) and `showcase_video_base` (content.json) to retarget.
