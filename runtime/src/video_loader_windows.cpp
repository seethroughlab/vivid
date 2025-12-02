#include "video_loader.h"
#include "renderer.h"
#include <iostream>
#include <vector>

#if defined(_WIN32)

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

namespace vivid {

// Helper to check HRESULT and log errors
static bool checkHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::cerr << "[VideoLoaderWindows] " << operation << " failed: "
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
        initialized_ = SUCCEEDED(hr) || hr == S_FALSE; // S_FALSE means already initialized
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
            std::cerr << "[VideoLoaderWindows] MFStartup failed\n";
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

/**
 * @brief Windows video loader using Media Foundation.
 *
 * Uses the Source Reader API for hardware-accelerated video decoding.
 */
class VideoLoaderWindows : public VideoLoader {
private:
    // Output format enum for tracking what Media Foundation gives us
    enum class OutputFormat {
        BGRA,   // MFVideoFormat_RGB32
        ARGB,   // MFVideoFormat_ARGB32
        RGB24,  // MFVideoFormat_RGB24
    };

public:
    VideoLoaderWindows() {
        // Initialize COM and Media Foundation
        comInit_ = std::make_unique<COMInitializer>();
        if (!comInit_->isInitialized()) {
            std::cerr << "[VideoLoaderWindows] COM initialization failed\n";
            return;
        }

        mfInit_ = std::make_unique<MFInitializer>();
        if (!mfInit_->isInitialized()) {
            std::cerr << "[VideoLoaderWindows] Media Foundation initialization failed\n";
        }
    }

    ~VideoLoaderWindows() override {
        close();
    }

    bool open(const std::string& path) override {
        close();

        if (!mfInit_ || !mfInit_->isInitialized()) {
            std::cerr << "[VideoLoaderWindows] Media Foundation not initialized\n";
            return false;
        }

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

        // Enable video processing (color conversion) so we can request RGB output
        hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (!checkHR(hr, "SetUINT32(VIDEO_PROCESSING)")) {
            attributes->Release();
            return false;
        }

        // Create source reader from URL
        hr = MFCreateSourceReaderFromURL(widePath.c_str(), attributes, &sourceReader_);
        attributes->Release();

        if (!checkHR(hr, "MFCreateSourceReaderFromURL")) {
            return false;
        }

        // Configure output format - try RGB32 first, then fall back to other formats
        // Some decoders don't support direct RGB32 output
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

        // Try RGB32 (BGRA) first - ideal for direct upload
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) {
            hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
            if (SUCCEEDED(hr)) {
                formatSet = true;
                outputFormat_ = OutputFormat::BGRA;
            }
        }

        // Try ARGB32
        if (!formatSet) {
            hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
            if (SUCCEEDED(hr)) {
                hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
                if (SUCCEEDED(hr)) {
                    formatSet = true;
                    outputFormat_ = OutputFormat::ARGB;
                }
            }
        }

        // Try RGB24
        if (!formatSet) {
            hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
            if (SUCCEEDED(hr)) {
                hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
                if (SUCCEEDED(hr)) {
                    formatSet = true;
                    outputFormat_ = OutputFormat::RGB24;
                }
            }
        }

        outputType->Release();

        if (!formatSet) {
            std::cerr << "[VideoLoaderWindows] No compatible output format found\n";
            close();
            return false;
        }

        // Get actual output format (may differ from requested)
        IMFMediaType* actualType = nullptr;
        hr = sourceReader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
        if (!checkHR(hr, "GetCurrentMediaType")) {
            close();
            return false;
        }

        // Extract video dimensions
        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &width, &height);
        if (!checkHR(hr, "MFGetAttributeSize(FRAME_SIZE)")) {
            actualType->Release();
            close();
            return false;
        }
        info_.width = static_cast<int>(width);
        info_.height = static_cast<int>(height);

        // Extract frame rate
        UINT32 numerator = 0, denominator = 1;
        hr = MFGetAttributeRatio(actualType, MF_MT_FRAME_RATE, &numerator, &denominator);
        if (SUCCEEDED(hr) && denominator > 0) {
            info_.frameRate = static_cast<double>(numerator) / static_cast<double>(denominator);
        } else {
            info_.frameRate = 30.0; // Default fallback
        }

        // Get stride (may be negative for bottom-up DIB)
        LONG stride = 0;
        hr = actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride));
        if (SUCCEEDED(hr)) {
            stride_ = stride;
        } else {
            // Calculate default stride (may be wrong for some formats)
            stride_ = info_.width * 4;
        }

        actualType->Release();

        // Get duration
        PROPVARIANT var;
        PropVariantInit(&var);
        hr = sourceReader_->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var);
        if (SUCCEEDED(hr)) {
            LONGLONG duration100ns = 0;
            PropVariantToInt64(var, &duration100ns);
            info_.duration = static_cast<double>(duration100ns) / 10000000.0; // 100ns units to seconds
            PropVariantClear(&var);
        }

        // Calculate frame count
        if (info_.frameRate > 0 && info_.duration > 0) {
            info_.frameCount = static_cast<int64_t>(info_.duration * info_.frameRate);
        }

        // Check for audio stream
        IMFMediaType* audioType = nullptr;
        hr = sourceReader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &audioType);
        if (SUCCEEDED(hr)) {
            info_.hasAudio = true;
            audioType->Release();
        } else {
            info_.hasAudio = false;
        }

        info_.codecType = VideoCodecType::Standard;
        info_.codecName = "Media Foundation";

        path_ = path;
        isOpen_ = true;

        std::cout << "[VideoLoaderWindows] Opened " << path
                  << " (" << info_.width << "x" << info_.height
                  << ", " << info_.frameRate << "fps"
                  << ", " << info_.duration << "s"
                  << (info_.hasAudio ? ", with audio" : "") << ")\n";

        return true;
    }

    void close() override {
        if (sourceReader_) {
            sourceReader_->Release();
            sourceReader_ = nullptr;
        }

        isOpen_ = false;
        info_ = VideoInfo{};
        path_.clear();
        currentTime_ = 0;
        currentFrame_ = 0;
        stride_ = 0;
        outputFormat_ = OutputFormat::BGRA;
    }

    bool isOpen() const override { return isOpen_; }
    const VideoInfo& info() const override { return info_; }

    bool seek(double timeSeconds) override {
        if (!isOpen_ || !sourceReader_) return false;

        // Convert to 100-nanosecond units
        LONGLONG position = static_cast<LONGLONG>(timeSeconds * 10000000.0);

        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = position;

        HRESULT hr = sourceReader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);

        if (!checkHR(hr, "SetCurrentPosition")) {
            return false;
        }

        currentTime_ = timeSeconds;
        currentFrame_ = static_cast<int64_t>(timeSeconds * info_.frameRate);

        return true;
    }

    bool seekToFrame(int64_t frameNumber) override {
        if (info_.frameRate <= 0) return false;
        double time = static_cast<double>(frameNumber) / info_.frameRate;
        return seek(time);
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        if (!isOpen_ || !sourceReader_) return false;

        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = sourceReader_->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        if (!checkHR(hr, "ReadSample")) {
            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            // End of video
            if (sample) sample->Release();
            return false;
        }

        if (!sample) {
            return false;
        }

        // Update current time
        currentTime_ = static_cast<double>(timestamp) / 10000000.0;
        currentFrame_ = static_cast<int64_t>(currentTime_ * info_.frameRate);

        // Get buffer from sample
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (!checkHR(hr, "ConvertToContiguousBuffer")) {
            sample->Release();
            return false;
        }

        // Lock buffer
        BYTE* data = nullptr;
        DWORD maxLength = 0, currentLength = 0;
        hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (!checkHR(hr, "Buffer Lock")) {
            buffer->Release();
            sample->Release();
            return false;
        }

        int width = info_.width;
        int height = info_.height;

        // Ensure output texture has correct dimensions
        if (!output.valid() || output.width != width || output.height != height) {
            if (output.valid()) {
                renderer.destroyTexture(output);
            }
            output = renderer.createTexture(width, height);
            if (!output.valid()) {
                buffer->Unlock();
                buffer->Release();
                sample->Release();
                return false;
            }
        }

        // Convert to RGBA based on source format
        std::vector<uint8_t> pixels(width * height * 4);

        LONG absStride = (stride_ < 0) ? -stride_ : stride_;
        if (absStride == 0) {
            // Calculate default stride based on format
            if (outputFormat_ == OutputFormat::RGB24) {
                absStride = width * 3;
            } else {
                absStride = width * 4;
            }
        }

        // Check if image is bottom-up (negative stride)
        bool bottomUp = (stride_ < 0);

        for (int y = 0; y < height; y++) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            const uint8_t* src = data + srcY * absStride;
            uint8_t* dst = pixels.data() + y * width * 4;

            for (int x = 0; x < width; x++) {
                switch (outputFormat_) {
                    case OutputFormat::BGRA:
                        // BGRA -> RGBA
                        dst[0] = src[2];  // R <- B
                        dst[1] = src[1];  // G <- G
                        dst[2] = src[0];  // B <- R
                        dst[3] = src[3];  // A <- A
                        src += 4;
                        break;
                    case OutputFormat::ARGB:
                        // ARGB -> RGBA
                        dst[0] = src[1];  // R
                        dst[1] = src[2];  // G
                        dst[2] = src[3];  // B
                        dst[3] = src[0];  // A
                        src += 4;
                        break;
                    case OutputFormat::RGB24:
                        // RGB -> RGBA (no alpha)
                        dst[0] = src[0];  // R
                        dst[1] = src[1];  // G
                        dst[2] = src[2];  // B
                        dst[3] = 255;     // A (opaque)
                        src += 3;
                        break;
                }
                dst += 4;
            }
        }

        renderer.uploadTexturePixels(output, pixels.data(), width, height);

        buffer->Unlock();
        buffer->Release();
        sample->Release();

        return true;
    }

    double currentTime() const override { return currentTime_; }
    int64_t currentFrame() const override { return currentFrame_; }

private:
    std::unique_ptr<COMInitializer> comInit_;
    std::unique_ptr<MFInitializer> mfInit_;
    IMFSourceReader* sourceReader_ = nullptr;

    VideoInfo info_;
    std::string path_;
    bool isOpen_ = false;
    double currentTime_ = 0;
    int64_t currentFrame_ = 0;
    LONG stride_ = 0;
    OutputFormat outputFormat_ = OutputFormat::BGRA;
};

std::unique_ptr<VideoLoader> createVideoLoaderWindows() {
    return std::make_unique<VideoLoaderWindows>();
}

} // namespace vivid

#endif // _WIN32
