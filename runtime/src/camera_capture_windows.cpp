// Windows camera capture stub
// TODO: Implement using Media Foundation

#include "camera_capture.h"
#include <iostream>

namespace vivid {

class CameraCaptureWindows : public CameraCapture {
public:
    CameraCaptureWindows() = default;
    ~CameraCaptureWindows() override = default;

    std::vector<CameraDeviceInfo> enumerateDevices() override {
        std::cerr << "[CameraCaptureWindows] Not yet implemented\n";
        return {};
    }

    bool open(const CameraConfig& config) override {
        std::cerr << "[CameraCaptureWindows] Not yet implemented\n";
        return false;
    }

    bool open(const std::string& deviceId, const CameraConfig& config) override {
        std::cerr << "[CameraCaptureWindows] Not yet implemented\n";
        return false;
    }

    bool openByIndex(int index, const CameraConfig& config) override {
        std::cerr << "[CameraCaptureWindows] Not yet implemented\n";
        return false;
    }

    void close() override {}

    bool isOpen() const override { return false; }

    bool startCapture() override {
        std::cerr << "[CameraCaptureWindows] Not yet implemented\n";
        return false;
    }

    void stopCapture() override {}

    bool isCapturing() const override { return false; }

    const CameraInfo& info() const override {
        static CameraInfo emptyInfo{};
        return emptyInfo;
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        return false;
    }

    bool hasNewFrame() const override { return false; }
};

std::unique_ptr<CameraCapture> CameraCapture::create() {
    return std::make_unique<CameraCaptureWindows>();
}

} // namespace vivid
