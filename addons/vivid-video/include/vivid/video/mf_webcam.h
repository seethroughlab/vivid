#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::video {

// Forward declaration - CameraDevice defined in avf_webcam.h
// We re-declare it here for Windows builds
#ifndef __APPLE__
struct CameraDevice {
    std::string deviceId;
    std::string name;
    bool isDefault = false;
};
#endif

/**
 * @brief Media Foundation webcam capture for Windows.
 *
 * Uses Media Foundation to capture frames from the camera.
 * Stub implementation - not yet functional.
 */
class MFWebcam {
public:
    MFWebcam();
    ~MFWebcam();

    // Non-copyable
    MFWebcam(const MFWebcam&) = delete;
    MFWebcam& operator=(const MFWebcam&) = delete;

    static std::vector<CameraDevice> enumerateDevices();

    bool open(Context& ctx, int width = 1280, int height = 720, float fps = 30.0f);
    bool open(Context& ctx, const std::string& deviceId, int width = 1280, int height = 720, float fps = 30.0f);
    bool openByIndex(Context& ctx, int index, int width = 1280, int height = 720, float fps = 30.0f);

    void close();
    bool isOpen() const;
    bool isCapturing() const { return isCapturing_; }

    bool update(Context& ctx);
    bool startCapture();
    void stopCapture();

    int width() const { return width_; }
    int height() const { return height_; }
    float frameRate() const { return frameRate_; }
    const std::string& deviceName() const { return deviceName_; }

    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    int width_ = 0;
    int height_ = 0;
    float frameRate_ = 30.0f;
    std::string deviceName_;
    bool isCapturing_ = false;

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    void createTexture();
    bool configureSourceReader(int requestedWidth, int requestedHeight, float requestedFps);
};

} // namespace vivid::video
