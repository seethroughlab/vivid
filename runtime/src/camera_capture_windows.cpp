// Windows camera capture using Media Foundation
#include "camera_capture.h"
#include "renderer.h"
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <comdef.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

namespace vivid {

// Helper to check HRESULT and log errors
static bool checkHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::cerr << "[CameraCaptureWindows] " << operation << " failed: "
                  << err.ErrorMessage() << " (0x" << std::hex << hr << std::dec << ")\n";
        return false;
    }
    return true;
}

// Convert wide string to UTF-8
static std::string wideToUtf8(const wchar_t* wide) {
    if (!wide) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// Convert UTF-8 to wide string
static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    return result;
}

/**
 * @brief Windows camera capture using Media Foundation Source Reader.
 */
class CameraCaptureWindows : public CameraCapture {
private:
    // Output format tracking
    enum class PixelFormat {
        BGRA,
        RGB24,
        NV12,
        YUY2,
    };

public:
    CameraCaptureWindows() {
        // Initialize COM
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE;

        // Initialize Media Foundation
        hr = MFStartup(MF_VERSION);
        mfInitialized_ = SUCCEEDED(hr);
        if (!mfInitialized_) {
            std::cerr << "[CameraCaptureWindows] MFStartup failed\n";
        }
    }

    ~CameraCaptureWindows() override {
        close();

        if (mfInitialized_) {
            MFShutdown();
        }
        if (comInitialized_) {
            CoUninitialize();
        }
    }

    std::vector<CameraDeviceInfo> enumerateDevices() override {
        std::vector<CameraDeviceInfo> devices;

        if (!mfInitialized_) return devices;

        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return devices;

        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            attributes->Release();
            return devices;
        }

        IMFActivate** deviceArray = nullptr;
        UINT32 deviceCount = 0;

        hr = MFEnumDeviceSources(attributes, &deviceArray, &deviceCount);
        attributes->Release();

        if (FAILED(hr)) {
            _com_error err(hr);
            std::cerr << "[CameraCaptureWindows] MFEnumDeviceSources failed in enumerate: "
                      << err.ErrorMessage() << " (0x" << std::hex << hr << std::dec << ")\n";
            return devices;
        }

        std::cout << "[CameraCaptureWindows] Found " << deviceCount << " camera(s)\n";
        if (deviceCount == 0 || !deviceArray) return devices;

        for (UINT32 i = 0; i < deviceCount; i++) {
            CameraDeviceInfo info;

            // Get device ID (symbolic link)
            WCHAR* symbolicLink = nullptr;
            UINT32 linkLength = 0;
            hr = deviceArray[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &symbolicLink, &linkLength);
            if (SUCCEEDED(hr) && symbolicLink) {
                info.deviceId = wideToUtf8(symbolicLink);
                CoTaskMemFree(symbolicLink);
            }

            // Get friendly name
            WCHAR* friendlyName = nullptr;
            UINT32 nameLength = 0;
            hr = deviceArray[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                &friendlyName, &nameLength);
            if (SUCCEEDED(hr) && friendlyName) {
                info.name = wideToUtf8(friendlyName);
                CoTaskMemFree(friendlyName);
            }

            info.isDefault = (i == 0);  // First device is typically the default
            devices.push_back(info);

            deviceArray[i]->Release();
        }

        CoTaskMemFree(deviceArray);
        return devices;
    }

    std::vector<CameraMode> enumerateModes(const std::string& deviceId) override {
        std::vector<CameraMode> modes;

        if (!mfInitialized_) return modes;

        // Enumerate devices to find the target device
        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return modes;

        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            attributes->Release();
            return modes;
        }

        IMFActivate** deviceArray = nullptr;
        UINT32 deviceCount = 0;

        hr = MFEnumDeviceSources(attributes, &deviceArray, &deviceCount);
        attributes->Release();

        if (FAILED(hr) || !deviceArray || deviceCount == 0) {
            return modes;
        }

        // Find target device (default to first if deviceId is empty)
        IMFActivate* targetDevice = nullptr;
        if (deviceId.empty()) {
            targetDevice = deviceArray[0];
            targetDevice->AddRef();
        } else {
            std::wstring wideDeviceId = utf8ToWide(deviceId);
            for (UINT32 i = 0; i < deviceCount; i++) {
                WCHAR* symbolicLink = nullptr;
                UINT32 linkLength = 0;
                hr = deviceArray[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &symbolicLink, &linkLength);

                if (SUCCEEDED(hr) && symbolicLink) {
                    if (wcscmp(symbolicLink, wideDeviceId.c_str()) == 0) {
                        targetDevice = deviceArray[i];
                        targetDevice->AddRef();
                    }
                    CoTaskMemFree(symbolicLink);
                }
                if (targetDevice) break;
            }
        }

        // Clean up device array
        for (UINT32 i = 0; i < deviceCount; i++) {
            deviceArray[i]->Release();
        }
        CoTaskMemFree(deviceArray);

        if (!targetDevice) return modes;

        // Create media source
        IMFMediaSource* mediaSource = nullptr;
        hr = targetDevice->ActivateObject(IID_PPV_ARGS(&mediaSource));
        targetDevice->Release();

        if (FAILED(hr) || !mediaSource) return modes;

        // Create source reader to enumerate formats
        IMFSourceReader* reader = nullptr;
        hr = MFCreateSourceReaderFromMediaSource(mediaSource, nullptr, &reader);
        mediaSource->Release();

        if (FAILED(hr) || !reader) return modes;

        // Enumerate native media types
        for (DWORD i = 0; ; i++) {
            IMFMediaType* mediaType = nullptr;
            hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &mediaType);
            if (FAILED(hr)) break;

            CameraMode mode;

            // Get dimensions
            UINT32 width = 0, height = 0;
            hr = MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height);
            if (SUCCEEDED(hr)) {
                mode.width = static_cast<int>(width);
                mode.height = static_cast<int>(height);
            }

            // Get frame rate
            UINT32 fpsNum = 0, fpsDenom = 1;
            hr = MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &fpsNum, &fpsDenom);
            if (SUCCEEDED(hr) && fpsDenom > 0) {
                float fps = static_cast<float>(fpsNum) / fpsDenom;
                mode.minFrameRate = fps;
                mode.maxFrameRate = fps;
            } else {
                mode.minFrameRate = 30.0f;
                mode.maxFrameRate = 30.0f;
            }

            // Get pixel format
            GUID subtype = GUID_NULL;
            hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (SUCCEEDED(hr)) {
                if (subtype == MFVideoFormat_NV12) {
                    mode.pixelFormat = "NV12";
                } else if (subtype == MFVideoFormat_YUY2) {
                    mode.pixelFormat = "YUY2";
                } else if (subtype == MFVideoFormat_RGB32) {
                    mode.pixelFormat = "BGRA";
                } else if (subtype == MFVideoFormat_RGB24) {
                    mode.pixelFormat = "RGB24";
                } else if (subtype == MFVideoFormat_MJPG) {
                    mode.pixelFormat = "MJPG";
                } else {
                    mode.pixelFormat = "Unknown";
                }
            }

            mediaType->Release();
            modes.push_back(mode);
        }

        reader->Release();
        return modes;
    }

    bool open(const CameraConfig& config) override {
        return openByIndex(0, config);
    }

    bool open(const std::string& deviceId, const CameraConfig& config) override {
        close();

        if (!mfInitialized_) {
            std::cerr << "[CameraCaptureWindows] Media Foundation not initialized\n";
            return false;
        }

        // Enumerate devices to find matching one
        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return false;

        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            attributes->Release();
            return false;
        }

        IMFActivate** deviceArray = nullptr;
        UINT32 deviceCount = 0;

        hr = MFEnumDeviceSources(attributes, &deviceArray, &deviceCount);
        attributes->Release();

        if (FAILED(hr) || !deviceArray) return false;

        IMFActivate* targetDevice = nullptr;
        std::wstring wideDeviceId = utf8ToWide(deviceId);

        for (UINT32 i = 0; i < deviceCount; i++) {
            WCHAR* symbolicLink = nullptr;
            UINT32 linkLength = 0;
            hr = deviceArray[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &symbolicLink, &linkLength);

            if (SUCCEEDED(hr) && symbolicLink) {
                if (wcscmp(symbolicLink, wideDeviceId.c_str()) == 0) {
                    targetDevice = deviceArray[i];
                    targetDevice->AddRef();
                }
                CoTaskMemFree(symbolicLink);
            }

            deviceArray[i]->Release();
        }
        CoTaskMemFree(deviceArray);

        if (!targetDevice) {
            std::cerr << "[CameraCaptureWindows] Device not found: " << deviceId << "\n";
            return false;
        }

        bool result = openDevice(targetDevice, config);
        targetDevice->Release();
        return result;
    }

    bool openByIndex(int index, const CameraConfig& config) override {
        close();

        if (!mfInitialized_) {
            std::cerr << "[CameraCaptureWindows] Media Foundation not initialized\n";
            return false;
        }

        // Enumerate devices
        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return false;

        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            attributes->Release();
            return false;
        }

        IMFActivate** deviceArray = nullptr;
        UINT32 deviceCount = 0;

        hr = MFEnumDeviceSources(attributes, &deviceArray, &deviceCount);
        attributes->Release();

        if (FAILED(hr)) {
            _com_error err(hr);
            std::cerr << "[CameraCaptureWindows] MFEnumDeviceSources failed: "
                      << err.ErrorMessage() << " (0x" << std::hex << hr << std::dec << ")\n";
            return false;
        }

        if (deviceCount == 0 || !deviceArray) {
            std::cerr << "[CameraCaptureWindows] No cameras found (count=" << deviceCount << ")\n";
            return false;
        }

        if (index < 0 || static_cast<UINT32>(index) >= deviceCount) {
            std::cerr << "[CameraCaptureWindows] Invalid camera index: " << index << "\n";
            for (UINT32 i = 0; i < deviceCount; i++) {
                deviceArray[i]->Release();
            }
            CoTaskMemFree(deviceArray);
            return false;
        }

        IMFActivate* targetDevice = deviceArray[index];
        targetDevice->AddRef();

        // Clean up other devices
        for (UINT32 i = 0; i < deviceCount; i++) {
            deviceArray[i]->Release();
        }
        CoTaskMemFree(deviceArray);

        bool result = openDevice(targetDevice, config);
        targetDevice->Release();
        return result;
    }

    void close() override {
        stopCapture();

        if (sourceReader_) {
            sourceReader_->Release();
            sourceReader_ = nullptr;
        }

        info_ = CameraInfo{};
        hasNewFrame_.store(false);
        pixelFormat_ = PixelFormat::BGRA;
        stride_ = 0;

        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_.clear();
    }

    bool isOpen() const override {
        return sourceReader_ != nullptr;
    }

    bool startCapture() override {
        if (!sourceReader_) return false;
        isCapturing_ = true;
        info_.isCapturing = true;
        std::cout << "[CameraCaptureWindows] Started capture\n";
        return true;
    }

    void stopCapture() override {
        isCapturing_ = false;
        info_.isCapturing = false;
    }

    bool isCapturing() const override {
        return isCapturing_;
    }

    const CameraInfo& info() const override {
        return info_;
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        if (!sourceReader_ || !isCapturing_) return false;

        // Read a sample from the camera
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

        if (FAILED(hr) || !sample) {
            return false;
        }

        // Get buffer from sample
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            sample->Release();
            return false;
        }

        // Lock buffer
        BYTE* data = nullptr;
        DWORD maxLength = 0, currentLength = 0;
        hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
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

        // Convert to RGBA
        std::vector<uint8_t> pixels(width * height * 4);
        convertToRGBA(data, pixels.data(), width, height);

        renderer.uploadTexturePixels(output, pixels.data(), width, height);

        buffer->Unlock();
        buffer->Release();
        sample->Release();

        hasNewFrame_.store(true);
        return true;
    }

    bool hasNewFrame() const override {
        return hasNewFrame_.load();
    }

private:
    bool openDevice(IMFActivate* device, const CameraConfig& config) {
        // Get friendly name
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;
        HRESULT hr = device->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendlyName, &nameLength);
        if (SUCCEEDED(hr) && friendlyName) {
            info_.deviceName = wideToUtf8(friendlyName);
            CoTaskMemFree(friendlyName);
        }

        // Create media source
        IMFMediaSource* mediaSource = nullptr;
        hr = device->ActivateObject(IID_PPV_ARGS(&mediaSource));
        if (!checkHR(hr, "ActivateObject")) {
            return false;
        }

        // Create source reader attributes
        IMFAttributes* readerAttributes = nullptr;
        hr = MFCreateAttributes(&readerAttributes, 2);
        if (FAILED(hr)) {
            mediaSource->Release();
            return false;
        }

        // Enable video processing for color conversion
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        // Create source reader
        hr = MFCreateSourceReaderFromMediaSource(mediaSource, readerAttributes, &sourceReader_);
        readerAttributes->Release();
        mediaSource->Release();

        if (!checkHR(hr, "MFCreateSourceReaderFromMediaSource")) {
            return false;
        }

        // Try to set the desired resolution and format
        if (!configureFormat(config)) {
            close();
            return false;
        }

        info_.isCapturing = false;

        std::cout << "[CameraCaptureWindows] Opened: " << info_.deviceName
                  << " (" << info_.width << "x" << info_.height
                  << " @ " << info_.frameRate << "fps)\n";

        return true;
    }

    bool configureFormat(const CameraConfig& config) {
        // First, enumerate native media types to find one matching requested resolution
        IMFMediaType* bestNativeType = nullptr;
        int bestScore = -1;
        DWORD bestTypeIndex = 0;

        for (DWORD i = 0; ; i++) {
            IMFMediaType* nativeType = nullptr;
            HRESULT hr = sourceReader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &nativeType);
            if (FAILED(hr)) break;

            UINT32 width = 0, height = 0;
            hr = MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height);
            if (FAILED(hr)) {
                nativeType->Release();
                continue;
            }

            UINT32 fpsNum = 0, fpsDenom = 1;
            MFGetAttributeRatio(nativeType, MF_MT_FRAME_RATE, &fpsNum, &fpsDenom);
            float fps = (fpsDenom > 0) ? static_cast<float>(fpsNum) / fpsDenom : 30.0f;

            // Score based on how close to requested resolution and frame rate
            int score = 0;
            if (static_cast<int>(width) == config.width && static_cast<int>(height) == config.height) {
                score += 1000;  // Exact resolution match
            } else if (static_cast<int>(width) >= config.width && static_cast<int>(height) >= config.height) {
                score += 500;   // At least as large as requested
            }
            // Prefer higher frame rates, up to the requested rate
            if (fps >= config.frameRate - 0.5f) {
                score += static_cast<int>(fps);
            }

            if (score > bestScore) {
                if (bestNativeType) bestNativeType->Release();
                bestNativeType = nativeType;
                bestScore = score;
                bestTypeIndex = i;
            } else {
                nativeType->Release();
            }
        }

        if (!bestNativeType) {
            std::cerr << "[CameraCaptureWindows] No native media types found\n";
            return false;
        }

        // Set the best native type first
        HRESULT hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, bestNativeType);
        bestNativeType->Release();

        if (FAILED(hr)) {
            std::cerr << "[CameraCaptureWindows] Failed to set native media type\n";
            return false;
        }

        // Now request RGB32 output (video processor will convert)
        IMFMediaType* outputType = nullptr;
        hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) return false;

        hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) {
            outputType->Release();
            return false;
        }

        // Try RGB32 (BGRA) first
        bool formatSet = false;
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) {
            hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
            if (SUCCEEDED(hr)) {
                formatSet = true;
                pixelFormat_ = PixelFormat::BGRA;
            }
        }

        // Try RGB24
        if (!formatSet) {
            hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
            if (SUCCEEDED(hr)) {
                hr = sourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
                if (SUCCEEDED(hr)) {
                    formatSet = true;
                    pixelFormat_ = PixelFormat::RGB24;
                }
            }
        }

        outputType->Release();

        if (!formatSet) {
            std::cerr << "[CameraCaptureWindows] No compatible output format found\n";
            return false;
        }

        // Get actual format
        IMFMediaType* actualType = nullptr;
        hr = sourceReader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
        if (FAILED(hr)) return false;

        // Get dimensions
        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &width, &height);
        if (SUCCEEDED(hr)) {
            info_.width = static_cast<int>(width);
            info_.height = static_cast<int>(height);
        }

        // Get actual frame rate
        UINT32 fpsNum = 0, fpsDenom = 1;
        hr = MFGetAttributeRatio(actualType, MF_MT_FRAME_RATE, &fpsNum, &fpsDenom);
        if (SUCCEEDED(hr) && fpsDenom > 0) {
            info_.frameRate = static_cast<float>(fpsNum) / fpsDenom;
        }

        // Get stride
        LONG stride = 0;
        hr = actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride));
        if (SUCCEEDED(hr)) {
            stride_ = stride;
        } else {
            stride_ = (pixelFormat_ == PixelFormat::RGB24) ? info_.width * 3 : info_.width * 4;
        }

        actualType->Release();
        return true;
    }

    void convertToRGBA(const uint8_t* src, uint8_t* dst, int width, int height) {
        LONG absStride = (stride_ < 0) ? -stride_ : stride_;
        if (absStride == 0) {
            absStride = (pixelFormat_ == PixelFormat::RGB24) ? width * 3 : width * 4;
        }

        bool bottomUp = (stride_ < 0);

        for (int y = 0; y < height; y++) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            const uint8_t* srcRow = src + srcY * absStride;
            uint8_t* dstRow = dst + y * width * 4;

            for (int x = 0; x < width; x++) {
                switch (pixelFormat_) {
                    case PixelFormat::BGRA:
                        // BGRA -> RGBA
                        dstRow[0] = srcRow[2];  // R <- B
                        dstRow[1] = srcRow[1];  // G <- G
                        dstRow[2] = srcRow[0];  // B <- R
                        dstRow[3] = srcRow[3];  // A <- A
                        srcRow += 4;
                        break;

                    case PixelFormat::RGB24:
                        // BGR -> RGBA (RGB24 is actually BGR in memory on Windows)
                        dstRow[0] = srcRow[2];  // R <- B
                        dstRow[1] = srcRow[1];  // G <- G
                        dstRow[2] = srcRow[0];  // B <- R
                        dstRow[3] = 255;        // A (opaque)
                        srcRow += 3;
                        break;

                    default:
                        // Fallback - treat as BGRA
                        dstRow[0] = srcRow[2];
                        dstRow[1] = srcRow[1];
                        dstRow[2] = srcRow[0];
                        dstRow[3] = 255;
                        srcRow += 4;
                        break;
                }
                dstRow += 4;
            }
        }
    }

    bool comInitialized_ = false;
    bool mfInitialized_ = false;
    IMFSourceReader* sourceReader_ = nullptr;

    CameraInfo info_;
    bool isCapturing_ = false;
    std::atomic<bool> hasNewFrame_{false};
    mutable std::mutex frameMutex_;
    std::vector<uint8_t> latestFrame_;

    PixelFormat pixelFormat_ = PixelFormat::BGRA;
    LONG stride_ = 0;
};

std::unique_ptr<CameraCapture> CameraCapture::create() {
    return std::make_unique<CameraCaptureWindows>();
}

} // namespace vivid

#endif // _WIN32
