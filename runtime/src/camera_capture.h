#pragma once
#include <vivid/types.h>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace vivid {

class Renderer;

// Use CameraDevice from types.h, but we need an internal alias for compatibility
using CameraDeviceInfo = CameraDevice;

/**
 * @brief Camera capture configuration.
 */
struct CameraConfig {
    int width = 1280;           ///< Requested capture width
    int height = 720;           ///< Requested capture height
    float frameRate = 30.0f;    ///< Requested frame rate
};

// CameraInfo is defined in types.h

/**
 * @brief Abstract interface for camera capture.
 *
 * Platform-specific implementations:
 * - macOS: AVFoundation AVCaptureSession (CameraCaptureMacOS)
 * - Windows: Media Foundation (CameraCaptureWindows)
 * - Linux: V4L2 (CameraCaptureLinux)
 */
class CameraCapture {
public:
    virtual ~CameraCapture() = default;

    // Non-copyable
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    /**
     * @brief Enumerate available camera devices.
     * @return List of available cameras.
     */
    virtual std::vector<CameraDeviceInfo> enumerateDevices() = 0;

    /**
     * @brief Open the default camera.
     * @param config Capture configuration (resolution, framerate).
     * @return true if opened successfully.
     */
    virtual bool open(const CameraConfig& config = CameraConfig{}) = 0;

    /**
     * @brief Open a specific camera by device ID.
     * @param deviceId Device identifier from enumerateDevices().
     * @param config Capture configuration.
     * @return true if opened successfully.
     */
    virtual bool open(const std::string& deviceId, const CameraConfig& config = CameraConfig{}) = 0;

    /**
     * @brief Open a camera by index (0 = first camera).
     * @param index Camera index.
     * @param config Capture configuration.
     * @return true if opened successfully.
     */
    virtual bool openByIndex(int index, const CameraConfig& config = CameraConfig{}) = 0;

    /**
     * @brief Close the camera and release resources.
     */
    virtual void close() = 0;

    /**
     * @brief Check if a camera is currently open.
     */
    virtual bool isOpen() const = 0;

    /**
     * @brief Start capturing frames.
     * @return true if capture started successfully.
     */
    virtual bool startCapture() = 0;

    /**
     * @brief Stop capturing frames.
     */
    virtual void stopCapture() = 0;

    /**
     * @brief Check if actively capturing.
     */
    virtual bool isCapturing() const = 0;

    /**
     * @brief Get camera info.
     * @return CameraInfo struct with dimensions, framerate, etc.
     */
    virtual const CameraInfo& info() const = 0;

    /**
     * @brief Get the latest frame and upload to texture.
     * @param output Texture to receive the frame.
     * @param renderer Renderer for texture operations.
     * @return true if a new frame was available and uploaded.
     *
     * This does not block - if no new frame is available since the last
     * call, returns false and leaves the texture unchanged.
     */
    virtual bool getFrame(Texture& output, Renderer& renderer) = 0;

    /**
     * @brief Check if a new frame is available.
     * @return true if getFrame() would return a new frame.
     */
    virtual bool hasNewFrame() const = 0;

    /**
     * @brief Create a platform-appropriate CameraCapture instance.
     * @return Unique pointer to a CameraCapture implementation.
     *
     * Returns:
     * - CameraCaptureMacOS on macOS (AVFoundation)
     * - CameraCaptureWindows on Windows (Media Foundation)
     * - CameraCaptureLinux on Linux (V4L2)
     */
    static std::unique_ptr<CameraCapture> create();

protected:
    CameraCapture() = default;
};

} // namespace vivid
