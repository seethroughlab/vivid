// Chain Visualizer Implementation

#include "vivid/chain_visualizer.h"
#include "vivid/operator.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuiImplDiligent.hpp>

#include <SwapChain.h>
#include <DeviceContext.h>
#include <RenderDevice.h>
#include <TextureView.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <unordered_map>

namespace vivid {

namespace {
    // Global ImGui state for the visualizer
    std::unique_ptr<Diligent::ImGuiImplDiligent> g_imguiRenderer;
    bool g_imguiInitialized = false;

    // Texture ID cache for operator output thumbnails
    std::unordered_map<Diligent::ITextureView*, ImTextureID> g_textureCache;
}

ChainVisualizer::~ChainVisualizer() {
    shutdown();
}

void ChainVisualizer::init(Context& ctx) {
    if (initialized_) {
        return;
    }

    // Check if ImGui is already initialized (by an addon)
    if (ImGui::GetCurrentContext() != nullptr) {
        // ImGui already initialized by addon, we'll use their context
        g_imguiInitialized = false;  // Not our responsibility to clean up
        initialized_ = true;
        std::cout << "[ChainVisualizer] Using existing ImGui context" << std::endl;
        return;
    }

    // Initialize ImGui ourselves
    auto* swapChain = ctx.swapChain();
    if (!swapChain) {
        std::cerr << "[ChainVisualizer] No swap chain available" << std::endl;
        return;
    }

    Diligent::ImGuiDiligentCreateInfo ci;
    ci.pDevice = ctx.device();
    const auto& scDesc = swapChain->GetDesc();
    ci.BackBufferFmt = scDesc.ColorBufferFormat;
    ci.DepthBufferFmt = scDesc.DepthBufferFormat;

    g_imguiRenderer = std::make_unique<Diligent::ImGuiImplDiligent>(ci);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    GLFWwindow* window = ctx.window();
    if (!window) {
        std::cerr << "[ChainVisualizer] No GLFW window available" << std::endl;
        g_imguiRenderer.reset();
        return;
    }
    ImGui_ImplGlfw_InitForOther(window, true);

    g_imguiInitialized = true;
    initialized_ = true;
    std::cout << "[ChainVisualizer] Initialized with new ImGui context" << std::endl;
}

void ChainVisualizer::shutdown() {
    if (!initialized_) {
        return;
    }

    g_textureCache.clear();

    if (g_imguiInitialized) {
        ImGui_ImplGlfw_Shutdown();
        g_imguiRenderer.reset();
        g_imguiInitialized = false;
    }

    initialized_ = false;
}

void ChainVisualizer::beginFrame(Context& ctx) {
    if (!initialized_ || !visible_) {
        return;
    }

    // Only manage frame if we own the ImGui context
    if (g_imguiInitialized && g_imguiRenderer) {
        ImGui_ImplGlfw_NewFrame();

        auto* swapChain = ctx.swapChain();
        if (swapChain) {
            const auto& scDesc = swapChain->GetDesc();
            g_imguiRenderer->NewFrame(
                scDesc.Width,
                scDesc.Height,
                scDesc.PreTransform
            );
        }
    }
}

void ChainVisualizer::render(Context& ctx) {
    if (!initialized_ || !visible_) {
        return;
    }

    const auto& operators = ctx.registeredOperators();
    if (operators.empty()) {
        // Still render a minimal window to show status
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(windowWidth_, 100), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Chain Visualizer", &visible_)) {
            ImGui::TextWrapped("No operators registered.");
            ImGui::TextWrapped("Call ctx.registerOperator() in setup()");
        }
        ImGui::End();
    } else {
        // Main visualization window
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        float estimatedHeight = 120.0f + operators.size() * (thumbnailSize_ + nodeSpacing_);
        ImGui::SetNextWindowSize(ImVec2(windowWidth_, std::min(estimatedHeight, 600.0f)), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Chain Visualizer", &visible_)) {
            ImGui::Text("Operators: %zu", operators.size());
            ImGui::Separator();

            // Render each operator as a node
            for (size_t i = 0; i < operators.size(); ++i) {
                renderOperatorNode(ctx, operators[i], static_cast<int>(i));
            }
        }
        ImGui::End();
    }

    // Render ImGui if we own the context
    if (g_imguiInitialized && g_imguiRenderer) {
        auto* swapChain = ctx.swapChain();
        if (swapChain) {
            auto* rtv = swapChain->GetCurrentBackBufferRTV();
            auto* dsv = swapChain->GetDepthBufferDSV();
            ctx.immediateContext()->SetRenderTargets(1, &rtv, dsv,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        g_imguiRenderer->Render(ctx.immediateContext());
    }
}

void ChainVisualizer::renderOperatorNode(Context& ctx, const OperatorInfo& info, int index) {
    if (!info.op) {
        return;
    }

    ImGui::PushID(index);

    // Node header with operator type and name
    std::string header = info.op->typeName();
    if (!info.name.empty() && info.name != header) {
        header = info.name + " (" + info.op->typeName() + ")";
    }

    bool nodeOpen = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    if (nodeOpen) {
        ImGui::Indent();

        // Show thumbnail if this is a texture operator
        if (info.op->outputKind() == OutputKind::Texture) {
            auto* srv = info.op->getOutputSRV();
            if (srv) {
                auto* tex = srv->GetTexture();
                float displayWidth = static_cast<float>(thumbnailSize_);
                float displayHeight = static_cast<float>(thumbnailSize_);

                // Calculate correct aspect ratio
                if (tex) {
                    const auto& desc = tex->GetDesc();
                    float aspect = static_cast<float>(desc.Width) / static_cast<float>(desc.Height);
                    if (aspect > 1.0f) {
                        // Wider than tall - fit to width
                        displayHeight = displayWidth / aspect;
                    } else {
                        // Taller than wide - fit to height
                        displayWidth = displayHeight * aspect;
                    }
                }

                // Use the texture view pointer as the texture ID for ImGui
                // ImGuiImplDiligent expects ITextureView* cast to ImTextureID
                ImTextureID texId = reinterpret_cast<ImTextureID>(srv);
                ImGui::Image(texId, ImVec2(displayWidth, displayHeight));

                // Show texture dimensions on hover
                if (ImGui::IsItemHovered() && tex) {
                    const auto& desc = tex->GetDesc();
                    ImGui::SetTooltip("%s\n%dx%d", info.op->typeName().c_str(),
                                      desc.Width, desc.Height);
                }
            } else {
                // No output yet - show placeholder
                ImVec2 size(static_cast<float>(thumbnailSize_), static_cast<float>(thumbnailSize_));
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    pos,
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(40, 40, 50, 255)
                );
                ImGui::GetWindowDrawList()->AddRect(
                    pos,
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(80, 80, 100, 255)
                );
                ImGui::Dummy(size);
            }
        }

        // Show parameters with current values (read-only)
        auto paramStrings = info.op->getParamStrings();
        if (!paramStrings.empty()) {
            ImGui::TextDisabled("Parameters:");
            for (const auto& [name, value] : paramStrings) {
                ImGui::BulletText("%s: %s", name.c_str(), value.c_str());
            }
        } else {
            // Fallback to showing just param names if getParamStrings not implemented
            auto params = info.op->params();
            if (!params.empty()) {
                ImGui::TextDisabled("Parameters:");
                for (const auto& param : params) {
                    ImGui::BulletText("%s", param.name.c_str());
                }
            }
        }

        // Show input connections
        int inputCount = 0;
        for (int i = 0; i < 4; ++i) {  // Max 4 inputs
            if (info.op->getInput(i)) {
                inputCount++;
            }
        }
        if (inputCount > 0) {
            ImGui::TextDisabled("Inputs: %d", inputCount);
        }

        ImGui::Unindent();
    }

    ImGui::PopID();
    ImGui::Spacing();
}

void ChainVisualizer::renderConnections(Context& ctx) {
    // TODO: Draw lines between connected operators
    // This requires knowing the screen positions of each operator node
}

} // namespace vivid
