# PLAN-05: Media Pipeline Architecture

This document describes the architecture for video playback, camera input, and HAP codec support in Vivid.

## Design Decisions

### Platform-Native Video Decoding

After evaluating FFmpeg-only vs platform-native approaches, we chose a **hybrid platform-native architecture** for these reasons:

1. **Performance**: Zero-copy GPU paths (AVFoundation → Metal → WebGPU on macOS)
2. **Hardware Acceleration**: Automatic on each platform (no manual setup)
3. **Power Efficiency**: Uses dedicated decode silicon
4. **Smaller Distribution**: No need to bundle FFmpeg for common formats
5. **Licensing Simplicity**: No LGPL concerns for standard codecs

This is the approach used by professional creative tools: TouchDesigner, Resolume, openFrameworks, VDMX.

### HAP Codec Support

HAP is critical for live visual/VJ workflows. It uses GPU-compressed textures (DXT/BC format):

```
Traditional: Compressed → CPU decode → Upload to GPU
HAP:         Compressed → Snappy decompress → Upload DXT → GPU decompresses
```

HAP requires FFmpeg for demuxing (extracting frames from MOV/AVI containers) but we **bypass** FFmpeg's decoder and upload DXT data directly to the GPU.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                       VideoLoader                                │
│                    (Platform Agnostic API)                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Standard Codecs (H.264, H.265, ProRes, VP9)                   │
│  ┌─────────────────┬─────────────────┬─────────────────┐       │
│  │     macOS       │     Windows     │      Linux      │       │
│  │  AVFoundation   │ Media Foundation│     FFmpeg      │       │
│  │  (zero-copy)    │  (HW decode)    │    (SW/HW)      │       │
│  └─────────────────┴─────────────────┴─────────────────┘       │
│                                                                 │
│  HAP Codec (All Platforms - Shared Implementation)              │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  FFmpeg demux → Snappy decompress → DXT GPU upload  │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## VideoLoader Interface

```cpp
// runtime/src/video_loader.h
namespace vivid {

enum class VideoCodecType {
    Standard,   // H.264, H.265, ProRes, VP9, etc.
    HAP,        // HAP (DXT1)
    HAPAlpha,   // HAP Alpha (DXT5)
    HAPQ,       // HAP Q (Scaled DXT5)
    HAPQAlpha   // HAP Q Alpha
};

struct VideoInfo {
    int width = 0;
    int height = 0;
    double duration = 0.0;      // seconds
    double frameRate = 0.0;     // fps
    int64_t frameCount = 0;
    VideoCodecType codecType = VideoCodecType::Standard;
    bool hasAudio = false;
};

class VideoLoader {
public:
    virtual ~VideoLoader() = default;

    // Lifecycle
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Metadata
    virtual const VideoInfo& info() const = 0;

    // Playback control
    virtual bool seek(double timeSeconds) = 0;
    virtual bool seekToFrame(int64_t frameNumber) = 0;

    // Frame retrieval
    // Returns true if a new frame is available
    virtual bool getFrame(Texture& output, Context& ctx) = 0;

    // HAP-specific: get raw DXT data for direct GPU upload
    virtual bool isHAP() const { return false; }

    // Factory method - creates platform-appropriate loader
    static std::unique_ptr<VideoLoader> create();
};

} // namespace vivid
```

---

## Platform Implementations

### macOS: AVFoundation

**File**: `runtime/src/video_loader_macos.mm`

**Formats Supported**:
- H.264/AVC (all profiles up to 5.1)
- H.265/HEVC
- ProRes (all variants)
- ProRes RAW
- MP4, MOV, M4V containers

**Key Features**:
- Zero-copy via IOSurface → Metal → WebGPU
- Hardware acceleration automatic
- Best frame-accurate seeking for modern codecs
- AVSampleCursor for precise navigation

**Dependencies**:
```cmake
target_link_libraries(vivid-runtime PRIVATE
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework VideoToolbox"
)
```

### Windows: Media Foundation

**File**: `runtime/src/video_loader_windows.cpp`

**Formats Supported**:
- H.264/AVC
- H.265/HEVC (Windows 10+)
- VP9 (via codec extension)
- AV1 (via codec extension)
- MP4 container (limited MOV support)

**Key Features**:
- Hardware acceleration via MFTs
- Direct3D 11 texture output
- Source Reader for sample-accurate seeking

**Dependencies**:
- Built-in Windows SDK (no external deps)

### Linux: FFmpeg

**File**: `runtime/src/video_loader_linux.cpp`

**Formats Supported**:
- All FFmpeg-supported formats
- H.264, H.265, VP8, VP9, AV1, ProRes, etc.
- All container formats

**Key Features**:
- Software decode (CPU)
- Optional hardware: VAAPI, VDPAU, NVDEC
- Maximum format compatibility

**Dependencies**:
- libavformat, libavcodec, libswscale, libavutil
- System FFmpeg or bundled

---

## HAP Implementation

**File**: `runtime/src/hap_decoder.cpp` (shared across all platforms)

HAP uses FFmpeg **only for demuxing** (container parsing), not decoding:

```cpp
class HAPDecoder {
public:
    bool open(const std::string& path);
    void close();

    // Get next HAP frame as raw DXT data
    // Returns DXT1 (BC1), DXT5 (BC3), or Scaled DXT5 depending on HAP variant
    bool getNextFrame(std::vector<uint8_t>& dxtData, int& width, int& height);

    // Upload DXT data directly to GPU texture
    bool uploadToTexture(Texture& output, Context& ctx,
                         const uint8_t* dxtData, int width, int height);

    VideoCodecType hapType() const;

private:
    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIndex_ = -1;

    // Snappy decompression
    std::vector<uint8_t> decompressBuffer_;
};
```

**DXT Format Mapping**:

| HAP Type | DXT Format | WebGPU Format |
|----------|------------|---------------|
| HAP | DXT1 | BC1RGBAUnorm |
| HAP Alpha | DXT5 | BC3RGBAUnorm |
| HAP Q | Scaled DXT5 | BC3RGBAUnorm (with shader scale) |
| HAP Q Alpha | Scaled DXT5 | BC3RGBAUnorm (with shader scale) |

---

## Dependencies

### Core Dependencies

```cmake
# CMakeLists.txt additions for video support

# Snappy (for HAP decompression) - all platforms
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.1.10
)
FetchContent_MakeAvailable(snappy)

# FFmpeg - for HAP demuxing (all platforms) and Linux decode
if(UNIX AND NOT APPLE)
    # Linux: full FFmpeg for decode
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat libavcodec libswscale libavutil)
else()
    # macOS/Windows: FFmpeg only for HAP demuxing (lighter config)
    # Can use system FFmpeg or FetchContent
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG libavformat libavutil)
    endif()
endif()
```

### Platform Framework Links

```cmake
if(APPLE)
    target_link_libraries(vivid-runtime PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
    )
elseif(WIN32)
    target_link_libraries(vivid-runtime PRIVATE
        mfplat mfreadwrite mfuuid
    )
endif()
```

---

## VideoFile Operator

```cpp
// operators/videofile.cpp
class VideoFile : public Operator {
public:
    // Fluent API
    VideoFile& path(const std::string& p);
    VideoFile& file(const std::string& p);  // alias
    VideoFile& loop(bool enabled = true);
    VideoFile& speed(float s);
    VideoFile& play();
    VideoFile& pause();
    VideoFile& toggle();
    VideoFile& seek(float timeSeconds);
    VideoFile& seekFrame(int64_t frame);

    void init(Context& ctx) override;
    void process(Context& ctx) override;

    std::vector<ParamDecl> params() override {
        return {
            stringParam("path", path_),
            boolParam("loop", loop_),
            floatParam("speed", speed_, -4.0f, 4.0f),
            floatParam("seek", seekTarget_, 0.0f, 1.0f)  // normalized 0-1
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::unique_ptr<VideoLoader> loader_;
    Texture output_;

    std::string path_;
    double playhead_ = 0.0;
    float speed_ = 1.0f;
    float seekTarget_ = 0.0f;
    bool playing_ = true;
    bool loop_ = true;
    bool needsSeek_ = false;

    // File change detection (like ImageFile)
    time_t lastMtime_ = 0;
    bool checkFileChanged();
};
```

---

## Format Support Matrix

| Format | macOS | Windows | Linux |
|--------|-------|---------|-------|
| H.264/MP4 | AVFoundation | Media Foundation | FFmpeg |
| H.265/HEVC | AVFoundation | Media Foundation | FFmpeg |
| ProRes | AVFoundation | FFmpeg fallback | FFmpeg |
| VP9/WebM | FFmpeg fallback | Media Foundation | FFmpeg |
| AV1 | FFmpeg fallback | Media Foundation | FFmpeg |
| **HAP** | FFmpeg demux + custom | FFmpeg demux + custom | FFmpeg demux + custom |
| MOV | AVFoundation | Limited | FFmpeg |

---

## Implementation Phases

### Phase 12.2a: Core Infrastructure ✅ COMPLETE
- [x] Create `VideoLoader` interface (`runtime/src/video_loader.h`)
- [x] Add Snappy dependency to CMakeLists.txt
- [x] Add platform framework links to CMakeLists.txt (AVFoundation, Media Foundation)
- [x] Factory method for platform-appropriate loader (`VideoLoader::create()`)

### Phase 12.2b: macOS Implementation ✅ COMPLETE
- [x] Implement `VideoLoaderMacOS` (`runtime/src/video_loader_macos.mm`)
- [x] AVAssetReader setup and frame extraction
- [x] CVPixelBuffer → Texture upload (BGRA to RGBA conversion)
- [x] Hardware-accelerated decode (automatic via VideoToolbox)
- [x] Frame-accurate seeking (via reader recreation with time range)
- [x] Frame rate limiting (decode only at video's native framerate)
- [x] Test with H.264 (avc1 codec tested successfully)
- [ ] Test with ProRes, HEVC
- [ ] Zero-copy via IOSurface (optimization for later)

### Phase 12.2c: HAP Support ✅ COMPLETE
- [x] Add FFmpeg for container demuxing and HAP decoding
- [x] Implement `HAPDecoder` (`runtime/src/hap_decoder.cpp`)
- [x] FFmpeg's built-in HAP decoder with swscale conversion
- [x] Auto-detection of HAP codec in video files
- [x] Test: HAP playback (Hap1 format tested at 1920x1080@60fps)

### Phase 12.2d: VideoFile Operator ✅ COMPLETE
- [x] Implement `VideoFile` operator (`operators/videofile.cpp`)
- [x] Playback controls (play, pause, seek, loop, speed)
- [x] File change detection for hot-reload
- [x] Integration with graph system
- [x] Fluent API: `.path()`, `.loop()`, `.speed()`, `.play()`, `.pause()`, `.seek()`

### Phase 12.2e: Windows Implementation
- [ ] Implement `VideoLoaderWindows` (`runtime/src/video_loader_windows.cpp`)
- [ ] Media Foundation Source Reader setup
- [ ] Hardware decode configuration
- [ ] D3D11 → WebGPU texture path

### Phase 12.2f: Linux Implementation
- [ ] Implement `VideoLoaderLinux` (`runtime/src/video_loader_linux.cpp`)
- [ ] Full FFmpeg decode path
- [ ] Optional hardware acceleration (VAAPI/VDPAU)

---

## Testing Checklist

- [x] Load and play H.264 MP4 file (macOS verified)
- [ ] Load and play ProRes MOV file (macOS)
- [x] Load and play HAP MOV file (all platforms via FFmpeg)
- [x] Seek to specific time
- [x] Seek to specific frame
- [x] Loop playback
- [x] Variable speed playback (0.5x, 2x)
- [ ] Reverse playback
- [ ] Multiple simultaneous videos
- [x] Hot-reload on file change
- [x] Memory stability (no leaks during playback)
- [x] Performance: 1080p60 HAP playback verified

---

## References

- [AVFoundation Programming Guide](https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/AVFoundationPG/)
- [Media Foundation Documentation](https://docs.microsoft.com/en-us/windows/win32/medfound/)
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- [HAP Codec Specification](https://github.com/Vidvox/hap)
- [Snappy Compression](https://github.com/google/snappy)
