#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>
#include <iostream>

namespace vivid {

/**
 * @brief Live camera/webcam capture.
 *
 * Captures video frames from a connected camera.
 *
 * Example:
 * @code
 * Webcam cam_;
 * cam_.resolution(1280, 720).frameRate(30.0f);
 * @endcode
 */
class Webcam : public Operator {
public:
    Webcam() = default;

    /// Set device by index (0 = first camera)
    Webcam& device(int index) {
        deviceIndex_ = index;
        useDeviceId_ = false;
        needsReopen_ = true;
        return *this;
    }

    /// Set device by ID
    Webcam& device(const std::string& id) {
        deviceId_ = id;
        useDeviceId_ = true;
        needsReopen_ = true;
        return *this;
    }

    /// Set capture resolution
    Webcam& resolution(int width, int height) {
        width_ = width;
        height_ = height;
        needsReopen_ = true;
        return *this;
    }

    /// Set capture frame rate
    Webcam& frameRate(float fps) {
        frameRate_ = fps;
        needsReopen_ = true;
        return *this;
    }

    void init(Context& ctx) override {
        ctx_ = &ctx;
    }

    void process(Context& ctx) override {
        ctx_ = &ctx;

        if (!camera_.valid() || needsReopen_) {
            openCamera(ctx);
            needsReopen_ = false;
        }

        if (!camera_.valid()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        ctx.cameraGetFrame(camera_, output_);

        if (output_.valid()) {
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", Texture{});
        }

        CameraInfo info = ctx.getCameraInfo(camera_);
        ctx.setOutput("width", static_cast<float>(info.width));
        ctx.setOutput("height", static_cast<float>(info.height));
        ctx.setOutput("fps", info.frameRate);
    }

    void cleanup() override {
        if (camera_.valid() && ctx_) {
            ctx_->destroyCamera(camera_);
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("device", deviceIndex_, 0, 10),
            intParam("width", width_, 320, 3840),
            intParam("height", height_, 240, 2160),
            floatParam("frameRate", frameRate_, 1.0f, 120.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    void openCamera(Context& ctx) {
        if (camera_.valid()) {
            ctx.destroyCamera(camera_);
        }

        if (useDeviceId_ && !deviceId_.empty()) {
            camera_ = ctx.createCamera(deviceId_, width_, height_, frameRate_);
        } else {
            auto devices = ctx.enumerateCameras();
            if (devices.empty()) {
                std::cerr << "[Webcam] No cameras found\n";
                return;
            }

            int index = deviceIndex_;
            if (index < 0 || index >= static_cast<int>(devices.size())) {
                index = 0;
            }

            camera_ = ctx.createCamera(devices[index].deviceId, width_, height_, frameRate_);
        }

        if (camera_.valid()) {
            CameraInfo info = ctx.getCameraInfo(camera_);
            std::cout << "[Webcam] Opened: " << info.deviceName
                      << " (" << info.width << "x" << info.height
                      << " @ " << info.frameRate << "fps)\n";
        }
    }

    Context* ctx_ = nullptr;
    int deviceIndex_ = 0;
    std::string deviceId_;
    bool useDeviceId_ = false;
    int width_ = 1280;
    int height_ = 720;
    float frameRate_ = 30.0f;
    Camera camera_;
    Texture output_;
    bool needsReopen_ = false;
};

} // namespace vivid
