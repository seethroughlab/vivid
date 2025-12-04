#pragma once

#include <string>
#include <memory>

namespace Diligent {
    struct ITexture;
    struct ITextureView;
    class PBR_Renderer;
}

namespace vivid {

class Context;

/// Image-Based Lighting environment using DiligentFX PBR_Renderer
class IBLEnvironment {
public:
    IBLEnvironment();
    ~IBLEnvironment();

    /// Initialize the IBL system (creates PBR_Renderer for cubemap processing)
    bool init(Context& ctx);

    /// Load an HDR environment map and generate IBL cubemaps
    bool loadHDR(Context& ctx, const std::string& hdrPath);

    /// Load a regular image as environment map
    bool loadImage(Context& ctx, const std::string& imagePath);

    /// Release resources
    void cleanup();

    // IBL texture accessors (from PBR_Renderer)
    Diligent::ITextureView* irradianceSRV() const;
    Diligent::ITextureView* prefilteredSRV() const;
    Diligent::ITextureView* brdfLutSRV() const;

    /// Get the source environment map
    Diligent::ITextureView* envMapSRV() const { return envMapSRV_; }

    bool isLoaded() const { return envMapTex_ != nullptr; }

private:
    std::unique_ptr<Diligent::PBR_Renderer> pbrRenderer_;

    // Source environment map
    Diligent::ITexture* envMapTex_ = nullptr;
    Diligent::ITextureView* envMapSRV_ = nullptr;

    bool initialized_ = false;
};

} // namespace vivid
