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
#include <deque>

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

// Timestamped audio chunk for A/V sync
struct AudioChunk {
    std::vector<float> samples;
    double pts;  // Presentation timestamp in seconds
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

    // A/V sync: timestamped audio chunks for chain routing
    std::deque<AudioChunk> audioChunks;
    std::mutex audioChunksMutex;
    double audioBufferHeadPTS = 0.0;  // PTS of next sample to be read
    double audioBufferTailPTS = 0.0;  // PTS of last sample added

    // A/V sync: video time communication (atomic for thread-safe access from audio thread)
    std::atomic<double> lastVideoTimeSeconds{-1.0};  // -1 = not yet set (startup)
    std::atomic<bool> videoHasStarted{false};  // True after first video frame decoded
    std::atomic<bool> initialSyncDone{false};  // True after initial A/V alignment
    std::atomic<bool> isShuttingDown{false};  // True during cleanup
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
    // Signal shutdown to prevent audio thread from accessing resources
    impl_->isShuttingDown.store(true);

    if (audioPlayer_) {
        audioPlayer_->shutdown();
        audioPlayer_.reset();
    }

    // Clear audio sync state
    {
        std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);
        impl_->audioChunks.clear();
    }
    impl_->audioBufferHeadPTS = 0.0;
    impl_->audioBufferTailPTS = 0.0;
    impl_->lastVideoTimeSeconds.store(-1.0);
    impl_->videoHasStarted.store(false);
    impl_->initialSyncDone.store(false);

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
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
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
    if (!impl_->sourceReader || !hasAudio_) return;

    // Keep audio buffer topped up - target ~0.5 seconds ahead
    const uint32_t targetFrames = 48000 / 2;

    // Check if we have enough in the audio player (for internal playback) or chunk queue (for chain routing)
    size_t totalChunkSamples = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);
        for (const auto& c : impl_->audioChunks) {
            totalChunkSamples += c.samples.size();
        }
    }
    uint32_t chunkFrames = static_cast<uint32_t>(totalChunkSamples / 2);

    bool needMoreForPlayer = internalAudioEnabled_ && audioPlayer_ && audioPlayer_->getBufferedFrames() < targetFrames;
    bool needMoreForChain = !internalAudioEnabled_ && chunkFrames < targetFrames;

    if (!needMoreForPlayer && !needMoreForChain) return;

    while (true) {
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
                float* floatData = reinterpret_cast<float*>(data);

                // For internal audio playback
                if (internalAudioEnabled_ && audioPlayer_) {
                    audioPlayer_->pushSamples(floatData, frameCount);
                }

                // For chain audio routing - add to timestamped chunk queue
                {
                    std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);
                    AudioChunk chunk;
                    chunk.pts = static_cast<double>(timestamp) / 10000000.0;  // 100ns to seconds
                    chunk.samples.assign(floatData, floatData + frameCount * 2);

                    // Update tail PTS
                    double chunkDurationSec = static_cast<double>(frameCount) / 48000.0;
                    impl_->audioBufferTailPTS = chunk.pts + chunkDurationSec;

                    // If first chunk, set head PTS too
                    if (impl_->audioChunks.empty()) {
                        impl_->audioBufferHeadPTS = chunk.pts;
                    }

                    impl_->audioChunks.push_back(std::move(chunk));

                    // Limit buffer to ~2 seconds
                    size_t totalSamples = 0;
                    for (const auto& c : impl_->audioChunks) {
                        totalSamples += c.samples.size();
                    }
                    size_t maxSamples = 48000 * 2 * 2;  // 2 seconds * 2 channels
                    while (totalSamples > maxSamples && impl_->audioChunks.size() > 1) {
                        totalSamples -= impl_->audioChunks.front().samples.size();
                        impl_->audioChunks.pop_front();
                        if (!impl_->audioChunks.empty()) {
                            impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
                        }
                    }
                }

                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();

        // Check if we have enough now
        if (internalAudioEnabled_ && audioPlayer_) {
            if (audioPlayer_->getBufferedFrames() >= targetFrames) break;
        } else {
            std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);
            size_t total = 0;
            for (const auto& c : impl_->audioChunks) {
                total += c.samples.size();
            }
            if (total / 2 >= targetFrames) break;
        }
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

    // Publish current video time for A/V sync
    impl_->lastVideoTimeSeconds.store(targetTime);
    impl_->videoHasStarted.store(true);

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

    // Reset A/V sync state
    {
        std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);
        impl_->audioChunks.clear();
    }
    impl_->audioBufferHeadPTS = seconds;
    impl_->audioBufferTailPTS = seconds;
    impl_->initialSyncDone.store(false);

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
    // Safety check for shutdown - audio thread may call this during cleanup
    if (!impl_ || impl_->isShuttingDown.load()) {
        if (buffer) {
            for (uint32_t i = 0; i < maxFrames * audioChannels_; i++) {
                buffer[i] = 0.0f;
            }
        }
        return maxFrames;
    }

    if (!hasAudio_ || !buffer || maxFrames == 0) {
        return 0;
    }

    uint32_t samplesNeeded = maxFrames * audioChannels_;
    uint32_t samplesCopied = 0;

    // Get current video time for A/V sync
    double videoPTS = impl_->lastVideoTimeSeconds.load();
    bool videoStarted = impl_->videoHasStarted.load();
    bool initialSyncDone = impl_->initialSyncDone.load();

    {
        std::lock_guard<std::mutex> lock(impl_->audioChunksMutex);

        // Skip sync correction until video has started playing
        if (!videoStarted || videoPTS < 0.0) {
            // Output silence during startup
            for (uint32_t i = 0; i < samplesNeeded; i++) {
                buffer[i] = 0.0f;
            }
            return maxFrames;
        }

        // Calculate sync error: positive = audio behind, negative = audio ahead
        double syncError = videoPTS - impl_->audioBufferHeadPTS;

        // Initial sync: align audio to video time
        if (!initialSyncDone) {
            if (syncError > 0.050) {
                // Audio behind video - discard samples to catch up
                double samplesToSkipSec = syncError;
                size_t samplesToSkip = static_cast<size_t>(samplesToSkipSec * audioSampleRate_ * audioChannels_);

                size_t skipped = 0;
                while (skipped < samplesToSkip && !impl_->audioChunks.empty()) {
                    auto& chunk = impl_->audioChunks.front();
                    size_t canSkip = std::min(chunk.samples.size(), samplesToSkip - skipped);
                    chunk.samples.erase(chunk.samples.begin(), chunk.samples.begin() + canSkip);
                    skipped += canSkip;
                    if (chunk.samples.empty()) {
                        impl_->audioChunks.pop_front();
                    }
                }
                if (!impl_->audioChunks.empty()) {
                    impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
                }
            }
            else if (syncError < -0.050) {
                // Audio ahead of video - set head PTS to match video
                impl_->audioBufferHeadPTS = videoPTS;
            }

            impl_->initialSyncDone.store(true);
            syncError = videoPTS - impl_->audioBufferHeadPTS;
        }

        // Sync thresholds (in seconds)
        constexpr double SYNC_TOLERANCE = 0.100;      // ±100ms is imperceptible
        constexpr double SYNC_CRITICAL = 0.500;       // 500ms triggers aggressive correction

        // Handle sync correction
        if (syncError > SYNC_CRITICAL) {
            // Audio significantly behind video - skip samples
            double samplesToSkipSec = syncError - SYNC_TOLERANCE;
            size_t samplesToSkip = static_cast<size_t>(samplesToSkipSec * audioSampleRate_ * audioChannels_);

            size_t skipped = 0;
            while (skipped < samplesToSkip && !impl_->audioChunks.empty()) {
                auto& chunk = impl_->audioChunks.front();
                size_t canSkip = std::min(chunk.samples.size(), samplesToSkip - skipped);
                chunk.samples.erase(chunk.samples.begin(), chunk.samples.begin() + canSkip);
                skipped += canSkip;
                if (chunk.samples.empty()) {
                    impl_->audioChunks.pop_front();
                }
            }
            if (!impl_->audioChunks.empty()) {
                impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
            }
        }
        else if (syncError < -SYNC_CRITICAL) {
            // Audio significantly ahead of video - insert silence
            for (uint32_t i = 0; i < samplesNeeded; i++) {
                buffer[i] = 0.0f;
            }
            return maxFrames;
        }

        // Copy samples from chunks to output buffer
        while (samplesCopied < samplesNeeded && !impl_->audioChunks.empty()) {
            auto& chunk = impl_->audioChunks.front();

            while (samplesCopied < samplesNeeded && !chunk.samples.empty()) {
                buffer[samplesCopied++] = chunk.samples.front();
                chunk.samples.erase(chunk.samples.begin());
            }

            if (chunk.samples.empty()) {
                impl_->audioChunks.pop_front();
                if (!impl_->audioChunks.empty()) {
                    impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
                }
            }
        }

        // Update head PTS based on samples consumed
        double consumedSec = static_cast<double>(samplesCopied / audioChannels_) / audioSampleRate_;
        impl_->audioBufferHeadPTS += consumedSec;
    }

    // Zero-fill any remaining samples (buffer underrun)
    while (samplesCopied < samplesNeeded) {
        buffer[samplesCopied++] = 0.0f;
    }

    return maxFrames;
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
