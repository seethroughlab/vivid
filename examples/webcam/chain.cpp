// Webcam Glitch Example
// Demonstrates live camera capture with glitch effects chain:
// Webcam -> ChromaticAberration -> Pixelate -> Scanlines

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

class WebcamGlitch : public Operator {
public:
    void init(Context& ctx) override {
        ctx_ = &ctx;

        // Disable vsync for maximum framerate (optional)
        // ctx.setVSync(false);

        // List available cameras
        auto cameras = ctx.enumerateCameras();
        std::cout << "[WebcamGlitch] Available cameras:\n";
        for (size_t i = 0; i < cameras.size(); i++) {
            std::cout << "  [" << i << "] " << cameras[i].name;
            if (cameras[i].isDefault) std::cout << " (default)";
            std::cout << "\n";
        }

        // Open default camera at 720p 30fps
        camera_ = ctx.createCamera(1280, 720, 30.0f);

        if (camera_.valid()) {
            CameraInfo info = ctx.getCameraInfo(camera_);
            std::cout << "[WebcamGlitch] Opened: " << info.deviceName
                      << " (" << info.width << "x" << info.height
                      << " @ " << info.frameRate << "fps)\n";
        } else {
            std::cerr << "[WebcamGlitch] Failed to open camera\n";
        }

        // Create textures for effect chain
        frame_ = ctx.createTexture();
        temp1_ = ctx.createTexture();
        temp2_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        if (!camera_.valid()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        // Get latest camera frame
        ctx.cameraGetFrame(camera_, frame_);

        if (!frame_.valid()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        // Effect 1: Chromatic Aberration
        // RGB channel separation with slowly rotating angle
        Context::ShaderParams chromaParams;
        chromaParams.param0 = chromaAmount_;      // amount
        chromaParams.param1 = ctx.time() * 0.3f;  // rotating angle for dynamic effect
        chromaParams.mode = 1;                    // radial mode - stronger at edges
        ctx.runShader("shaders/chromatic_aberration.wgsl", &frame_, temp1_, chromaParams);

        // Effect 2: Pixelate
        // Subtle blockiness for retro feel
        Context::ShaderParams pixelParams;
        pixelParams.param0 = pixelSize_;  // block size
        pixelParams.mode = 0;             // square blocks
        ctx.runShader("shaders/pixelate.wgsl", &temp1_, temp2_, pixelParams);

        // Effect 3: Scanlines
        // CRT monitor effect with subtle scrolling
        Context::ShaderParams scanParams;
        scanParams.param0 = scanDensity_;    // line density
        scanParams.param1 = scanIntensity_;  // darkness
        scanParams.param2 = 20.0f;           // slow scroll speed
        scanParams.mode = 2;                 // RGB sub-pixel mode for authentic CRT look
        ctx.runShader("shaders/scanlines.wgsl", &temp2_, output_, scanParams);

        ctx.setOutput("out", output_);

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

    std::vector<ParamDecl> params() override {
        return {
            floatParam("chromaAmount", chromaAmount_, 0.0f, 0.05f),
            floatParam("pixelSize", pixelSize_, 1.0f, 32.0f),
            floatParam("scanDensity", scanDensity_, 100.0f, 800.0f),
            floatParam("scanIntensity", scanIntensity_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    Context* ctx_ = nullptr;
    Camera camera_;
    Texture frame_;
    Texture temp1_;
    Texture temp2_;
    Texture output_;

    // Effect parameters (adjust these for different looks!)
    float chromaAmount_ = 0.012f;    // RGB separation strength
    float pixelSize_ = 3.0f;         // Block size (1 = no pixelation)
    float scanDensity_ = 400.0f;     // Scanlines per screen height
    float scanIntensity_ = 0.25f;    // Scanline darkness
};

VIVID_OPERATOR(WebcamGlitch)
