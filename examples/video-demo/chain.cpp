// Video Demo Example
// Demonstrates video playback using the vivid-video addon

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/video/video.h>
#include <iostream>

using namespace vivid;
using namespace vivid::video;

// Video player and display
static VideoPlayer video;
static std::unique_ptr<Output> output;

// Custom passthrough that uses the video texture
// We need a simple adapter since VideoTexture isn't a full Operator
class VideoOutput {
public:
    void init(Context& ctx) {
        // Create PSO for rendering video to screen
        const char* psSource = R"(
            Texture2D g_Texture : register(t0);
            SamplerState g_Sampler : register(s0);

            struct PSInput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            float4 main(in PSInput input) : SV_TARGET {
                return g_Texture.Sample(g_Sampler, input.uv);
            }
        )";

        auto* ps = ctx.shaderUtils().loadShaderFromSource(
            psSource, "VideoPS", "main", Diligent::SHADER_TYPE_PIXEL
        );

        if (!ps) {
            std::cerr << "[VideoDemo] Failed to create pixel shader" << std::endl;
            return;
        }

        const auto& scDesc = ctx.swapChain()->GetDesc();
        pso_ = ctx.shaderUtils().createOutputPipeline("VideoPSO", ps, scDesc.ColorBufferFormat);
        ps->Release();

        if (!pso_) {
            std::cerr << "[VideoDemo] Failed to create PSO" << std::endl;
            return;
        }

        pso_->CreateShaderResourceBinding(&srb_, true);
    }

    void process(Context& ctx, Diligent::ITextureView* textureSRV) {
        if (!pso_ || !srb_ || !textureSRV) return;

        auto* immediateCtx = ctx.immediateContext();

        // Set render target to swap chain
        auto* rtv = ctx.currentRTV();
        immediateCtx->SetRenderTargets(1, &rtv, nullptr,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Set viewport
        Diligent::Viewport vp;
        vp.Width = static_cast<float>(ctx.width());
        vp.Height = static_cast<float>(ctx.height());
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        immediateCtx->SetViewports(1, &vp, ctx.width(), ctx.height());

        // Bind video texture
        auto* texVar = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
        if (texVar) {
            texVar->Set(textureSRV);
        }

        // Draw fullscreen
        immediateCtx->SetPipelineState(pso_);
        immediateCtx->CommitShaderResources(srb_,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx.fullscreenQuad().draw();
    }

    void cleanup() {
        if (srb_) { srb_->Release(); srb_ = nullptr; }
        if (pso_) { pso_->Release(); pso_ = nullptr; }
    }

private:
    Diligent::IPipelineState* pso_ = nullptr;
    Diligent::IShaderResourceBinding* srb_ = nullptr;
};

static VideoOutput videoOutput;
static bool initialized = false;

void setup(Context& ctx) {
    std::cout << "[VideoDemo] Initializing video playback..." << std::endl;

    // Initialize the video output renderer
    videoOutput.init(ctx);

    // Try to open a video file
    // Look for common video file locations
    const char* videoPaths[] = {
        "examples/video-demo/test.mp4",
        "examples/video-demo/video.mp4",
        "test.mp4",
        "video.mp4"
    };

    for (const char* path : videoPaths) {
        if (video.open(ctx, path, true)) {  // Loop enabled
            std::cout << "[VideoDemo] Opened: " << path << std::endl;
            std::cout << "[VideoDemo] Size: " << video.width() << "x" << video.height() << std::endl;
            std::cout << "[VideoDemo] Duration: " << video.duration() << "s" << std::endl;
            std::cout << "[VideoDemo] Frame rate: " << video.frameRate() << " fps" << std::endl;
            initialized = true;
            return;
        }
    }

    std::cerr << "[VideoDemo] No video file found!" << std::endl;
    std::cerr << "[VideoDemo] Please place a video file at examples/video-demo/test.mp4" << std::endl;
    initialized = false;
}

void update(Context& ctx) {
    if (!initialized || !video.isOpen()) {
        // Show a colored background when no video is available
        auto* immediateCtx = ctx.immediateContext();
        auto* rtv = ctx.currentRTV();
        float clearColor[] = {0.2f, 0.1f, 0.3f, 1.0f};
        immediateCtx->SetRenderTargets(1, &rtv, nullptr,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        immediateCtx->ClearRenderTarget(rtv, clearColor,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }

    // Update video (decode next frame)
    video.update(ctx);

    // Display the video frame
    if (auto* texView = video.textureView()) {
        videoOutput.process(ctx, texView);
    }

    // Print status occasionally
    static int frameCount = 0;
    if (++frameCount % 120 == 0) {
        std::cout << "[VideoDemo] Time: " << video.currentTime()
                  << "s / " << video.duration() << "s" << std::endl;
    }

    // Keyboard controls
    if (ctx.wasKeyPressed(32)) {  // Space
        if (video.isPlaying()) {
            video.pause();
            std::cout << "[VideoDemo] Paused" << std::endl;
        } else {
            video.play();
            std::cout << "[VideoDemo] Playing" << std::endl;
        }
    }

    if (ctx.wasKeyPressed(82)) {  // R key
        video.seek(0.0f);
        std::cout << "[VideoDemo] Restarted" << std::endl;
    }
}

VIVID_CHAIN(setup, update)
