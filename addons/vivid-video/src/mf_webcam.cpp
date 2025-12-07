// Media Foundation Webcam Capture - Windows camera capture (stub)
// TODO: Implement using Media Foundation IMFSourceReader

#if defined(_WIN32)

#include <vivid/video/mf_webcam.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::video {

struct MFWebcam::Impl {
    // Stub - no implementation yet
};

MFWebcam::MFWebcam() : impl_(std::make_unique<Impl>()) {}

MFWebcam::~MFWebcam() {
    close();
}

std::vector<CameraDevice> MFWebcam::enumerateDevices() {
    std::cerr << "[MFWebcam] Webcam not yet implemented on Windows" << std::endl;
    return {};
}

bool MFWebcam::open(Context& ctx, int width, int height, float fps) {
    std::cerr << "[MFWebcam] Webcam not yet implemented on Windows" << std::endl;
    return false;
}

bool MFWebcam::open(Context& ctx, const std::string& deviceId, int width, int height, float fps) {
    std::cerr << "[MFWebcam] Webcam not yet implemented on Windows" << std::endl;
    return false;
}

bool MFWebcam::openByIndex(Context& ctx, int index, int width, int height, float fps) {
    std::cerr << "[MFWebcam] Webcam not yet implemented on Windows" << std::endl;
    return false;
}

void MFWebcam::close() {
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
    isCapturing_ = false;
}

bool MFWebcam::isOpen() const {
    return false;
}

bool MFWebcam::update(Context& ctx) {
    return false;
}

bool MFWebcam::startCapture() {
    return false;
}

void MFWebcam::stopCapture() {
    isCapturing_ = false;
}

} // namespace vivid::video

#endif // _WIN32
