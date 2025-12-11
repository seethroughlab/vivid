#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/video/export.h>
#include <string>
#include <memory>

namespace vivid::video {

#if defined(__APPLE__)
class AVFWebcam;
#elif defined(_WIN32)
class MFWebcam;
#endif

/**
 * @brief Live webcam capture operator.
 *
 * Captures frames from a connected camera and outputs as a texture.
 *
 * Example:
 * @code
 * auto& webcam = chain.add<Webcam>("cam");
 * webcam.resolution(1280, 720).frameRate(30);
 * @endcode
 */
class VIVID_VIDEO_API Webcam : public vivid::effects::TextureOperator {
public:
    Webcam();
    ~Webcam();

    // Non-copyable
    Webcam(const Webcam&) = delete;
    Webcam& operator=(const Webcam&) = delete;

    /**
     * @brief Set camera by device index (0 = first camera).
     */
    Webcam& device(int index);

    /**
     * @brief Set camera by device ID.
     */
    Webcam& device(const std::string& deviceId);

    /**
     * @brief Set capture resolution.
     */
    Webcam& resolution(int width, int height);

    /**
     * @brief Set capture frame rate.
     */
    Webcam& frameRate(float fps);

    /**
     * @brief List available cameras to console.
     */
    void listDevices();

    // Accessors
    int captureWidth() const { return m_captureWidth; }
    int captureHeight() const { return m_captureHeight; }
    float captureFrameRate() const { return m_captureFrameRate; }
    bool isCapturing() const;

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Webcam"; }

private:
    void openCamera(Context& ctx);

    int m_deviceIndex = 0;
    std::string m_deviceId;
    bool m_useDeviceId = false;

    int m_requestedWidth = 1280;
    int m_requestedHeight = 720;
    float m_requestedFps = 30.0f;

    int m_captureWidth = 0;
    int m_captureHeight = 0;
    float m_captureFrameRate = 0.0f;

    bool m_needsReopen = true;

#if defined(__APPLE__)
    std::unique_ptr<AVFWebcam> m_webcam;
#elif defined(_WIN32)
    std::unique_ptr<MFWebcam> m_webcam;
#endif
};

} // namespace vivid::video
