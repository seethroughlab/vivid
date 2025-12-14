// Vivid Video - Webcam Operator Implementation

#include <vivid/video/webcam.h>
#include <vivid/context.h>
#include <iostream>

#if defined(__APPLE__)
#include <vivid/video/avf_webcam.h>
using WebcamBackend = vivid::video::AVFWebcam;
#elif defined(_WIN32)
#include <vivid/video/mf_webcam.h>
using WebcamBackend = vivid::video::MFWebcam;
#endif

namespace vivid::video {

Webcam::Webcam() = default;

Webcam::~Webcam() {
    cleanup();
}

void Webcam::setDevice(int index) {
    m_deviceIndex = index;
    m_useDeviceId = false;
    m_needsReopen = true;
}

void Webcam::setDevice(const std::string& deviceId) {
    m_deviceId = deviceId;
    m_useDeviceId = true;
    m_needsReopen = true;
}

void Webcam::setResolution(int width, int height) {
    m_requestedWidth = width;
    m_requestedHeight = height;
    m_needsReopen = true;
}

void Webcam::setFrameRate(float fps) {
    m_requestedFps = fps;
    m_needsReopen = true;
}

void Webcam::listDevices() {
#if defined(__APPLE__) || defined(_WIN32)
    auto devices = WebcamBackend::enumerateDevices();
    if (devices.empty()) {
        std::cout << "[Webcam] No cameras found" << std::endl;
        return;
    }

    std::cout << "[Webcam] Available cameras:" << std::endl;
    for (size_t i = 0; i < devices.size(); i++) {
        std::cout << "  [" << i << "] " << devices[i].name;
        if (devices[i].isDefault) std::cout << " (default)";
        std::cout << std::endl;
    }
#else
    std::cout << "[Webcam] Camera enumeration not available on this platform" << std::endl;
#endif
}

bool Webcam::isCapturing() const {
#if defined(__APPLE__) || defined(_WIN32)
    return m_webcam && m_webcam->isCapturing();
#else
    return false;
#endif
}

const uint8_t* Webcam::cpuPixelData() const {
#if defined(__APPLE__) || defined(_WIN32)
    return m_webcam ? m_webcam->cpuPixelData() : nullptr;
#else
    return nullptr;
#endif
}

size_t Webcam::cpuPixelDataSize() const {
#if defined(__APPLE__) || defined(_WIN32)
    return m_webcam ? m_webcam->cpuPixelDataSize() : 0;
#else
    return 0;
#endif
}

void Webcam::init(Context& ctx) {
    openCamera(ctx);
}

void Webcam::openCamera(Context& ctx) {
#if defined(__APPLE__) || defined(_WIN32)
    // Close existing webcam
    if (m_webcam) {
        m_webcam->close();
        m_webcam.reset();
    }

    // List available cameras
    auto devices = WebcamBackend::enumerateDevices();
    if (devices.empty()) {
        std::cerr << "[Webcam] No cameras found" << std::endl;
        return;
    }

    std::cout << "[Webcam] Available cameras:" << std::endl;
    for (size_t i = 0; i < devices.size(); i++) {
        std::cout << "  [" << i << "] " << devices[i].name;
        if (devices[i].isDefault) std::cout << " (default)";
        std::cout << std::endl;
    }

    // Create webcam backend
    m_webcam = std::make_unique<WebcamBackend>();

    bool success = false;
    if (m_useDeviceId && !m_deviceId.empty()) {
        success = m_webcam->open(ctx, m_deviceId, m_requestedWidth, m_requestedHeight, m_requestedFps);
    } else {
        int index = m_deviceIndex;
        if (index < 0 || index >= static_cast<int>(devices.size())) {
            index = 0;
        }
        success = m_webcam->openByIndex(ctx, index, m_requestedWidth, m_requestedHeight, m_requestedFps);
    }

    if (success) {
        m_captureWidth = m_webcam->width();
        m_captureHeight = m_webcam->height();
        m_captureFrameRate = m_webcam->frameRate();

        // Set output dimensions
        m_width = m_captureWidth;
        m_height = m_captureHeight;
        m_output = m_webcam->texture();
        m_outputView = m_webcam->textureView();
    } else {
        std::cerr << "[Webcam] Failed to open camera" << std::endl;
        m_webcam.reset();
    }
#else
    std::cerr << "[Webcam] Not supported on this platform" << std::endl;
#endif
}

void Webcam::process(Context& ctx) {
    // Webcam uses camera resolution - no auto-resize

    // Webcam is streaming - always cooks

#if defined(__APPLE__) || defined(_WIN32)
    // Reopen camera if needed
    if (m_needsReopen) {
        openCamera(ctx);
        m_needsReopen = false;
    }

    if (!m_webcam) {
        didCook();
        return;
    }

    // Get new frame (non-blocking)
    m_webcam->update(ctx);

    // Update output texture view (may have changed)
    m_outputView = m_webcam->textureView();
#endif

    didCook();
}

void Webcam::cleanup() {
#if defined(__APPLE__) || defined(_WIN32)
    if (m_webcam) {
        m_webcam->close();
        m_webcam.reset();
    }
#endif
    m_output = nullptr;
    m_outputView = nullptr;
}

} // namespace vivid::video
