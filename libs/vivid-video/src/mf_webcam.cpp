// Media Foundation Webcam Capture - Windows camera capture via IMFSourceReader
// Captures frames to RGBA pixels and uploads to GPU texture

#if defined(_WIN32)

#include <vivid/video/mf_webcam.h>
#include <vivid/context.h>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <wmcodecdsp.h>

#include <iostream>
#include <mutex>
#include <atomic>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Helper macro for COM release
template<class T>
void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

namespace vivid::video {

// RAII wrapper for COM initialization
class ComInitializer {
public:
    ComInitializer() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(hr) || hr == S_FALSE; // S_FALSE means already initialized
    }
    ~ComInitializer() {
        if (initialized_) {
            CoUninitialize();
        }
    }
    bool isInitialized() const { return initialized_; }
private:
    bool initialized_ = false;
};

// RAII wrapper for Media Foundation
class MFInitializer {
public:
    MFInitializer() {
        HRESULT hr = MFStartup(MF_VERSION);
        initialized_ = SUCCEEDED(hr);
        if (!initialized_) {
            std::cerr << "[MFWebcam] MFStartup failed: 0x" << std::hex << hr << std::dec << std::endl;
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

struct MFWebcam::Impl {
    ComInitializer comInit;
    MFInitializer mfInit;

    IMFSourceReader* sourceReader = nullptr;
    IMFMediaSource* mediaSource = nullptr;

    std::atomic<bool> hasNewFrame{false};
    std::mutex frameMutex;
    std::vector<uint8_t> frameBuffer;
    int frameWidth = 0;
    int frameHeight = 0;

    bool isVideoFormatNV12 = false;  // Track if we need NV12->RGB conversion
    bool isVideoFormatYUY2 = false;  // Track if we need YUY2->RGB conversion

    void cleanup() {
        SafeRelease(&sourceReader);
        SafeRelease(&mediaSource);
        hasNewFrame.store(false);
        frameBuffer.clear();
        frameWidth = 0;
        frameHeight = 0;
    }
};

MFWebcam::MFWebcam() : impl_(std::make_unique<Impl>()) {}

MFWebcam::~MFWebcam() {
    close();
}

const uint8_t* MFWebcam::cpuPixelData() const {
    return impl_->frameBuffer.data();
}

size_t MFWebcam::cpuPixelDataSize() const {
    return impl_->frameBuffer.size();
}

std::vector<CameraDevice> MFWebcam::enumerateDevices() {
    std::vector<CameraDevice> devices;

    ComInitializer com;
    MFInitializer mf;

    if (!com.isInitialized() || !mf.isInitialized()) {
        std::cerr << "[MFWebcam] Failed to initialize COM/MF for device enumeration" << std::endl;
        return devices;
    }

    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        std::cerr << "[MFWebcam] MFCreateAttributes failed" << std::endl;
        return devices;
    }

    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        SafeRelease(&attributes);
        return devices;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    hr = MFEnumDeviceSources(attributes, &ppDevices, &count);
    SafeRelease(&attributes);

    if (FAILED(hr) || count == 0) {
        return devices;
    }

    for (UINT32 i = 0; i < count; i++) {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;

        hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                               &friendlyName, &nameLength);

        if (SUCCEEDED(hr) && friendlyName) {
            CameraDevice device;

            // Convert wide string to UTF-8
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::vector<char> utf8Name(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, utf8Name.data(), utf8Len, nullptr, nullptr);
                device.name = utf8Name.data();
            }

            // Use index as device ID for simplicity
            device.deviceId = std::to_string(i);
            device.isDefault = (i == 0);

            devices.push_back(device);
            CoTaskMemFree(friendlyName);
        }

        ppDevices[i]->Release();
    }

    CoTaskMemFree(ppDevices);

    return devices;
}

void MFWebcam::createTexture() {
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }

    WGPUTextureDescriptor desc = {};
    desc.label = toStringView("WebcamFrame");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[MFWebcam] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("WebcamFrameView");
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

bool MFWebcam::configureSourceReader(int requestedWidth, int requestedHeight, float requestedFps) {
    if (!impl_->sourceReader) return false;

    // Try to find the best matching media type
    DWORD mediaTypeIndex = 0;
    IMFMediaType* nativeType = nullptr;
    IMFMediaType* bestType = nullptr;
    int bestScore = -1;
    DWORD bestIndex = 0;

    while (SUCCEEDED(impl_->sourceReader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaTypeIndex, &nativeType))) {

        GUID subtype;
        if (SUCCEEDED(nativeType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
            // Get frame size
            UINT32 width = 0, height = 0;
            MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height);

            // Get frame rate
            UINT32 numerator = 0, denominator = 1;
            MFGetAttributeRatio(nativeType, MF_MT_FRAME_RATE, &numerator, &denominator);
            float fps = (denominator > 0) ? static_cast<float>(numerator) / denominator : 30.0f;

            // Score this format
            int score = 0;

            // Prefer RGB32 or RGB24, but accept NV12/YUY2 (common webcam formats)
            if (subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_ARGB32) {
                score += 1000;
            } else if (subtype == MFVideoFormat_RGB24) {
                score += 800;
            } else if (subtype == MFVideoFormat_NV12) {
                score += 600;
            } else if (subtype == MFVideoFormat_YUY2) {
                score += 400;
            } else {
                // Skip unsupported formats
                SafeRelease(&nativeType);
                mediaTypeIndex++;
                continue;
            }

            // Prefer sizes close to requested
            if (width >= static_cast<UINT32>(requestedWidth) &&
                height >= static_cast<UINT32>(requestedHeight)) {
                score += 100;
            }
            // Penalty for much larger sizes
            if (width > static_cast<UINT32>(requestedWidth) * 2 ||
                height > static_cast<UINT32>(requestedHeight) * 2) {
                score -= 50;
            }

            // Prefer frame rates close to requested
            if (fps >= requestedFps - 5) {
                score += 50;
            }

            if (score > bestScore) {
                SafeRelease(&bestType);
                bestType = nativeType;
                nativeType = nullptr;  // Don't release, transferred to bestType
                bestScore = score;
                bestIndex = mediaTypeIndex;
            }
        }

        SafeRelease(&nativeType);
        mediaTypeIndex++;
    }

    if (!bestType) {
        std::cerr << "[MFWebcam] No compatible media type found" << std::endl;
        return false;
    }

    // Get the format details
    GUID subtype;
    bestType->GetGUID(MF_MT_SUBTYPE, &subtype);

    UINT32 width = 0, height = 0;
    MFGetAttributeSize(bestType, MF_MT_FRAME_SIZE, &width, &height);

    UINT32 numerator = 0, denominator = 1;
    MFGetAttributeRatio(bestType, MF_MT_FRAME_RATE, &numerator, &denominator);

    impl_->isVideoFormatNV12 = (subtype == MFVideoFormat_NV12);
    impl_->isVideoFormatYUY2 = (subtype == MFVideoFormat_YUY2);

    // If the native format is YUV, ask MF to convert to RGB32
    IMFMediaType* outputType = nullptr;
    HRESULT hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) {
        SafeRelease(&bestType);
        return false;
    }

    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, width, height);
    hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, numerator, denominator);

    hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);

    if (FAILED(hr)) {
        // If RGB32 conversion fails, try to use native format
        std::cerr << "[MFWebcam] RGB32 conversion not available, using native format" << std::endl;
        hr = impl_->sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, bestType);
    } else {
        // Successfully set RGB32, no YUV conversion needed
        impl_->isVideoFormatNV12 = false;
        impl_->isVideoFormatYUY2 = false;
    }

    SafeRelease(&outputType);
    SafeRelease(&bestType);

    if (FAILED(hr)) {
        std::cerr << "[MFWebcam] Failed to set media type: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Get the actual configured format
    IMFMediaType* currentType = nullptr;
    hr = impl_->sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
    if (SUCCEEDED(hr)) {
        MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &width, &height);
        MFGetAttributeRatio(currentType, MF_MT_FRAME_RATE, &numerator, &denominator);

        width_ = static_cast<int>(width);
        height_ = static_cast<int>(height);
        frameRate_ = (denominator > 0) ? static_cast<float>(numerator) / denominator : 30.0f;

        impl_->frameWidth = width_;
        impl_->frameHeight = height_;
        impl_->frameBuffer.resize(width_ * height_ * 4);

        SafeRelease(&currentType);
    }

    return true;
}

bool MFWebcam::open(Context& ctx, int width, int height, float fps) {
    return openByIndex(ctx, 0, width, height, fps);
}

bool MFWebcam::open(Context& ctx, const std::string& deviceId, int width, int height, float fps) {
    try {
        int index = std::stoi(deviceId);
        return openByIndex(ctx, index, width, height, fps);
    } catch (...) {
        std::cerr << "[MFWebcam] Invalid device ID: " << deviceId << std::endl;
        return false;
    }
}

bool MFWebcam::openByIndex(Context& ctx, int index, int width, int height, float fps) {
    close();

    if (!impl_->comInit.isInitialized() || !impl_->mfInit.isInitialized()) {
        std::cerr << "[MFWebcam] COM/MF not initialized" << std::endl;
        return false;
    }

    device_ = ctx.device();
    queue_ = ctx.queue();

    // Enumerate devices
    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        std::cerr << "[MFWebcam] MFCreateAttributes failed" << std::endl;
        return false;
    }

    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        SafeRelease(&attributes);
        return false;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    hr = MFEnumDeviceSources(attributes, &ppDevices, &count);
    SafeRelease(&attributes);

    if (FAILED(hr) || count == 0) {
        std::cerr << "[MFWebcam] No video capture devices found" << std::endl;
        return false;
    }

    if (index < 0 || index >= static_cast<int>(count)) {
        std::cerr << "[MFWebcam] Invalid camera index: " << index << std::endl;
        for (UINT32 i = 0; i < count; i++) {
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
        return false;
    }

    // Get device name
    WCHAR* friendlyName = nullptr;
    UINT32 nameLength = 0;
    hr = ppDevices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                               &friendlyName, &nameLength);
    if (SUCCEEDED(hr) && friendlyName) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::vector<char> utf8Name(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, utf8Name.data(), utf8Len, nullptr, nullptr);
            deviceName_ = utf8Name.data();
        }
        CoTaskMemFree(friendlyName);
    }

    // Activate the device
    hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&impl_->mediaSource));

    // Release all devices
    for (UINT32 i = 0; i < count; i++) {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(hr)) {
        std::cerr << "[MFWebcam] Failed to activate device: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Create source reader
    IMFAttributes* readerAttributes = nullptr;
    hr = MFCreateAttributes(&readerAttributes, 2);
    if (SUCCEEDED(hr)) {
        // Enable hardware transforms (for YUV->RGB conversion)
        readerAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(impl_->mediaSource, readerAttributes, &impl_->sourceReader);
    SafeRelease(&readerAttributes);

    if (FAILED(hr)) {
        std::cerr << "[MFWebcam] Failed to create source reader: 0x" << std::hex << hr << std::dec << std::endl;
        impl_->cleanup();
        return false;
    }

    // Configure the format
    if (!configureSourceReader(width, height, fps)) {
        impl_->cleanup();
        return false;
    }

    // Create GPU texture
    createTexture();
    if (!texture_) {
        impl_->cleanup();
        return false;
    }

    isCapturing_ = true;

    std::cout << "[MFWebcam] Opened: " << deviceName_
              << " (" << width_ << "x" << height_
              << " @ " << frameRate_ << "fps)" << std::endl;

    return true;
}

void MFWebcam::close() {
    stopCapture();
    impl_->cleanup();

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
    frameRate_ = 30.0f;
    deviceName_.clear();
    isCapturing_ = false;
}

bool MFWebcam::isOpen() const {
    return impl_->sourceReader != nullptr;
}

bool MFWebcam::startCapture() {
    if (!impl_->sourceReader) return false;
    isCapturing_ = true;
    std::cout << "[MFWebcam] Started capture" << std::endl;
    return true;
}

void MFWebcam::stopCapture() {
    isCapturing_ = false;
    std::cout << "[MFWebcam] Stopped capture" << std::endl;
}

bool MFWebcam::update(Context& ctx) {
    if (!impl_->sourceReader || !isCapturing_) {
        return false;
    }

    // Read next sample (synchronous)
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = nullptr;

    HRESULT hr = impl_->sourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // No flags
        &streamIndex,
        &flags,
        &timestamp,
        &sample
    );

    if (FAILED(hr) || !sample) {
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            std::cerr << "[MFWebcam] End of stream" << std::endl;
        }
        return false;
    }

    // Get the buffer from the sample
    IMFMediaBuffer* buffer = nullptr;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        SafeRelease(&sample);
        return false;
    }

    // Lock the buffer
    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;

    hr = buffer->Lock(&data, &maxLength, &currentLength);
    if (SUCCEEDED(hr) && data) {
        // Copy and convert BGR(A) to RGBA
        // MF RGB32 is actually BGRA
        uint8_t* src = data;
        uint8_t* dst = impl_->frameBuffer.data();

        // Media Foundation RGB32 is bottom-up BGRA, we need top-down RGBA
        int stride = width_ * 4;
        for (int y = 0; y < height_; y++) {
            // Flip vertically: read from bottom, write to top
            uint8_t* srcRow = src + (height_ - 1 - y) * stride;
            uint8_t* dstRow = dst + y * stride;

            for (int x = 0; x < width_; x++) {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
                dstRow[x * 4 + 3] = 255;                 // A
            }
        }

        // Upload to GPU
        WGPUTexelCopyTextureInfo destination = {};
        destination.texture = texture_;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout dataLayout = {};
        dataLayout.offset = 0;
        dataLayout.bytesPerRow = static_cast<uint32_t>(width_ * 4);
        dataLayout.rowsPerImage = static_cast<uint32_t>(height_);

        WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

        wgpuQueueWriteTexture(queue_, &destination, impl_->frameBuffer.data(),
                              impl_->frameBuffer.size(), &dataLayout, &writeSize);

        buffer->Unlock();
    }

    SafeRelease(&buffer);
    SafeRelease(&sample);

    return true;
}

} // namespace vivid::video

#endif // _WIN32
