// Webcam Operator
// Captures live video from a camera/webcam

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

class Webcam : public Operator {
public:
    Webcam() = default;

    // Fluent API
    Webcam& device(int index) {
        deviceIndex_ = index;
        useDeviceId_ = false;
        needsReopen_ = true;
        return *this;
    }

    Webcam& device(const std::string& id) {
        deviceId_ = id;
        useDeviceId_ = true;
        needsReopen_ = true;
        return *this;
    }

    Webcam& resolution(int width, int height) {
        width_ = width;
        height_ = height;
        needsReopen_ = true;
        return *this;
    }

    Webcam& frameRate(float fps) {
        frameRate_ = fps;
        needsReopen_ = true;
        return *this;
    }

    void init(Context& ctx) override {
        // Camera will be created on first process
    }

    void process(Context& ctx) override {
        // Open camera if needed
        if (!camera_.valid() || needsReopen_) {
            openCamera(ctx);
            needsReopen_ = false;
        }

        if (!camera_.valid()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        // Get latest frame (non-blocking)
        ctx.cameraGetFrame(camera_, output_);

        // Output current frame
        if (output_.valid()) {
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", Texture{});
        }

        // Output camera info
        CameraInfo info = ctx.getCameraInfo(camera_);
        ctx.setOutput("width", static_cast<float>(info.width));
        ctx.setOutput("height", static_cast<float>(info.height));
        ctx.setOutput("fps", info.frameRate);
        ctx.setOutput("capturing", info.isCapturing ? 1.0f : 0.0f);
    }

    void cleanup(Context& ctx) override {
        if (camera_.valid()) {
            ctx.destroyCamera(camera_);
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

    OutputKind outputKind() override {
        return OutputKind::Texture;
    }

private:
    void openCamera(Context& ctx) {
        // Close existing camera
        if (camera_.valid()) {
            ctx.destroyCamera(camera_);
        }

        // Open new camera
        if (useDeviceId_ && !deviceId_.empty()) {
            camera_ = ctx.createCamera(deviceId_, width_, height_, frameRate_);
        } else {
            // Use default camera or by index
            auto devices = ctx.enumerateCameras();
            if (devices.empty()) {
                std::cerr << "[Webcam] No cameras found\n";
                return;
            }

            // List available cameras
            std::cout << "[Webcam] Available cameras:\n";
            for (size_t i = 0; i < devices.size(); i++) {
                std::cout << "  [" << i << "] " << devices[i].name;
                if (devices[i].isDefault) std::cout << " (default)";
                std::cout << "\n";
            }

            // Select camera by index
            int index = deviceIndex_;
            if (index < 0 || index >= static_cast<int>(devices.size())) {
                index = 0;  // Fall back to first camera
            }

            camera_ = ctx.createCamera(devices[index].deviceId, width_, height_, frameRate_);
        }

        if (camera_.valid()) {
            CameraInfo info = ctx.getCameraInfo(camera_);
            std::cout << "[Webcam] Opened: " << info.deviceName
                      << " (" << info.width << "x" << info.height
                      << " @ " << info.frameRate << "fps)\n";
        } else {
            std::cerr << "[Webcam] Failed to open camera\n";
        }
    }

    // Parameters
    int deviceIndex_ = 0;
    std::string deviceId_;
    bool useDeviceId_ = false;
    int width_ = 1280;
    int height_ = 720;
    float frameRate_ = 30.0f;

    // State
    Camera camera_;
    Texture output_;
    bool needsReopen_ = false;
};

VIVID_OPERATOR(Webcam)
