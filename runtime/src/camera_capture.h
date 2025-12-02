#pragma once
#include <vivid/types.h>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <set>
#include <algorithm>

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

/**
 * @brief Available camera capture mode (resolution + frame rate).
 */
struct CameraMode {
    int width;                  ///< Resolution width
    int height;                 ///< Resolution height
    float minFrameRate;         ///< Minimum supported frame rate
    float maxFrameRate;         ///< Maximum supported frame rate
    std::string pixelFormat;    ///< Pixel format (e.g., "BGRA", "YUV420")
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
     * @brief Enumerate available capture modes for a device.
     * @param deviceId Device identifier (empty string for default device).
     * @return List of available modes (resolution + frame rate combinations).
     */
    virtual std::vector<CameraMode> enumerateModes(const std::string& deviceId = "") = 0;

    /**
     * @brief Print all available modes for a device to stdout.
     * @param deviceId Device identifier (empty string for default device).
     */
    void printModes(const std::string& deviceId = "") {
        auto modes = enumerateModes(deviceId);

        if (modes.empty()) {
            std::cout << "[CameraCapture] No modes available\n";
            return;
        }

        // Group by resolution, collect unique fps values
        struct ResolutionModes {
            int width, height;
            std::set<std::pair<float, float>> fpsRanges;  // min, max
            std::set<std::string> formats;
        };
        std::vector<ResolutionModes> grouped;

        for (const auto& mode : modes) {
            // Find or create resolution group
            auto it = std::find_if(grouped.begin(), grouped.end(),
                [&](const ResolutionModes& rm) {
                    return rm.width == mode.width && rm.height == mode.height;
                });

            if (it == grouped.end()) {
                ResolutionModes rm;
                rm.width = mode.width;
                rm.height = mode.height;
                rm.fpsRanges.insert({mode.minFrameRate, mode.maxFrameRate});
                rm.formats.insert(mode.pixelFormat);
                grouped.push_back(rm);
            } else {
                it->fpsRanges.insert({mode.minFrameRate, mode.maxFrameRate});
                it->formats.insert(mode.pixelFormat);
            }
        }

        // Sort by resolution (descending)
        std::sort(grouped.begin(), grouped.end(),
            [](const ResolutionModes& a, const ResolutionModes& b) {
                return (a.width * a.height) > (b.width * b.height);
            });

        std::cout << "\n[CameraCapture] Available modes:\n";
        std::cout << std::string(60, '-') << "\n";

        for (const auto& rm : grouped) {
            std::cout << "  " << std::setw(4) << rm.width << " x " << std::setw(4) << rm.height << "  |  ";

            // Print fps options
            std::cout << "fps: ";
            bool first = true;
            for (const auto& fps : rm.fpsRanges) {
                if (!first) std::cout << ", ";
                if (fps.first == fps.second) {
                    std::cout << static_cast<int>(fps.first);
                } else {
                    std::cout << static_cast<int>(fps.first) << "-" << static_cast<int>(fps.second);
                }
                first = false;
            }

            // Print formats
            std::cout << "  |  ";
            first = true;
            for (const auto& fmt : rm.formats) {
                if (!first) std::cout << ", ";
                std::cout << fmt;
                first = false;
            }
            std::cout << "\n";
        }
        std::cout << std::string(60, '-') << "\n\n";
    }

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
