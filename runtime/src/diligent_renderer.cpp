#include "diligent_renderer.h"

#ifdef VIVID_USE_DILIGENT

#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include <GLFW/glfw3.h>
#include <iostream>

#if PLATFORM_MACOS
extern "C" void* getNSViewFromGLFW(GLFWwindow* window);
#endif

namespace vivid {

DiligentRenderer::DiligentRenderer() = default;

DiligentRenderer::~DiligentRenderer() {
    shutdown();
}

bool DiligentRenderer::init(GLFWwindow* window, int width, int height) {
    width_ = width;
    height_ = height;

#if PLATFORM_MACOS
    // On macOS, Diligent expects an NSView pointer (it gets the CAMetalLayer internally)
    void* nsView = getNSViewFromGLFW(window);
    if (!nsView) {
        std::cerr << "DiligentRenderer: Failed to get NSView from GLFW" << std::endl;
        return false;
    }
    if (!backend_.init(nsView, width, height)) {
        std::cerr << "DiligentRenderer: Failed to initialize Diligent backend" << std::endl;
        return false;
    }
#elif PLATFORM_WIN32
    HWND hwnd = glfwGetWin32Window(window);
    if (!backend_.init(hwnd, width, height)) {
        std::cerr << "DiligentRenderer: Failed to initialize Diligent backend" << std::endl;
        return false;
    }
#else
    // Linux - use X11 window
    Window x11Window = glfwGetX11Window(window);
    if (!backend_.init(reinterpret_cast<void*>(x11Window), width, height)) {
        std::cerr << "DiligentRenderer: Failed to initialize Diligent backend" << std::endl;
        return false;
    }
#endif

    if (!createBlitPipeline()) {
        std::cerr << "DiligentRenderer: Failed to create blit pipeline" << std::endl;
        return false;
    }

    std::cout << "DiligentRenderer: Initialized successfully" << std::endl;
    return true;
}

void DiligentRenderer::shutdown() {
    blitPipeline_.Release();
    blitSRB_.Release();
    blitSampler_.Release();
    backend_.shutdown();
}

bool DiligentRenderer::beginFrame() {
    backend_.beginFrame();
    return true;
}

void DiligentRenderer::endFrame() {
    backend_.endFrame();
    backend_.present();
}

void DiligentRenderer::clear(float r, float g, float b, float a) {
    backend_.clear(glm::vec4(r, g, b, a));
}

Texture DiligentRenderer::createTexture(int width, int height) {
    using namespace Diligent;

    Texture tex;
    tex.width = width;
    tex.height = height;

    TextureDesc TexDesc;
    TexDesc.Name = "Vivid Texture";
    TexDesc.Type = RESOURCE_DIM_TEX_2D;
    TexDesc.Width = width;
    TexDesc.Height = height;
    TexDesc.Format = TEX_FORMAT_RGBA8_UNORM;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> pTexture;
    backend_.device()->CreateTexture(TexDesc, nullptr, &pTexture);

    if (!pTexture) {
        std::cerr << "DiligentRenderer: Failed to create texture" << std::endl;
        return tex;
    }

    // Create texture data
    auto* data = new DiligentTextureData();
    data->texture = pTexture;
    data->view = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    data->rtv = pTexture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);

    tex.handle = data;
    return tex;
}

void DiligentRenderer::destroyTexture(Texture& texture) {
    if (texture.handle) {
        auto* data = static_cast<DiligentTextureData*>(texture.handle);
        delete data;
        texture.handle = nullptr;
    }
    texture.width = 0;
    texture.height = 0;
}

void DiligentRenderer::uploadTexturePixels(Texture& texture, const uint8_t* pixels, int width, int height) {
    if (!texture.handle) return;

    auto* data = static_cast<DiligentTextureData*>(texture.handle);
    if (!data->texture) return;

    Diligent::Box UpdateBox;
    UpdateBox.MinX = 0;
    UpdateBox.MinY = 0;
    UpdateBox.MaxX = width;
    UpdateBox.MaxY = height;

    Diligent::TextureSubResData SubResData;
    SubResData.pData = pixels;
    SubResData.Stride = width * 4;

    backend_.context()->UpdateTexture(
        data->texture,
        0, 0,
        UpdateBox,
        SubResData,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION
    );
}

void DiligentRenderer::blitToScreen(const Texture& texture) {
    if (!texture.handle || !blitPipeline_) return;

    auto* data = static_cast<DiligentTextureData*>(texture.handle);
    if (!data->view) return;

    using namespace Diligent;

    auto* pRTV = backend_.swapChain()->GetCurrentBackBufferRTV();

    // Set render target
    backend_.context()->SetRenderTargets(1, &pRTV, nullptr,
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set pipeline and resources
    backend_.context()->SetPipelineState(blitPipeline_);

    // Update SRB with texture
    if (blitSRB_) {
        auto* pVar = blitSRB_->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
        if (pVar) {
            pVar->Set(data->view);
        }
        backend_.context()->CommitShaderResources(blitSRB_,
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Draw fullscreen triangle
    DrawAttribs drawAttrs;
    drawAttrs.NumVertices = 3;
    backend_.context()->Draw(drawAttrs);
}

void DiligentRenderer::fillTexture(Texture& texture, float r, float g, float b, float a) {
    if (!texture.handle) return;

    auto* data = static_cast<DiligentTextureData*>(texture.handle);
    if (!data->rtv) return;

    float clearColor[4] = {r, g, b, a};
    backend_.context()->ClearRenderTarget(data->rtv, clearColor,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

std::vector<uint8_t> DiligentRenderer::readTexturePixels(const Texture& texture) {
    // TODO: Implement texture readback
    std::vector<uint8_t> pixels(texture.width * texture.height * 4, 0);
    return pixels;
}

void DiligentRenderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    backend_.resize(width, height);
}

void DiligentRenderer::setVSync(bool enabled) {
    vsync_ = enabled;
    // Diligent handles VSync through swap chain present mode
}

bool DiligentRenderer::createBlitPipeline() {
    using namespace Diligent;

    // Simple fullscreen blit shader
    const char* VSSource = R"(
        struct VSOutput {
            float4 Pos : SV_POSITION;
            float2 UV  : TEXCOORD;
        };

        void main(uint VertId : SV_VertexID, out VSOutput Out) {
            // Fullscreen triangle
            Out.UV = float2((VertId << 1) & 2, VertId & 2);
            Out.Pos = float4(Out.UV * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
        }
    )";

    const char* PSSource = R"(
        Texture2D    g_Texture;
        SamplerState g_Sampler;

        struct VSOutput {
            float4 Pos : SV_POSITION;
            float2 UV  : TEXCOORD;
        };

        float4 main(VSOutput In) : SV_Target {
            return g_Texture.Sample(g_Sampler, In.UV);
        }
    )";

    // Create shaders
    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler = SHADER_COMPILER_DEFAULT;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name = "Blit VS";
        ShaderCI.Source = VSSource;
        ShaderCI.EntryPoint = "main";
        backend_.device()->CreateShader(ShaderCI, &pVS);
        if (!pVS) {
            std::cerr << "DiligentRenderer: Failed to create blit vertex shader" << std::endl;
            return false;
        }
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Blit PS";
        ShaderCI.Source = PSSource;
        ShaderCI.EntryPoint = "main";
        backend_.device()->CreateShader(ShaderCI, &pPS);
        if (!pPS) {
            std::cerr << "DiligentRenderer: Failed to create blit pixel shader" << std::endl;
            return false;
        }
    }

    // Create sampler
    SamplerDesc SamDesc;
    SamDesc.MinFilter = FILTER_TYPE_LINEAR;
    SamDesc.MagFilter = FILTER_TYPE_LINEAR;
    SamDesc.MipFilter = FILTER_TYPE_LINEAR;
    SamDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
    SamDesc.AddressV = TEXTURE_ADDRESS_CLAMP;
    SamDesc.AddressW = TEXTURE_ADDRESS_CLAMP;
    backend_.device()->CreateSampler(SamDesc, &blitSampler_);

    // Create pipeline state
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Blit PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = backend_.swapChain()->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    // Define shader resources
    ShaderResourceVariableDesc Vars[] = {
        {SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    ImmutableSamplerDesc ImtblSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Sampler", SamDesc}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

    backend_.device()->CreateGraphicsPipelineState(PSOCreateInfo, &blitPipeline_);
    if (!blitPipeline_) {
        std::cerr << "DiligentRenderer: Failed to create blit pipeline" << std::endl;
        return false;
    }

    blitPipeline_->CreateShaderResourceBinding(&blitSRB_, true);
    return true;
}

} // namespace vivid

#endif // VIVID_USE_DILIGENT
