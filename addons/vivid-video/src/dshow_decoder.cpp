// DirectShow Video Decoder for Windows
// Fallback for codecs not supported by Media Foundation
// Uses DirectShow filter graph with Sample Grabber for frame capture

#if defined(_WIN32)

#include <vivid/video/dshow_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <comdef.h>

// qedit.h is deprecated but still works - need to define these before including
// or define the interfaces ourselves
#pragma warning(push)
#pragma warning(disable: 4091)  // 'typedef ': ignored on left of '' when no variable is declared

// Sample Grabber GUIDs (from qedit.h which is deprecated)
static const GUID CLSID_SampleGrabber = { 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const GUID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const GUID IID_ISampleGrabber = { 0x6B652FFF, 0x11FE, 0x4FCE, { 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
static const GUID IID_ISampleGrabberCB = { 0x0579154A, 0x2B53, 0x4994, { 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85 } };

// Forward declare the callback interface
interface ISampleGrabberCB : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample* pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen) = 0;
};

// Sample Grabber interface (from deprecated qedit.h)
interface ISampleGrabber : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* pBufferSize, long* pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, long WhichMethodToCallback) = 0;
};

#pragma warning(pop)

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "quartz.lib")

namespace vivid::video {

// Helper to check HRESULT
static bool checkHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::cerr << "[DShowDecoder] " << operation << " failed: "
                  << err.ErrorMessage() << " (0x" << std::hex << hr << std::dec << ")\n";
        return false;
    }
    return true;
}

// Sample grabber callback for receiving frames
class SampleGrabberCallback : public ISampleGrabberCB {
public:
    SampleGrabberCallback() : refCount_(1) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_ISampleGrabberCB) {
            *ppv = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&refCount_); }
    STDMETHODIMP_(ULONG) Release() {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // ISampleGrabberCB
    STDMETHODIMP SampleCB(double time, IMediaSample* pSample) { return S_OK; }

    STDMETHODIMP BufferCB(double time, BYTE* pBuffer, long bufferLen) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pBuffer && bufferLen > 0) {
            frameBuffer_.assign(pBuffer, pBuffer + bufferLen);
            sampleTime_ = time;
            hasNewFrame_ = true;
        }
        return S_OK;
    }

    bool getFrame(std::vector<uint8_t>& buffer, double& time) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!hasNewFrame_) return false;
        buffer.swap(frameBuffer_);
        time = sampleTime_;
        hasNewFrame_ = false;
        return true;
    }

private:
    LONG refCount_;
    std::mutex mutex_;
    std::vector<uint8_t> frameBuffer_;
    double sampleTime_ = 0.0;
    bool hasNewFrame_ = false;
};

struct DShowDecoder::Impl {
    IGraphBuilder* graphBuilder = nullptr;
    IMediaControl* mediaControl = nullptr;
    IMediaSeeking* mediaSeeking = nullptr;
    IMediaEventEx* mediaEvent = nullptr;
    IBaseFilter* sourceFilter = nullptr;
    IBaseFilter* grabberFilter = nullptr;
    IBaseFilter* nullRenderer = nullptr;
    ISampleGrabber* sampleGrabber = nullptr;
    SampleGrabberCallback* callback = nullptr;

    std::chrono::steady_clock::time_point lastUpdateTime;
    bool bottomUp = true;  // DIB format is usually bottom-up
};

DShowDecoder::DShowDecoder() : impl_(std::make_unique<Impl>()) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

DShowDecoder::~DShowDecoder() {
    close();
    CoUninitialize();
}

bool DShowDecoder::canDecode(const std::string& path) {
    // Check file extension for codecs that DirectShow might handle better
    std::string ext = path;
    auto pos = ext.rfind('.');
    if (pos != std::string::npos) {
        ext = ext.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // MOV files often contain codecs MF doesn't support (ProRes, etc.)
    if (ext == ".mov") {
        // Check for ProRes or other Apple codecs in the file
        std::ifstream file(path, std::ios::binary);
        if (file.is_open()) {
            char buffer[8192];
            file.read(buffer, sizeof(buffer));
            std::string data(buffer, file.gcount());

            // Look for ProRes codec identifiers
            if (data.find("apcn") != std::string::npos ||  // ProRes 422
                data.find("apcs") != std::string::npos ||  // ProRes 422 LT
                data.find("apco") != std::string::npos ||  // ProRes 422 Proxy
                data.find("apch") != std::string::npos ||  // ProRes 422 HQ
                data.find("ap4h") != std::string::npos ||  // ProRes 4444
                data.find("ap4x") != std::string::npos) {  // ProRes 4444 XQ
                return true;
            }
        }
    }

    return false;
}

bool DShowDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    filePath_ = path;
    isLooping_ = loop;
    device_ = ctx.device();
    queue_ = ctx.queue();

    HRESULT hr;

    // Create filter graph
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IGraphBuilder, (void**)&impl_->graphBuilder);
    if (!checkHR(hr, "Create FilterGraph")) return false;

    // Get interfaces
    hr = impl_->graphBuilder->QueryInterface(IID_IMediaControl, (void**)&impl_->mediaControl);
    if (!checkHR(hr, "Get IMediaControl")) { close(); return false; }

    hr = impl_->graphBuilder->QueryInterface(IID_IMediaSeeking, (void**)&impl_->mediaSeeking);
    if (!checkHR(hr, "Get IMediaSeeking")) { close(); return false; }

    hr = impl_->graphBuilder->QueryInterface(IID_IMediaEventEx, (void**)&impl_->mediaEvent);
    if (!checkHR(hr, "Get IMediaEventEx")) { close(); return false; }

    // Convert path to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    // Add source filter
    hr = impl_->graphBuilder->AddSourceFilter(widePath.c_str(), L"Source", &impl_->sourceFilter);
    if (!checkHR(hr, "Add source filter")) { close(); return false; }

    // Create Sample Grabber
    hr = CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, (void**)&impl_->grabberFilter);
    if (!checkHR(hr, "Create SampleGrabber")) { close(); return false; }

    hr = impl_->graphBuilder->AddFilter(impl_->grabberFilter, L"Sample Grabber");
    if (!checkHR(hr, "Add SampleGrabber")) { close(); return false; }

    hr = impl_->grabberFilter->QueryInterface(IID_ISampleGrabber, (void**)&impl_->sampleGrabber);
    if (!checkHR(hr, "Get ISampleGrabber")) { close(); return false; }

    // Configure sample grabber for RGB24
    AM_MEDIA_TYPE mt = {};
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB24;
    mt.formattype = FORMAT_VideoInfo;

    hr = impl_->sampleGrabber->SetMediaType(&mt);
    if (!checkHR(hr, "Set media type")) { close(); return false; }

    // Create null renderer (we grab frames via callback, don't need to render)
    hr = CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, (void**)&impl_->nullRenderer);
    if (!checkHR(hr, "Create NullRenderer")) { close(); return false; }

    hr = impl_->graphBuilder->AddFilter(impl_->nullRenderer, L"Null Renderer");
    if (!checkHR(hr, "Add NullRenderer")) { close(); return false; }

    // Render the file - this connects the filter graph
    // Try to connect source -> sample grabber -> null renderer
    IEnumPins* enumPins = nullptr;
    IPin* sourceOutPin = nullptr;
    IPin* grabberInPin = nullptr;
    IPin* grabberOutPin = nullptr;
    IPin* nullInPin = nullptr;

    // Find source output pin
    hr = impl_->sourceFilter->EnumPins(&enumPins);
    if (SUCCEEDED(hr)) {
        IPin* pin;
        while (enumPins->Next(1, &pin, nullptr) == S_OK) {
            PIN_DIRECTION dir;
            pin->QueryDirection(&dir);
            if (dir == PINDIR_OUTPUT) {
                // Check if this is a video pin
                IEnumMediaTypes* enumMT;
                if (SUCCEEDED(pin->EnumMediaTypes(&enumMT))) {
                    AM_MEDIA_TYPE* pmt;
                    while (enumMT->Next(1, &pmt, nullptr) == S_OK) {
                        if (pmt->majortype == MEDIATYPE_Video) {
                            sourceOutPin = pin;
                            sourceOutPin->AddRef();
                        }
                        CoTaskMemFree(pmt->pbFormat);
                        CoTaskMemFree(pmt);
                        if (sourceOutPin) break;
                    }
                    enumMT->Release();
                }
            }
            pin->Release();
            if (sourceOutPin) break;
        }
        enumPins->Release();
    }

    // Find grabber pins
    hr = impl_->grabberFilter->EnumPins(&enumPins);
    if (SUCCEEDED(hr)) {
        IPin* pin;
        while (enumPins->Next(1, &pin, nullptr) == S_OK) {
            PIN_DIRECTION dir;
            pin->QueryDirection(&dir);
            if (dir == PINDIR_INPUT && !grabberInPin) {
                grabberInPin = pin;
                grabberInPin->AddRef();
            } else if (dir == PINDIR_OUTPUT && !grabberOutPin) {
                grabberOutPin = pin;
                grabberOutPin->AddRef();
            }
            pin->Release();
        }
        enumPins->Release();
    }

    // Find null renderer input pin
    hr = impl_->nullRenderer->EnumPins(&enumPins);
    if (SUCCEEDED(hr)) {
        IPin* pin;
        while (enumPins->Next(1, &pin, nullptr) == S_OK) {
            PIN_DIRECTION dir;
            pin->QueryDirection(&dir);
            if (dir == PINDIR_INPUT) {
                nullInPin = pin;
                nullInPin->AddRef();
            }
            pin->Release();
            if (nullInPin) break;
        }
        enumPins->Release();
    }

    // Try intelligent connect through the graph
    if (sourceOutPin) {
        // Let the graph builder find the right decoders
        hr = impl_->graphBuilder->Render(sourceOutPin);
        if (FAILED(hr)) {
            std::cerr << "[DShowDecoder] Failed to render source pin - codec not installed?\n";
            if (sourceOutPin) sourceOutPin->Release();
            if (grabberInPin) grabberInPin->Release();
            if (grabberOutPin) grabberOutPin->Release();
            if (nullInPin) nullInPin->Release();
            close();
            return false;
        }
    }

    if (sourceOutPin) sourceOutPin->Release();
    if (grabberInPin) grabberInPin->Release();
    if (grabberOutPin) grabberOutPin->Release();
    if (nullInPin) nullInPin->Release();

    // Get actual media type
    AM_MEDIA_TYPE actualMT = {};
    hr = impl_->sampleGrabber->GetConnectedMediaType(&actualMT);
    if (SUCCEEDED(hr) && actualMT.formattype == FORMAT_VideoInfo) {
        VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)actualMT.pbFormat;
        width_ = vih->bmiHeader.biWidth;
        height_ = std::abs(vih->bmiHeader.biHeight);
        impl_->bottomUp = vih->bmiHeader.biHeight > 0;

        if (vih->AvgTimePerFrame > 0) {
            frameRate_ = 10000000.0f / vih->AvgTimePerFrame;
        }

        if (actualMT.pbFormat) CoTaskMemFree(actualMT.pbFormat);
    } else {
        // Fallback
        width_ = 1920;
        height_ = 1080;
        frameRate_ = 30.0f;
    }

    // Get duration
    LONGLONG duration = 0;
    hr = impl_->mediaSeeking->GetDuration(&duration);
    if (SUCCEEDED(hr)) {
        duration_ = duration / 10000000.0f;
    }

    // Set up callback
    impl_->callback = new SampleGrabberCallback();
    impl_->sampleGrabber->SetBufferSamples(FALSE);
    impl_->sampleGrabber->SetOneShot(FALSE);
    impl_->sampleGrabber->SetCallback(impl_->callback, 1);  // BufferCB

    // Create texture
    createTexture();
    pixelBuffer_.resize(width_ * height_ * 4);

    impl_->lastUpdateTime = std::chrono::steady_clock::now();
    isPlaying_ = false;
    isFinished_ = false;
    currentTime_ = 0.0f;

    std::cout << "[DShowDecoder] Opened " << path
              << " (" << width_ << "x" << height_
              << ", " << frameRate_ << "fps"
              << ", " << duration_ << "s)\n";

    return true;
}

void DShowDecoder::close() {
    if (impl_->mediaControl) {
        impl_->mediaControl->Stop();
    }

    if (impl_->sampleGrabber) {
        impl_->sampleGrabber->SetCallback(nullptr, 0);
    }

    if (impl_->callback) {
        impl_->callback->Release();
        impl_->callback = nullptr;
    }

    if (impl_->nullRenderer) {
        impl_->nullRenderer->Release();
        impl_->nullRenderer = nullptr;
    }

    if (impl_->sampleGrabber) {
        impl_->sampleGrabber->Release();
        impl_->sampleGrabber = nullptr;
    }

    if (impl_->grabberFilter) {
        impl_->grabberFilter->Release();
        impl_->grabberFilter = nullptr;
    }

    if (impl_->sourceFilter) {
        impl_->sourceFilter->Release();
        impl_->sourceFilter = nullptr;
    }

    if (impl_->mediaEvent) {
        impl_->mediaEvent->Release();
        impl_->mediaEvent = nullptr;
    }

    if (impl_->mediaSeeking) {
        impl_->mediaSeeking->Release();
        impl_->mediaSeeking = nullptr;
    }

    if (impl_->mediaControl) {
        impl_->mediaControl->Release();
        impl_->mediaControl = nullptr;
    }

    if (impl_->graphBuilder) {
        impl_->graphBuilder->Release();
        impl_->graphBuilder = nullptr;
    }

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

bool DShowDecoder::isOpen() const {
    return impl_->graphBuilder != nullptr;
}

void DShowDecoder::createTexture() {
    if (texture_) {
        wgpuTextureViewRelease(textureView_);
        wgpuTextureRelease(texture_);
    }

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = { "DShowDecoder Texture", WGPU_STRLEN };
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = { static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = { "DShowDecoder TextureView", WGPU_STRLEN };
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

void DShowDecoder::resetPlayback() {
    if (!impl_->mediaSeeking) return;

    LONGLONG pos = 0;
    impl_->mediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
                                       nullptr, AM_SEEKING_NoPositioning);
    currentTime_ = 0.0f;
    isFinished_ = false;
}

void DShowDecoder::update(Context& ctx) {
    if (!impl_->graphBuilder || !isPlaying_) return;

    // Check for end of stream
    if (impl_->mediaEvent) {
        long evCode;
        LONG_PTR param1, param2;
        while (impl_->mediaEvent->GetEvent(&evCode, &param1, &param2, 0) == S_OK) {
            impl_->mediaEvent->FreeEventParams(evCode, param1, param2);
            if (evCode == EC_COMPLETE) {
                if (isLooping_) {
                    resetPlayback();
                    impl_->mediaControl->Run();
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                }
            }
        }
    }

    // Get current position
    if (impl_->mediaSeeking) {
        LONGLONG pos = 0;
        impl_->mediaSeeking->GetCurrentPosition(&pos);
        currentTime_ = pos / 10000000.0f;
    }

    // Check for new frame from callback
    std::vector<uint8_t> frameData;
    double sampleTime;
    if (impl_->callback && impl_->callback->getFrame(frameData, sampleTime)) {
        // Convert RGB24 (BGR) to RGBA
        size_t expectedSize = width_ * height_ * 3;
        if (frameData.size() >= expectedSize) {
            for (int y = 0; y < height_; y++) {
                int srcY = impl_->bottomUp ? (height_ - 1 - y) : y;
                const uint8_t* src = frameData.data() + srcY * width_ * 3;
                uint8_t* dst = pixelBuffer_.data() + y * width_ * 4;

                for (int x = 0; x < width_; x++) {
                    dst[0] = src[2];  // R <- B (BGR to RGB)
                    dst[1] = src[1];  // G
                    dst[2] = src[0];  // B <- R
                    dst[3] = 255;     // A
                    src += 3;
                    dst += 4;
                }
            }

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

            wgpuQueueWriteTexture(queue_, &destination, pixelBuffer_.data(),
                                  pixelBuffer_.size(), &dataLayout, &writeSize);
        }
    }
}

void DShowDecoder::seek(float seconds) {
    if (!impl_->mediaSeeking) return;

    LONGLONG pos = static_cast<LONGLONG>(seconds * 10000000.0);
    impl_->mediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
                                       nullptr, AM_SEEKING_NoPositioning);
    currentTime_ = seconds;
    isFinished_ = false;
}

void DShowDecoder::play() {
    if (!impl_->mediaControl) return;

    if (isFinished_ && !isLooping_) {
        resetPlayback();
    }

    impl_->mediaControl->Run();
    isPlaying_ = true;
    impl_->lastUpdateTime = std::chrono::steady_clock::now();
}

void DShowDecoder::pause() {
    if (!impl_->mediaControl) return;

    impl_->mediaControl->Pause();
    isPlaying_ = false;
}

void DShowDecoder::setVolume(float volume) {
    // TODO: Implement audio volume control via IBasicAudio
}

float DShowDecoder::getVolume() const {
    return 1.0f;
}

uint32_t DShowDecoder::readAudioSamples(float* buffer, uint32_t maxFrames) {
    return 0;  // Not implemented
}

void DShowDecoder::setInternalAudioEnabled(bool enable) {
    internalAudioEnabled_ = enable;
}

bool DShowDecoder::isInternalAudioEnabled() const {
    return internalAudioEnabled_;
}

uint32_t DShowDecoder::audioSampleRate() const {
    return audioSampleRate_;
}

uint32_t DShowDecoder::audioChannels() const {
    return audioChannels_;
}

} // namespace vivid::video

#endif // _WIN32
