#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::video {

/**
 * @brief Camera device information.
 */
struct CameraDevice {
    std::string deviceId;   ///< Unique device identifier
    std::string name;       ///< Human-readable name
    bool isDefault = false; ///< Is the system default camera
};

/**
 * @brief AVFoundation webcam capture for macOS.
 *
 * Uses AVCaptureSession to capture frames from the camera and uploads
 * to a GPU texture in RGBA format.
 */
class AVFWebcam {
public:
    AVFWebcam();
    ~AVFWebcam();

    // Non-copyable
    AVFWebcam(const AVFWebcam&) = delete;
    AVFWebcam& operator=(const AVFWebcam&) = delete;

    /**
     * @brief Enumerate available camera devices.
     */
    static std::vector<CameraDevice> enumerateDevices();

    /**
     * @brief Open the default camera.
     */
    bool open(Context& ctx, int width = 1280, int height = 720, float fps = 30.0f);

    /**
     * @brief Open a camera by device ID.
     */
    bool open(Context& ctx, const std::string& deviceId, int width = 1280, int height = 720, float fps = 30.0f);

    /**
     * @brief Open a camera by index (0 = first camera).
     */
    bool openByIndex(Context& ctx, int index, int width = 1280, int height = 720, float fps = 30.0f);

    /**
     * @brief Close and release resources.
     */
    void close();

    /**
     * @brief Check if camera is open.
     */
    bool isOpen() const;

    /**
     * @brief Check if capturing.
     */
    bool isCapturing() const { return isCapturing_; }

    /**
     * @brief Update - get latest frame and upload to texture.
     * @return true if a new frame was available.
     */
    bool update(Context& ctx);

    /**
     * @brief Start capture.
     */
    bool startCapture();

    /**
     * @brief Stop capture.
     */
    void stopCapture();

    // Accessors
    int width() const { return width_; }
    int height() const { return height_; }
    float frameRate() const { return frameRate_; }
    const std::string& deviceName() const { return deviceName_; }

    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Camera info
    int width_ = 0;
    int height_ = 0;
    float frameRate_ = 30.0f;
    std::string deviceName_;
    bool isCapturing_ = false;

    // Pixel buffer for frames
    std::vector<uint8_t> pixelBuffer_;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    void createTexture();
    bool setupCapture(void* device, int width, int height, float fps);
};

} // namespace vivid::video
