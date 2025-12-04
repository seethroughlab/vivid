// GLTF Viewer Operator Implementation

#include "vivid/operators/gltf_viewer.h"
#include "vivid/context.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// DiligentFX for GLTF PBR rendering
#include "GLTF_PBR_Renderer.hpp"
#include "EnvMapRenderer.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"

// Include PBR shader structures
namespace Diligent {
namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL
} // namespace Diligent

namespace vivid {

using namespace Diligent;

struct GLTFViewer::Impl {
    std::unique_ptr<GLTF_PBR_Renderer> renderer;
    RefCntAutoPtr<IBuffer> frameAttribsCB;
    std::vector<GLTF_PBR_Renderer::ModelResourceBindings> modelBindings;
    GLTF::Light defaultLight;
    GLTF_PBR_Renderer::RenderInfo renderParams;
    TEXTURE_FORMAT colorFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT depthFormat = TEX_FORMAT_UNKNOWN;
    bool initialized = false;
    RefCntAutoPtr<ITexture> envMapTex;
    RefCntAutoPtr<ITextureView> envMapSRV;

    // Skybox rendering
    std::unique_ptr<EnvMapRenderer> envMapRenderer;
    RefCntAutoPtr<IBuffer> cameraAttribsCB;
};

GLTFViewer::GLTFViewer() : impl_(std::make_unique<Impl>()) {
    // Setup default light
    impl_->defaultLight.Type = GLTF::Light::TYPE::DIRECTIONAL;
    impl_->defaultLight.Intensity = lightIntensity_;
}

GLTFViewer::~GLTFViewer() = default;

int GLTFViewer::loadModel(Context& ctx, const std::string& path) {
    GLTFModel model;
    if (!model.load(ctx, path)) {
        return -1;
    }

    // Store model
    int index = static_cast<int>(models_.size());
    models_.push_back(std::move(model));

    // Extract name from path
    size_t lastSlash = path.rfind('/');
    size_t secondLast = path.rfind('/', lastSlash - 1);
    std::string name = (secondLast != std::string::npos)
        ? path.substr(secondLast + 1, lastSlash - secondLast - 1)
        : path.substr(lastSlash + 1);
    modelNames_.push_back(name);

    // Create resource bindings if renderer is ready
    if (impl_->renderer && impl_->frameAttribsCB) {
        auto bindings = impl_->renderer->CreateResourceBindings(
            *models_[index].diligentModel(), impl_->frameAttribsCB);
        impl_->modelBindings.push_back(std::move(bindings));
    }

    return index;
}

void GLTFViewer::setCurrentModel(int index) {
    if (index >= 0 && index < static_cast<int>(models_.size())) {
        currentModelIndex_ = index;
    }
}

void GLTFViewer::nextModel() {
    if (!models_.empty()) {
        currentModelIndex_ = (currentModelIndex_ + 1) % static_cast<int>(models_.size());
    }
}

std::string GLTFViewer::modelName(int index) const {
    if (index >= 0 && index < static_cast<int>(modelNames_.size())) {
        return modelNames_[index];
    }
    return "";
}

void GLTFViewer::lightDirection(float x, float y, float z) {
    lightDir_ = glm::normalize(glm::vec3(x, y, z));
}

void GLTFViewer::lightIntensity(float intensity) {
    lightIntensity_ = intensity;
    impl_->defaultLight.Intensity = intensity;
}

void GLTFViewer::backgroundColor(float r, float g, float b) {
    bgColor_ = glm::vec3(r, g, b);
}

bool GLTFViewer::loadEnvironment(Context& ctx, const std::string& hdrPath) {
    if (!impl_->initialized || !impl_->renderer) {
        std::cerr << "GLTFViewer must be initialized before loading environment" << std::endl;
        return false;
    }

    // Load HDR texture
    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = false;  // HDR is linear
    loadInfo.GenerateMips = true;
    loadInfo.Name = hdrPath.c_str();

    RefCntAutoPtr<ITexture> texture;
    CreateTextureFromFile(hdrPath.c_str(), loadInfo, ctx.device(), &texture);

    if (!texture) {
        std::cerr << "Failed to load HDR environment: " << hdrPath << std::endl;
        return false;
    }

    impl_->envMapTex = texture;
    impl_->envMapSRV = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    std::cout << "Loaded HDR environment: " << hdrPath << std::endl;

    // Precompute IBL cubemaps using the renderer
    impl_->renderer->PrecomputeCubemaps(ctx.immediateContext(), impl_->envMapSRV);

    std::cout << "Generated IBL cubemaps (irradiance + prefiltered)" << std::endl;

    // Create EnvMapRenderer for skybox rendering
    EnvMapRenderer::CreateInfo EnvMapRndrCI;
    EnvMapRndrCI.pDevice = ctx.device();
    EnvMapRndrCI.pCameraAttribsCB = impl_->cameraAttribsCB;
    EnvMapRndrCI.NumRenderTargets = 1;
    EnvMapRndrCI.RTVFormats[0] = impl_->colorFormat;
    EnvMapRndrCI.DSVFormat = impl_->depthFormat;

    try {
        impl_->envMapRenderer = std::make_unique<EnvMapRenderer>(EnvMapRndrCI);
        std::cout << "Created EnvMapRenderer for skybox" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create EnvMapRenderer: " << e.what() << std::endl;
    }

    hasEnvironment_ = true;

    // Enable IBL in render params
    impl_->renderParams.Flags |= GLTF_PBR_Renderer::PSO_FLAG_USE_IBL;

    return true;
}

bool GLTFViewer::hasEnvironment() const {
    return hasEnvironment_;
}

bool GLTFViewer::isInitialized() const {
    return impl_->initialized;
}

void GLTFViewer::init(Context& ctx) {
    // Get swap chain formats
    const auto& SCDesc = ctx.swapChain()->GetDesc();
    impl_->colorFormat = SCDesc.ColorBufferFormat;
    impl_->depthFormat = SCDesc.DepthBufferFormat;

    // Create GLTF PBR Renderer
    GLTF_PBR_Renderer::CreateInfo RendererCI;
    RendererCI.NumRenderTargets = 1;
    RendererCI.RTVFormats[0] = impl_->colorFormat;
    RendererCI.DSVFormat = impl_->depthFormat;
    // FrontCounterClockwise = false because we flip Y in the projection matrix,
    // which reverses the triangle winding order
    RendererCI.FrontCounterClockwise = false;

    try {
        impl_->renderer = std::make_unique<GLTF_PBR_Renderer>(
            ctx.device(), nullptr, ctx.immediateContext(), RendererCI);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create GLTF_PBR_Renderer: " << e.what() << std::endl;
        return;
    }

    // Create frame attributes constant buffer
    CreateUniformBuffer(ctx.device(), impl_->renderer->GetPRBFrameAttribsSize(),
                       "PBR frame attribs buffer", &impl_->frameAttribsCB);

    // Create camera attribs constant buffer for skybox renderer
    CreateUniformBuffer(ctx.device(), sizeof(HLSL::CameraAttribs),
                       "Camera attribs buffer", &impl_->cameraAttribsCB);

    // Transition buffers to constant buffer state
    StateTransitionDesc Barriers[] = {
        {impl_->frameAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
        {impl_->cameraAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    ctx.immediateContext()->TransitionResourceStates(2, Barriers);

    // Create resource bindings for already loaded models
    for (size_t i = 0; i < models_.size(); i++) {
        auto bindings = impl_->renderer->CreateResourceBindings(
            *models_[i].diligentModel(), impl_->frameAttribsCB);
        impl_->modelBindings.push_back(std::move(bindings));
    }

    // Setup render parameters
    impl_->renderParams.AlphaModes = GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAG_ALL;
    impl_->renderParams.Flags = GLTF_PBR_Renderer::PSO_FLAG_DEFAULT |
                                GLTF_PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING;

    // Convert output to sRGB if needed
    if (impl_->colorFormat == TEX_FORMAT_RGBA8_UNORM ||
        impl_->colorFormat == TEX_FORMAT_BGRA8_UNORM) {
        impl_->renderParams.Flags |= GLTF_PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;
    }

    // Setup default camera
    camera_.setOrbit(glm::vec3(0, 0, 0), 3.0f, 45.0f, 20.0f);

    impl_->initialized = true;
    std::cout << "GLTFViewer initialized" << std::endl;
}

void GLTFViewer::process(Context& ctx) {
    if (!impl_->initialized || models_.empty()) {
        return;
    }

    // Get current model
    auto& model = models_[currentModelIndex_];
    auto* diligentModel = model.diligentModel();
    auto* transforms = model.transforms();

    if (!diligentModel || !transforms) {
        return;
    }

    // Update animation if available
    if (model.animationCount() > 0) {
        model.updateAnimation(model.defaultSceneIndex(), 0, ctx.time());
    }

    // Compute model transform to center and scale
    glm::vec3 modelCenter = model.center();
    glm::vec3 modelSize = model.size();
    float maxDim = std::max({modelSize.x, modelSize.y, modelSize.z});
    float scale = (maxDim > 0.01f) ? (1.0f / maxDim) : 1.0f;

    // Build model transform matrix
    glm::mat4 modelTransform = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    modelTransform = modelTransform * glm::translate(glm::mat4(1.0f), -modelCenter);

    // Build view and projection matrices
    float aspect = static_cast<float>(ctx.width()) / static_cast<float>(ctx.height());
    glm::mat4 viewMatrix = camera_.viewMatrix();
    glm::mat4 projMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

    // Flip Y for Vulkan
    projMatrix[1][1] *= -1.0f;

    // Clear render target
    ITextureView* pRTV = ctx.currentRTV();
    ITextureView* pDSV = ctx.currentDSV();
    const float ClearColor[] = {bgColor_.r, bgColor_.g, bgColor_.b, 1.0f};
    ctx.immediateContext()->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx.immediateContext()->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set render target
    ctx.immediateContext()->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Update frame attributes
    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{ctx.immediateContext(), impl_->frameAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

        // Camera attributes
        auto& cam = FrameAttribs->Camera;

        // Convert glm matrices to Diligent float4x4 (transpose for row-major)
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                cam.mView.m[i][j] = viewMatrix[j][i];
                cam.mProj.m[i][j] = projMatrix[j][i];
            }
        }

        // View-projection
        glm::mat4 viewProj = projMatrix * viewMatrix;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                cam.mViewProj.m[i][j] = viewProj[j][i];
            }
        }

        // Inverse matrices
        glm::mat4 invView = glm::inverse(viewMatrix);
        glm::mat4 invProj = glm::inverse(projMatrix);
        glm::mat4 invViewProj = glm::inverse(viewProj);
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                cam.mViewInv.m[i][j] = invView[j][i];
                cam.mProjInv.m[i][j] = invProj[j][i];
                cam.mViewProjInv.m[i][j] = invViewProj[j][i];
            }
        }

        // Camera position
        glm::vec3 camPos = camera_.position();
        cam.f4Position = float4{camPos.x, camPos.y, camPos.z, 1.0f};

        cam.f4ViewportSize = float4{static_cast<float>(ctx.width()),
                                   static_cast<float>(ctx.height()),
                                   1.0f / ctx.width(), 1.0f / ctx.height()};

        // Previous camera (same as current for now)
        FrameAttribs->PrevCamera = cam;

        // Light setup
        float3 lightDir = normalize(float3{lightDir_.x, lightDir_.y, lightDir_.z});
        HLSL::PBRLightAttribs* Lights = reinterpret_cast<HLSL::PBRLightAttribs*>(FrameAttribs + 1);
        GLTF_PBR_Renderer::WritePBRLightShaderAttribs({&impl_->defaultLight, nullptr, &lightDir, scale}, Lights);

        // Renderer parameters
        auto& Renderer = FrameAttribs->Renderer;
        impl_->renderer->SetInternalShaderParameters(Renderer);
        Renderer.OcclusionStrength = 1.0f;
        Renderer.EmissionScale = 1.0f;
        Renderer.AverageLogLum = 0.3f;
        Renderer.MiddleGray = 0.18f;
        Renderer.WhitePoint = 3.0f;
        Renderer.IBLScale = float4{1.0f, 1.0f, 1.0f, 1.0f};
        Renderer.LightCount = 1;
    }

    // Set scene index and model transform
    impl_->renderParams.SceneIndex = model.defaultSceneIndex() >= 0 ? model.defaultSceneIndex() : 0;

    // Convert model transform to Diligent float4x4
    float4x4 diligentModelTransform;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            diligentModelTransform.m[i][j] = modelTransform[j][i];
        }
    }
    impl_->renderParams.ModelTransform = diligentModelTransform;

    // Begin rendering
    impl_->renderer->Begin(ctx.immediateContext());

    // Render the model
    impl_->renderer->Render(ctx.immediateContext(), *diligentModel, *transforms, nullptr,
                           impl_->renderParams, &impl_->modelBindings[currentModelIndex_]);

    // Render skybox AFTER model (renders only where depth == 1.0, i.e. no geometry)
    if (hasEnvironment_ && impl_->envMapRenderer) {
        // Update camera attribs constant buffer for skybox
        {
            MapHelper<HLSL::CameraAttribs> CamAttribs{ctx.immediateContext(), impl_->cameraAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

            // Convert glm matrices to Diligent float4x4 (transpose for row-major)
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    CamAttribs->mView.m[i][j] = viewMatrix[j][i];
                    CamAttribs->mProj.m[i][j] = projMatrix[j][i];
                }
            }

            // View-projection
            glm::mat4 viewProj = projMatrix * viewMatrix;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    CamAttribs->mViewProj.m[i][j] = viewProj[j][i];
                }
            }

            // Inverse matrices
            glm::mat4 invView = glm::inverse(viewMatrix);
            glm::mat4 invProj = glm::inverse(projMatrix);
            glm::mat4 invViewProj = glm::inverse(viewProj);
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    CamAttribs->mViewInv.m[i][j] = invView[j][i];
                    CamAttribs->mProjInv.m[i][j] = invProj[j][i];
                    CamAttribs->mViewProjInv.m[i][j] = invViewProj[j][i];
                }
            }

            // Camera position
            glm::vec3 camPos = camera_.position();
            CamAttribs->f4Position = float4{camPos.x, camPos.y, camPos.z, 1.0f};
            CamAttribs->f4ViewportSize = float4{static_cast<float>(ctx.width()),
                                               static_cast<float>(ctx.height()),
                                               1.0f / ctx.width(), 1.0f / ctx.height()};

            // Depth values - critical for skybox rendering at far plane
            // Using standard depth (near=0, far=1), not reverse depth
            CamAttribs->fNearPlaneZ = 0.1f;
            CamAttribs->fFarPlaneZ = 100.0f;
            CamAttribs->fNearPlaneDepth = 0.0f;
            CamAttribs->fFarPlaneDepth = 1.0f;
        }

        // Setup env map render attributes
        EnvMapRenderer::RenderAttribs EnvMapAttribs;
        EnvMapAttribs.pEnvMap = impl_->renderer->GetPrefilteredEnvMapSRV();
        EnvMapAttribs.AverageLogLum = 0.3f;
        EnvMapAttribs.MipLevel = 1.0f;  // Slight blur for skybox

        // Convert to sRGB if output format is not sRGB
        if (impl_->colorFormat == TEX_FORMAT_RGBA8_UNORM ||
            impl_->colorFormat == TEX_FORMAT_BGRA8_UNORM) {
            EnvMapAttribs.Options |= EnvMapRenderer::OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB;
        }

        // Setup tone mapping
        HLSL::ToneMappingAttribs TMAttribs;
        TMAttribs.iToneMappingMode = TONE_MAPPING_MODE_UNCHARTED2;
        TMAttribs.bAutoExposure = false;
        TMAttribs.fMiddleGray = 0.18f;
        TMAttribs.fWhitePoint = 3.0f;
        TMAttribs.fLuminanceSaturation = 1.0f;

        impl_->envMapRenderer->Prepare(ctx.immediateContext(), EnvMapAttribs, TMAttribs);
        impl_->envMapRenderer->Render(ctx.immediateContext());
    }
}

void GLTFViewer::cleanup() {
    impl_->modelBindings.clear();
    impl_->envMapRenderer.reset();
    impl_->renderer.reset();
    impl_->frameAttribsCB.Release();
    impl_->cameraAttribsCB.Release();
    impl_->envMapTex.Release();
    impl_->envMapSRV.Release();
    impl_->initialized = false;
    hasEnvironment_ = false;
    models_.clear();
    modelNames_.clear();
}

} // namespace vivid
