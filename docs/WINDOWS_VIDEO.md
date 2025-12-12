# Windows Video Implementation

This document provides an overview of video playback on Windows in Vivid, the challenges we face, and potential improvements.

## Current Architecture

### Decoder Hierarchy

```
VideoPlayer (video_player.cpp)
    │
    ├── HAPDecoder (hap_decoder.cpp)     [Custom MOV parser + Vidvox HAP library]
    │
    ├── MFDecoder (mf_decoder.cpp)       [PRIMARY - Media Foundation]
    │
    └── DShowDecoder (dshow_decoder.cpp) [FALLBACK - DirectShow for ProRes]
```

**Selection Logic:**
1. Check if file is HAP format → HAP decoder (custom MOV parser + hap.c decoder)
2. Try Media Foundation decoder
3. If MF fails AND file is ProRes → Try DirectShow decoder
4. If all fail → Show SMPTE test pattern fallback texture

### Component Overview

| Component | File | Purpose |
|-----------|------|---------|
| `MFDecoder` | `mf_decoder.cpp` | Primary decoder using Media Foundation Source Reader |
| `DShowDecoder` | `dshow_decoder.cpp` | Fallback using DirectShow filter graph for ProRes |
| `AudioPlayer` | `audio_player.cpp` | miniaudio-based ring buffer for audio playback |
| `VideoPlayer` | `video_player.cpp` | High-level operator, decoder selection, fallback handling |

---

## HAP Decoder (HAPDecoder)

### How It Works

The HAP decoder uses a lightweight approach without FFmpeg:

1. **Container Parsing** - Custom MOV parser (`mov_parser.cpp`) handles QuickTime MOV structure
2. **Frame Extraction** - Reads raw HAP frame data by offset/size from the file
3. **Decompression** - Vidvox HAP library (`hap.c`) decompresses Snappy → DXT
4. **GPU Upload** - DXT/BC textures uploaded directly to WebGPU (no pixel conversion!)
5. **Audio Extraction** - PCM audio tracks decoded and synced via AudioPlayer

**Note:** We use a custom MOV parser instead of minimp4 because minimp4 only supports ISO MP4, not QuickTime MOV containers which are standard for HAP files.

### Supported HAP Variants

| Format | WebGPU Texture | Notes |
|--------|----------------|-------|
| HAP (DXT1) | BC1RGBAUnorm | Fastest, no alpha |
| HAP Alpha (DXT5) | BC3RGBAUnorm | Full alpha channel |
| HAP Q (YCoCg DXT5) | BC3RGBAUnorm | Higher quality color |
| HAP Alpha-Only | BC4RUnorm | Single channel |

### Performance

HAP is extremely efficient because:
- No CPU pixel conversion (DXT textures are GPU-native)
- Snappy decompression is fast (~1GB/s)
- Frame data is already block-compressed

Typical decode time: **<1ms** per 1080p frame (vs 5-10ms for H.264)

### Audio Support

HAP files with PCM audio tracks are now fully supported:
- Audio is extracted via the custom MOV parser
- Supports 16-bit, 24-bit, and 32-bit PCM (both big and little endian)
- Audio syncs to video using audio as the master clock
- Pre-buffers ~0.5s before playback starts

### Limitations

- **PCM only** - Compressed audio (AAC, MP3) in HAP files not supported
- **File size** - HAP files are larger than H.264/HEVC (trade-off for decode speed)

---

## Media Foundation Decoder (MFDecoder)

### How It Works

1. **Initialization**
   - COM initialized with `COINIT_MULTITHREADED`
   - Media Foundation initialized via `MFStartup()`
   - Source Reader created from file URL

2. **Video Configuration**
   - Requests RGB32 (BGRA) output format (preferred)
   - Falls back to ARGB32, then RGB24 if needed
   - Hardware transforms enabled via `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`

3. **Audio Configuration**
   - Requests PCM float output: 48kHz, stereo, 32-bit
   - Creates AudioPlayer with ring buffer for playback
   - Pre-buffers ~0.5 seconds of audio before playback starts

4. **Playback Loop** (`update()`)
   - Uses audio playback position as master clock (when audio present)
   - Falls back to wall-clock timing for video-only files
   - Reads video frames and skips up to 5 if video falls behind audio
   - Converts BGRA→RGBA pixel-by-pixel (handles bottom-up DIB format)
   - Uploads to WebGPU texture via `wgpuQueueWriteTexture()`

### Supported Codecs

Media Foundation supports whatever codecs are installed system-wide:
- **H.264** - Built into Windows
- **HEVC/H.265** - Requires HEVC Video Extensions ($0.99 from Microsoft Store)
- **VP9** - VP9 Video Extensions (free)
- **AV1** - AV1 Video Extension (free)

**NOT supported by MF:**
- ProRes (Apple codec)
- HAP (GPU-compressed)
- Some legacy codecs

---

## DirectShow Decoder (DShowDecoder)

### How It Works

1. **Initialization**
   - Creates DirectShow filter graph
   - Adds source filter, sample grabber, null renderer
   - Lets graph builder find appropriate decoder filters

2. **Frame Capture**
   - Uses `ISampleGrabber` with callback
   - Callback receives BGR24 frames asynchronously
   - Frames stored in buffer, picked up by `update()`

3. **When It's Used**
   - Only when MFDecoder fails
   - Only for files detected as ProRes (checks FourCC in file header)

### Codec Detection

```cpp
// Checks first 8KB of file for ProRes FourCC codes:
"apcn" - ProRes 422
"apcs" - ProRes 422 LT
"apco" - ProRes 422 Proxy
"apch" - ProRes 422 HQ
"ap4h" - ProRes 4444
"ap4x" - ProRes 4444 XQ
```

### Requirements

DirectShow needs third-party codec packs:
- **LAV Filters** (recommended) - Open source, includes ProRes
- **K-Lite Codec Pack** - Bundles LAV and others
- **FFDShow** - Legacy option

---

## Audio/Video Synchronization

### Current Approach

Audio is the **master clock**:

```cpp
double targetTime;
if (audioPlayer_ && hasAudio_ && internalAudioEnabled_) {
    targetTime = audioPlayer_->getPlaybackPosition();
} else {
    // Wall-clock fallback
    playbackTime_ += elapsed;
    targetTime = playbackTime_;
}
```

Video frames are decoded until one matches the audio position:
- Skip frames that are too old
- Limit skipping to 5 frames max (prevents infinite loops)
- Upload the frame that best matches audio time

### AudioPlayer Ring Buffer

```
┌─────────────────────────────────────────────┐
│ Ring Buffer (~1 second @ 48kHz stereo)      │
│                                             │
│  writePos ────────► (decoder pushes here)   │
│                                             │
│  readPos ─────────► (miniaudio pulls here)  │
│                                             │
│  samplesPlayed ───► (tracks playback time)  │
└─────────────────────────────────────────────┘
```

- Pre-buffer 0.5s before playback
- Keep buffer topped up with 0.25s during playback
- `getPlaybackPosition()` = `samplesPlayed / sampleRate`

---

## Known Issues

### 1. Pixel Format Conversion Overhead

Every frame requires pixel-by-pixel conversion:
```cpp
for (int y = 0; y < height_; y++) {
    for (int x = 0; x < width_; x++) {
        dst[0] = src[2];  // R <- B (swap)
        dst[1] = src[1];  // G
        dst[2] = src[0];  // B <- R (swap)
        dst[3] = src[3];  // A
    }
}
```

**Impact:** ~1-2ms per 1080p frame on CPU

### 2. Synchronous Frame Reading

`IMFSourceReader::ReadSample()` blocks until a frame is ready. Combined with the conversion and GPU upload, this can cause frame drops at high resolutions.

### 3. Seek Latency

Seeking requires:
1. Setting new position on source reader
2. Flushing audio buffer
3. Re-prebuffering ~0.5s of audio
4. Reading video frames until target time

This can take 100-500ms depending on file format.

### 4. DirectShow Deprecation

`ISampleGrabber` is deprecated and not in modern Windows SDK:
```cpp
// Had to manually define the interface
interface ISampleGrabber : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
    // ... etc
};
```

### 5. DShowDecoder Has No Audio

The DirectShow decoder captures video only. Audio playback is not implemented.

### 6. Code Duplication

Both `MFDecoder` and `DShowDecoder` have:
- Identical texture creation code
- Similar pixel conversion loops
- Duplicate playback state management

---

## Robustness Recommendations

The Windows video system has multiple decoder paths and platform dependencies. Here are recommendations to make it more reliable:

### 1. Defensive Initialization

**COM/MF Initialization**: Both MFDecoder and DShowDecoder rely on COM. Consider:
- Using `CoInitializeEx` with `COINIT_MULTITHREADED` consistently
- Adding `MFStartup`/`MFShutdown` reference counting if multiple decoders are active
- Wrapping initialization in RAII patterns

### 2. Graceful Degradation

**Fallback Chain**: The current HAP → MF → DShow → Test Pattern chain works well. To make it more robust:
- Log which decoder succeeded (helps users understand codec requirements)
- Cache decoder capability checks to avoid repeated failures
- Consider probing file format before attempting decode (avoid expensive MF initialization for HAP files)

### 3. Resource Management

**Texture/Buffer Lifecycle**:
- Ensure textures are released before creating new ones on video size change
- Add null checks before all WebGPU operations
- Use explicit cleanup order: views before textures, buffers before device resources

**Audio Buffer Safety**:
- The ring buffer uses atomic operations but could benefit from explicit memory barriers
- Pre-allocate worst-case audio buffer size to avoid runtime allocation
- Add underrun detection and recovery (insert silence rather than stale data)

### 4. Error Reporting

**Diagnostic Logging**:
- Log HRESULT values with `FormatMessage` for Windows API failures
- Include codec FourCC in error messages
- Track frame decode times to detect performance degradation

### 5. Thread Safety

**Audio/Video Sync**:
- AudioPlayer's ring buffer is accessed from both audio and main threads
- Consider using `std::atomic` for `samplesPlayed_` counter
- Ensure seek operations properly synchronize audio flush and video reset

### 6. File Validation

**Before Opening**:
- Check file exists and is readable
- Validate file size is non-zero
- For HAP: verify MOV container magic bytes before parsing
- For MF: check `IMFSourceReader` attributes before assuming success

---

## Improvement Opportunities

### Short-term Fixes

1. **SIMD Pixel Conversion**
   Use SSE/AVX intrinsics for BGRA→RGBA conversion:
   ```cpp
   // Shuffle mask for BGRA→RGBA: swap R and B channels
   __m128i shuffleMask = _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);
   // Process 4 pixels at once
   ```

2. **Request NV12/YUV Output**
   Media Foundation can output NV12 (GPU-native format). Could skip CPU conversion entirely with a compute shader.

3. **Async Frame Reading**
   Use `MF_SOURCE_READER_CONTROL_ASYNC` flag or a dedicated decode thread to prevent blocking the render loop.

4. **Add Audio to DShowDecoder**
   Query `IBasicAudio` interface for volume control, or capture audio stream alongside video.

### Medium-term Improvements

1. **Unified Decoder Interface**
   Create abstract base class to reduce duplication:
   ```cpp
   class IVideoDecoder {
   public:
       virtual bool open(Context& ctx, const std::string& path, bool loop) = 0;
       virtual void update(Context& ctx) = 0;
       virtual void seek(float seconds) = 0;
       // ... common interface
   };
   ```

2. **Texture Pool**
   Pre-allocate multiple textures and cycle through them to hide upload latency.

3. **GPU Decode Path**
   Investigate D3D11/D3D12 hardware decode surfaces → WebGPU interop. Would eliminate CPU pixel shuffling entirely.

### Long-term Architectural Changes

1. **FFmpeg Backend**
   Replace both MF and DShow with FFmpeg:
   - Single codebase for all platforms
   - All codecs including HAP, ProRes
   - Better seeking behavior
   - Trade-off: ~20MB dependency

2. **Media Foundation Transform Pipeline**
   Use full MFT pipeline instead of Source Reader:
   - More control over decode timing
   - Access to hardware decode surfaces
   - Complex to implement correctly

3. **WMF Hardware Decode → Direct3D → WebGPU**
   Zero-copy path from GPU decoder to WebGPU texture via shared handles.

---

## File Locations

| Purpose | Path |
|---------|------|
| HAP Decoder | `addons/vivid-video/src/hap_decoder.cpp` |
| HAP Library | `addons/vivid-video/src/hap.c`, `hap.h` |
| MOV Parser | `addons/vivid-video/src/mov_parser.cpp` |
| MF Decoder | `addons/vivid-video/src/mf_decoder.cpp` |
| DShow Decoder | `addons/vivid-video/src/dshow_decoder.cpp` |
| Video Player | `addons/vivid-video/src/video_player.cpp` |
| Audio Player | `addons/vivid-video/src/audio_player.cpp` |
| MF Header | `addons/vivid-video/include/vivid/video/mf_decoder.h` |
| DShow Header | `addons/vivid-video/include/vivid/video/dshow_decoder.h` |
| CMakeLists | `addons/vivid-video/CMakeLists.txt` |

---

## Testing

### Required Test Videos

Create sync test videos with:
- Visual flash aligned with audio beep
- Various codecs: H.264, HEVC, ProRes
- Various resolutions: 720p, 1080p, 4K

### Manual Test Checklist

- [ ] HAP video plays (DXT1, DXT5 variants)
- [ ] H.264 plays with sync audio
- [ ] HEVC plays (if extension installed)
- [ ] ProRes falls back to DirectShow (if LAV installed)
- [ ] Unsupported codec shows test pattern
- [ ] Loop restarts cleanly
- [ ] Seek works without audio glitches
- [ ] Pause/resume maintains sync

---

## Dependencies

### Build Dependencies (CMakeLists.txt)

```cmake
# Media Foundation
mf
mfplat
mfreadwrite
mfuuid
ole32
oleaut32
propsys

# DirectShow
strmiids    # GUIDs
quartz      # Runtime
```

### Bundled Libraries

- **mov_parser** - Custom QuickTime MOV parser (supports HAP containers)
- **hap.c** - Vidvox HAP codec (BSD license, uses Snappy)
- **snappy** - Fast compression (fetched via CMake)
- **miniaudio** - Audio output (header-only)

### Runtime Dependencies

- **HEVC Extensions** - For H.265 (optional, Microsoft Store)
- **LAV Filters** - For ProRes (optional, user-installed)
