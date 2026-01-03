# Test Assets

Test assets are not included in the repository. To run asset-dependent tests locally, download the required asset packs.

## Download

Download from: **https://vivid-test-assets.s3.us-east-1.amazonaws.com**

Available packs:
- `vivid-test-audio.zip` (31 MB) - WAV/MP3 audio files
- `vivid-test-materials.zip` (302 MB) - PBR texture sets
- `vivid-test-meshes.zip` (3.2 MB) - glTF models
- `vivid-test-sample_packs.zip` (441 MB) - Audio sample packs for MultiSampler
- `vivid-test-videos.zip` (13 MB) - Test videos (H.264, HAP, ProRes, etc.)

## Installation

Download and extract each zip file into this directory:

```bash
cd tests/assets
curl -O https://vivid-test-assets.s3.us-east-1.amazonaws.com/vivid-test-audio.zip && unzip vivid-test-audio.zip
curl -O https://vivid-test-assets.s3.us-east-1.amazonaws.com/vivid-test-materials.zip && unzip vivid-test-materials.zip
curl -O https://vivid-test-assets.s3.us-east-1.amazonaws.com/vivid-test-meshes.zip && unzip vivid-test-meshes.zip
curl -O https://vivid-test-assets.s3.us-east-1.amazonaws.com/vivid-test-sample_packs.zip && unzip vivid-test-sample_packs.zip
curl -O https://vivid-test-assets.s3.us-east-1.amazonaws.com/vivid-test-videos.zip && unzip vivid-test-videos.zip
```

Expected directory structure:
```
tests/assets/
  audio/
  materials/
  meshes/
  sample_packs/
  videos/
```

## CI Note

Asset-dependent tests are skipped in CI. Run them locally after installing the assets.
