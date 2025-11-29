// Webcam Example
// Demonstrates live camera capture using the Context camera API

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

class WebcamDemo : public Operator {
public:
    void init(Context& ctx) override {
        ctx_ = &ctx;  // Store context pointer for cleanup

        // List available cameras
        auto cameras = ctx.enumerateCameras();
        std::cout << "[WebcamDemo] Available cameras:\n";
        for (size_t i = 0; i < cameras.size(); i++) {
            std::cout << "  [" << i << "] " << cameras[i].name;
            if (cameras[i].isDefault) std::cout << " (default)";
            std::cout << "\n";
        }

        // Open default camera at 720p 30fps
        camera_ = ctx.createCamera(1280, 720, 30.0f);

        if (camera_.valid()) {
            CameraInfo info = ctx.getCameraInfo(camera_);
            std::cout << "[WebcamDemo] Opened: " << info.deviceName
                      << " (" << info.width << "x" << info.height
                      << " @ " << info.frameRate << "fps)\n";
        } else {
            std::cerr << "[WebcamDemo] Failed to open camera\n";
        }

        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
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
    }

    void cleanup() override {
        if (camera_.valid() && ctx_) {
            ctx_->destroyCamera(camera_);
        }
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    Context* ctx_ = nullptr;
    Camera camera_;
    Texture output_;
};

VIVID_OPERATOR(WebcamDemo)
