// Media Foundation Video Decoder for Windows
// Hardware-accelerated video decoding via Source Reader API

#if defined(_WIN32)

#include <vivid/video/mf_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>
#include "pixel_convert.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <comdef.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

namespace vivid::video {

// Helper to check HRESULT and log errors
static bool checkHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::cerr << "[MFDecoder] " << operation << " failed: "
                  << err.ErrorMessage() << " (0x" << std::hex << hr << std::dec << ")\n";
        return false;
    }
    return true;
}

// RAII wrapper for COM initialization
class COMInitializer {
public:
    COMInitializer() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(hr) || hr == S_FALSE;
    }
    ~COMInitializer() {
        if (initialized_) {
            CoUninitialize();
        }
    }
    bool isInitialized() const { return initialized_; }
private:
    bool initialized_ = false;
};

// RAII wrapper for Media Foundation initialization
class MFInitializer {
public:
    MFInitializer() {
        HRESULT hr = MFStartup(MF_VERSION);
        initialized_ = SUCCEEDED(hr);
        if (!initialized_) {
            std::cerr << "[MFDecoder] MFStartup failed\n";
        }
    }
    ~MFInitializer() {
        if (initialized_) {
            MFShutdown();
        }
    }
    bool isInitialized() const { return initialized_; }
private:
    bool initialized_ = false;
};

// Output format for tracking what Media Foundation gives us
enum class OutputFormat {
    BGRA,   // MFVideoFormat_RGB32
    ARGB,   // MFVideoFormat_ARGB32
    RGB24,  // MFVideoFormat_RGB24
    NV12,   // MFVideoFormat_NV12 (GPU conversion)
};

struct MFDecoder::Impl {
    std::unique_ptr<COMInitializer> comInit;
    std::unique_ptr<MFInitializer> mfInit;
    IMFSourceReader* sourceReader = nullptr;
    OutputFormat outputFormat = OutputFormat::BGRA;
    LONG stride = 0;

    std::chrono::steady_clock::time_point lastUpdateTime;

    // Async decode thread members
    std::thread decodeThread;
    std::atomic<bool> stopThread{false};
    std::atomic<bool> seekRequested{false};
    std::atomic<float> seekTime{0.0f};

    // Double-buffered frame data
    std::mutex frameMutex;
    std::condition_variable frameCV;
    std::vector<uint8_t> frameBuffer[2];  // Double buffer
    std::atomic<int> writeBuffer{0};      // Buffer being written by decode thread
    std::atomic<int> readBuffer{-1};      // Buffer ready to be read (-1 = none)
    std::atomic<bool> frameReady{false};
    LONGLONG frameTimestamp{0};           // Timestamp of ready frame
};

MFDecoder::MFDecoder() : impl_(std::make_unique<Impl>()) {
    // Initialize COM and Media Foundation
    impl_->comInit = std::make_unique<COMInitializer>();
    if (!impl_->comInit->isInitialized()) {
        std::cerr << "[MFDecoder] COM initialization failed\n";
        return;
    }

    impl_->mfInit = std::make_unique<MFInitializer>();
    if (!impl_->mfInit->isInitialized()) {
        std::cerr << "[MFDecoder] Media Foundation initialization failed\n";
    }
}

MFDecoder::~MFDecoder() {
    close();
}

bool MFDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    if (!impl_->mfInit || !impl_->mfInit->isInitialized()) {
        std::cerr << "[MFDecoder] Media Foundation not initialized\n";
        return false;
    }

    filePath_ = path;
    isLooping_ = loop;
    device_ = ctx.device();
    queue_ = ctx.queue();

    // Convert path to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    // Create source reader attributes
    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 3);
    if (!checkHR(hr, "MFCreateAttributes")) {
        return false;
    }

    // Enable hardware acceleration
    hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (!checkHR(hr, "SetUINT32(HARDWARE_TRANSFORMS)")) {
        attributes->Release();
        return false;
    }

    // Enable video processing (color conversion)
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (!checkHR(hr, "SetUINT32(VIDEO_PROCESSING)")) {
        attributes->Release();
        return false;
    }

    // Create source reader from URL
    hr = MFCreateSourceReaderFromURL(widePath.c_str(), attributes, &impl_->sourceReader);
    attributes->Release();

    if (!checkHR(hr, "MFCreateSourceReaderFromURL")) {
        return false;
    }

    // Configure output format - try RGB32 first, then fall back
    IMFMediaType* outputType = nullptr;
    hr = MFCreateMediaType(&outputType);
    if (!checkHR(hr, "MFCreateMediaType")) {
        close();
        return false;
    }

    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (!checkHR(hr, "SetGUID(MAJOR_TYPE)")) {
        outputType->Release();
        close();
        return false;
    }

    // Try formats in order of preference
    bool formatSet = false;

    // Try NV12 first - smaller memory bandwidth than RGB32
    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) {
        hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
        if (SUCCEEDED(hr)) {
            formatSet = true;
            impl_->outputFormat = OutputFormat::NV12;
        }
    }

    // Try RGB32 (BGRA) - direct upload without color conversion
    if (!formatSet) {
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) {
            hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
            if (SUCCEEDED(hr)) {
                formatSet = true;
                impl_->outputFormat = OutputFormat::BGRA;
            }
        }
    }

    // Try ARGB32
    if (!formatSet) {
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
        if (SUCCEEDED(hr)) {
            hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
            if (SUCCEEDED(hr)) {
                formatSet = true;
                impl_->outputFormat = OutputFormat::ARGB;
            }
        }
    }

    // Try RGB24
    if (!formatSet) {
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
        if (SUCCEEDED(hr)) {
            hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
            if (SUCCEEDED(hr)) {
                formatSet = true;
                impl_->outputFormat = OutputFormat::RGB24;
            }
        }
    }

    outputType->Release();

    if (!formatSet) {
        std::cerr << "[MFDecoder] No compatible output format found\n";
        close();
        return false;
    }

    // Get actual output format
    IMFMediaType* actualType = nullptr;
    hr = impl_->sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
    if (!checkHR(hr, "GetCurrentMediaType")) {
        close();
        return false;
    }

    // Extract video dimensions
    UINT32 w = 0, h = 0;
    hr = MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &w, &h);
    if (!checkHR(hr, "MFGetAttributeSize(FRAME_SIZE)")) {
        actualType->Release();
        close();
        return false;
    }
    width_ = static_cast<int>(w);
    height_ = static_cast<int>(h);

    // Extract frame rate
    UINT32 numerator = 0, denominator = 1;
    hr = MFGetAttributeRatio(actualType, MF_MT_FRAME_RATE, &numerator, &denominator);
    if (SUCCEEDED(hr) && denominator > 0) {
        frameRate_ = static_cast<float>(numerator) / static_cast<float>(denominator);
    } else {
        frameRate_ = 30.0f;
    }

    // Get stride
    LONG stride = 0;
    hr = actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride));
    if (SUCCEEDED(hr)) {
        impl_->stride = stride;
    } else {
        // Default stride depends on format
        if (impl_->outputFormat == OutputFormat::NV12) {
            impl_->stride = width_;  // Y plane: 1 byte per pixel
        } else if (impl_->outputFormat == OutputFormat::RGB24) {
            impl_->stride = width_ * 3;
        } else {
            impl_->stride = width_ * 4;
        }
    }

    actualType->Release();

    // Get duration
    PROPVARIANT var;
    PropVariantInit(&var);
    hr = impl_->sourceReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var);
    if (SUCCEEDED(hr)) {
        LONGLONG duration100ns = 0;
        PropVariantToInt64(var, &duration100ns);
        duration_ = static_cast<float>(duration100ns) / 10000000.0f;
        PropVariantClear(&var);
    }

    // Check for and configure audio stream
    IMFMediaType* audioType = nullptr;
    hr = impl_->sourceReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &audioType);
    if (SUCCEEDED(hr)) {
        audioType->Release();

        // Configure audio output format - PCM float, 48kHz, stereo
        IMFMediaType* audioOutType = nullptr;
        hr = MFCreateMediaType(&audioOutType);
        if (SUCCEEDED(hr)) {
            audioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            audioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            audioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            audioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
            audioOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
            audioOutType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);  // 2 channels * 4 bytes
            audioOutType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 8);

            hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioOutType);
            audioOutType->Release();

            if (SUCCEEDED(hr)) {
                // Initialize audio player
                audioPlayer_ = std::make_unique<AudioPlayer>();
                if (audioPlayer_->init(48000, 2)) {
                    hasAudio_ = true;
                    audioSampleRate_ = 48000;
                    audioChannels_ = 2;
                    std::cout << "[MFDecoder] Audio: 48000Hz, 2 ch\n";
                } else {
                    std::cerr << "[MFDecoder] Failed to initialize audio player\n";
                    audioPlayer_.reset();
                    hasAudio_ = false;
                }
            } else {
                std::cerr << "[MFDecoder] Failed to set audio output format\n";
                hasAudio_ = false;
            }
        }
    } else {
        hasAudio_ = false;
    }

    // Create GPU texture
    createTexture();

    // Allocate pixel buffer
    pixelBuffer_.resize(width_ * height_ * 4);

    impl_->lastUpdateTime = std::chrono::steady_clock::now();
    isPlaying_ = false;
    isFinished_ = false;
    currentTime_ = 0.0f;
    playbackTime_ = 0.0f;
    nextFrameTime_ = 0.0f;

    // Pre-buffer audio
    if (audioPlayer_ && hasAudio_) {
        prebufferAudio();
    }

    std::cout << "[MFDecoder] Opened " << path
              << " (" << width_ << "x" << height_
              << ", " << frameRate_ << "fps"
              << ", " << duration_ << "s"
              << (hasAudio_ ? ", with audio" : "") << ")\n";

    return true;
}

void MFDecoder::prebufferAudio() {
    if (!audioPlayer_ || !impl_->sourceReader || !hasAudio_) return;

    const uint32_t targetFrames = 48000 / 2;  // ~0.5 seconds

    while (audioPlayer_->getBufferedFrames() < targetFrames) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = impl_->sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        if (FAILED(hr) || !sample) {
            if (sample) sample->Release();
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            sample->Release();
            break;
        }

        // Get buffer from sample
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr) && buffer) {
            BYTE* data = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (SUCCEEDED(hr)) {
                // Data is float samples, interleaved stereo
                uint32_t frameCount = currentLength / (2 * sizeof(float));
                audioPlayer_->pushSamples(reinterpret_cast<float*>(data), frameCount);
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }
}

void MFDecoder::close() {
    if (audioPlayer_) {
        audioPlayer_->shutdown();
        audioPlayer_.reset();
    }

    if (impl_->sourceReader) {
        impl_->sourceReader->Release();
        impl_->sourceReader = nullptr;
    }

    // Clean up NV12 GPU resources
    cleanupNV12Resources();

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
    duration_ = 0.0f;
    frameRate_ = 30.0f;
    isPlaying_ = false;
    isFinished_ = false;
    currentTime_ = 0.0f;
    pixelBuffer_.clear();
    filePath_.clear();
}

bool MFDecoder::isOpen() const {
    return impl_->sourceReader != nullptr;
}

void MFDecoder::createTexture() {
    if (texture_) {
        wgpuTextureViewRelease(textureView_);
        wgpuTextureRelease(texture_);
    }

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = { "MFDecoder Texture", WGPU_STRLEN };
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = { static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = { "MFDecoder TextureView", WGPU_STRLEN };
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

void MFDecoder::createNV12Pipeline() {
    // TODO: Implement GPU compute pipeline for NV12->RGBA conversion
    // For now, NV12 is converted on CPU using SIMD
    useNV12Compute_ = false;
}

void MFDecoder::cleanupNV12Resources() {
    if (nv12BindGroup_) {
        wgpuBindGroupRelease(nv12BindGroup_);
        nv12BindGroup_ = nullptr;
    }
    if (nv12BindGroupLayout_) {
        wgpuBindGroupLayoutRelease(nv12BindGroupLayout_);
        nv12BindGroupLayout_ = nullptr;
    }
    if (nv12Pipeline_) {
        wgpuComputePipelineRelease(nv12Pipeline_);
        nv12Pipeline_ = nullptr;
    }
    if (yTexture_) {
        wgpuTextureRelease(yTexture_);
        yTexture_ = nullptr;
    }
    if (uvTexture_) {
        wgpuTextureRelease(uvTexture_);
        uvTexture_ = nullptr;
    }
    if (outputTexture_) {
        wgpuTextureRelease(outputTexture_);
        outputTexture_ = nullptr;
    }
    useNV12Compute_ = false;
}

void MFDecoder::decodeNV12Sample(void* samplePtr) {
    // TODO: Implement GPU compute path for NV12->RGBA conversion
    // For now, fall back to CPU conversion
    decodeVideoSample(samplePtr);
}

void MFDecoder::resetReader() {
    // Seek back to beginning
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;

    if (impl_->sourceReader) {
        impl_->sourceReader->SetCurrentPosition(GUID_NULL, var);
    }
    PropVariantClear(&var);

    currentTime_ = 0.0f;
    playbackTime_ = 0.0f;
    nextFrameTime_ = 0.0f;
    isFinished_ = false;

    // Re-prebuffer audio
    if (audioPlayer_ && hasAudio_) {
        prebufferAudio();
    }
}

void MFDecoder::readAudioSamplesToBuffer() {
    if (!audioPlayer_ || !impl_->sourceReader || !hasAudio_ || !internalAudioEnabled_) return;

    // Keep audio buffer topped up - target ~0.25 seconds ahead
    const uint32_t targetFrames = 48000 / 4;

    while (audioPlayer_->getBufferedFrames() < targetFrames) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = impl_->sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        if (FAILED(hr) || !sample) {
            if (sample) sample->Release();
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            sample->Release();
            break;
        }

        // Get buffer from sample
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr) && buffer) {
            BYTE* data = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (SUCCEEDED(hr)) {
                // Data is float samples, interleaved stereo
                uint32_t frameCount = currentLength / (2 * sizeof(float));
                audioPlayer_->pushSamples(reinterpret_cast<float*>(data), frameCount);
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }
}

void MFDecoder::decodeVideoSample(void* samplePtr) {
    if (!samplePtr) return;

    IMFSample* sample = static_cast<IMFSample*>(samplePtr);

    // Get buffer from sample
    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        return;
    }

    // Lock buffer
    BYTE* data = nullptr;
    DWORD maxLength = 0, currentLength = 0;
    hr = buffer->Lock(&data, &maxLength, &currentLength);
    if (FAILED(hr)) {
        buffer->Release();
        return;
    }

    // Handle NV12 format specially
    if (impl_->outputFormat == OutputFormat::NV12) {
        // NV12: Y plane is width*height, UV plane follows at half resolution
        LONG yStride = (impl_->stride > 0) ? impl_->stride : width_;
        const uint8_t* yPlane = data;
        const uint8_t* uvPlane = data + yStride * height_;
        LONG uvStride = yStride;  // UV plane has same stride as Y plane

        // Convert NV12 to RGBA
        convertNV12toRGBA_SIMD(yPlane, yStride, uvPlane, uvStride,
                               pixelBuffer_.data(), width_, height_);
    } else {
        // Convert RGB/ARGB/BGRA to RGBA
        LONG absStride = (impl_->stride < 0) ? -impl_->stride : impl_->stride;
        if (absStride == 0) {
            absStride = (impl_->outputFormat == OutputFormat::RGB24) ? width_ * 3 : width_ * 4;
        }

        bool bottomUp = (impl_->stride < 0);

        // Use SIMD-optimized pixel conversion (processes 4 pixels at a time)
        for (int y = 0; y < height_; y++) {
            int srcY = bottomUp ? (height_ - 1 - y) : y;
            const uint8_t* src = data + srcY * absStride;
            uint8_t* dst = pixelBuffer_.data() + y * width_ * 4;

            switch (impl_->outputFormat) {
                case OutputFormat::BGRA:
                    convertRowBGRAtoRGBA(src, dst, width_);
                    break;
                case OutputFormat::ARGB:
                    convertRowARGBtoRGBA(src, dst, width_);
                    break;
                case OutputFormat::RGB24:
                    convertRowRGB24toRGBA(src, dst, width_);
                    break;
                case OutputFormat::NV12:
                    // Already handled above
                    break;
            }
        }
    }

    buffer->Unlock();
    buffer->Release();

    // Upload to GPU
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texture_;
    destination.mipLevel = 0;
    destination.origin = { 0, 0, 0 };
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(width_ * 4);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height_);

    WGPUExtent3D writeSize = { static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1 };

    size_t textureDataSize = static_cast<size_t>(width_) * height_ * 4;

    wgpuQueueWriteTexture(queue_, &destination, pixelBuffer_.data(),
                          textureDataSize, &dataLayout, &writeSize);
}

void MFDecoder::update(Context& ctx) {
    if (!impl_->sourceReader || !isPlaying_) return;

    // Keep audio buffer topped up
    readAudioSamplesToBuffer();

    // Use audio playback position as master clock if audio is available
    // Otherwise fall back to wall-clock timing
    double targetTime;
    if (audioPlayer_ && hasAudio_ && internalAudioEnabled_) {
        targetTime = audioPlayer_->getPlaybackPosition();
    } else {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - impl_->lastUpdateTime).count();
        impl_->lastUpdateTime = now;
        playbackTime_ += elapsed;
        targetTime = playbackTime_;
    }

    // Check if we need a new frame based on target time
    if (targetTime < nextFrameTime_) {
        return;
    }

    // Read video frames until we catch up to target time
    // This handles cases where video falls behind audio
    int framesSkipped = 0;
    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = impl_->sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        if (FAILED(hr)) {
            return;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();

            if (isLooping_) {
                resetReader();
                if (audioPlayer_) {
                    audioPlayer_->flush();
                    audioPlayer_->play();
                }
            } else {
                isFinished_ = true;
                isPlaying_ = false;
            }
            return;
        }

        if (!sample) {
            return;
        }

        // Get frame timestamp
        float frameTime = static_cast<float>(timestamp) / 10000000.0f;
        float nextFrame = frameTime + (1.0f / frameRate_);

        // If this frame's end time is past our target, or we've skipped too many, use it
        if (nextFrame >= targetTime || framesSkipped >= 5) {
            currentTime_ = frameTime;
            nextFrameTime_ = nextFrame;

            // Decode and upload this frame
            decodeVideoSample(sample);
            sample->Release();
            return;
        }

        // Frame is too old, skip it and read next
        sample->Release();
        framesSkipped++;
    }
}

void MFDecoder::seek(float seconds) {
    if (!impl_->sourceReader) return;

    // Convert to 100-nanosecond units
    LONGLONG position = static_cast<LONGLONG>(seconds * 10000000.0f);

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = position;

    impl_->sourceReader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    currentTime_ = seconds;
    playbackTime_ = seconds;
    nextFrameTime_ = seconds;
    isFinished_ = false;

    if (audioPlayer_) {
        audioPlayer_->flush();
        // Re-prebuffer audio after seek
        prebufferAudio();
    }
}

void MFDecoder::pause() {
    isPlaying_ = false;
    if (audioPlayer_) {
        audioPlayer_->pause();
    }
}

void MFDecoder::play() {
    if (isFinished_ && !isLooping_) {
        resetReader();
    }
    isPlaying_ = true;
    impl_->lastUpdateTime = std::chrono::steady_clock::now();

    if (audioPlayer_) {
        audioPlayer_->play();
    }
}

void MFDecoder::setVolume(float volume) {
    if (audioPlayer_) {
        audioPlayer_->setVolume(volume);
    }
}

float MFDecoder::getVolume() const {
    if (audioPlayer_) {
        return audioPlayer_->getVolume();
    }
    return 1.0f;
}

uint32_t MFDecoder::readAudioSamples(float* buffer, uint32_t maxFrames) {
    // TODO: Audio extraction not yet implemented for Windows MFDecoder
    return 0;
}

void MFDecoder::setInternalAudioEnabled(bool enable) {
    internalAudioEnabled_ = enable;
}

bool MFDecoder::isInternalAudioEnabled() const {
    return internalAudioEnabled_;
}

uint32_t MFDecoder::audioSampleRate() const {
    return audioSampleRate_;
}

uint32_t MFDecoder::audioChannels() const {
    return audioChannels_;
}

} // namespace vivid::video

#endif // _WIN32
