// Chain Visualizer Implementation - Node Graph with imnodes

#include "vivid/chain_visualizer.h"
#include "vivid/operator.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuiImplDiligent.hpp>
#include <imnodes.h>

#include <SwapChain.h>
#include <DeviceContext.h>
#include <RenderDevice.h>
#include <TextureView.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace vivid {

namespace {
    // Global ImGui state for the visualizer
    std::unique_ptr<Diligent::ImGuiImplDiligent> g_imguiRenderer;
    bool g_imguiInitialized = false;

    // Attribute ID scheme:
    // - Output attribute: nodeId * 100
    // - Input attribute N: nodeId * 100 + N + 1
    inline int outputAttrId(int nodeId) { return nodeId * 100; }
    inline int inputAttrId(int nodeId, int inputIndex) { return nodeId * 100 + inputIndex + 1; }
    inline int nodeIdFromAttr(int attrId) { return attrId / 100; }
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
    } else {
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

    // Initialize imnodes
    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    // Configure imnodes style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8.0f, 8.0f);
    style.NodeBorderThickness = 1.0f;
    style.LinkThickness = 3.0f;
    style.LinkLineSegmentsPerLength = 0.1f;
    style.PinCircleRadius = 4.0f;
    style.PinOffset = 0.0f;

    // Make links more visible
    ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(100, 180, 255, 255));
    ImNodes::PushColorStyle(ImNodesCol_LinkHovered, IM_COL32(150, 200, 255, 255));
    ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(200, 220, 255, 255));

    imnodesInitialized_ = true;
}

void ChainVisualizer::shutdown() {
    if (!initialized_) {
        return;
    }

    if (imnodesInitialized_) {
        ImNodes::PopColorStyle();  // LinkSelected
        ImNodes::PopColorStyle();  // LinkHovered
        ImNodes::PopColorStyle();  // Link
        ImNodes::DestroyContext();
        imnodesInitialized_ = false;
    }

    if (g_imguiInitialized) {
        ImGui_ImplGlfw_Shutdown();
        g_imguiRenderer.reset();
        g_imguiInitialized = false;
    }

    initialized_ = false;
}

void ChainVisualizer::beginFrame(Context& ctx) {
    // Need to run if visible OR if there's an error to display
    if (!initialized_ || (!visible_ && !hasError_)) {
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
    // Need to run if visible OR if there's an error to display
    if (!initialized_ || (!visible_ && !hasError_)) {
        return;
    }

    // Always render error overlay first (if any)
    if (hasError_) {
        renderErrorOverlay();
    }

    // Only render chain visualization if visible (not just for errors)
    if (!visible_) {
        // Render ImGui (for error overlay) and return
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
        return;
    }

    // Render the node graph
    renderNodeGraph(ctx);

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

void ChainVisualizer::buildGraphLayout(Context& ctx) {
    const auto& operators = ctx.registeredOperators();
    if (operators.empty()) return;

    // Build operator -> node ID mapping
    std::unordered_map<Operator*, int> opToNodeId;
    for (size_t i = 0; i < operators.size(); ++i) {
        opToNodeId[operators[i].op] = static_cast<int>(i);
    }

    // Calculate depth for each operator (distance from sources)
    std::vector<int> depths(operators.size(), 0);

    // Find max depth for each operator by traversing inputs
    for (size_t i = 0; i < operators.size(); ++i) {
        Operator* op = operators[i].op;
        int maxInputDepth = -1;

        for (int inputIdx = 0; inputIdx < 4; ++inputIdx) {
            Operator* input = op->getInput(inputIdx);
            if (input && opToNodeId.count(input)) {
                int inputNodeId = opToNodeId[input];
                maxInputDepth = std::max(maxInputDepth, depths[inputNodeId]);
            }
        }

        depths[i] = maxInputDepth + 1;
    }

    // Group operators by depth
    int maxDepth = *std::max_element(depths.begin(), depths.end());
    std::vector<std::vector<int>> depthGroups(maxDepth + 1);
    for (size_t i = 0; i < operators.size(); ++i) {
        depthGroups[depths[i]].push_back(static_cast<int>(i));
    }

    // Position nodes: left-to-right by depth, top-to-bottom within each depth
    float nodeWidth = static_cast<float>(thumbnailSize_) + 40.0f;
    float nodeHeight = static_cast<float>(thumbnailSize_) + 80.0f;
    float horizontalSpacing = 80.0f;
    float verticalSpacing = 40.0f;

    for (int depth = 0; depth <= maxDepth; ++depth) {
        float x = 50.0f + depth * (nodeWidth + horizontalSpacing);
        float startY = 50.0f;

        for (size_t i = 0; i < depthGroups[depth].size(); ++i) {
            int nodeId = depthGroups[depth][i];
            float y = startY + i * (nodeHeight + verticalSpacing);
            ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(x, y));
        }
    }

    needsLayout_ = false;
    lastOperatorCount_ = operators.size();
}

void ChainVisualizer::renderNodeGraph(Context& ctx) {
    const auto& operators = ctx.registeredOperators();

    // Check if we need to relayout (operator count changed)
    if (operators.size() != lastOperatorCount_) {
        needsLayout_ = true;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Chain Visualizer", &visible_, ImGuiWindowFlags_NoScrollbar)) {
        if (operators.empty()) {
            ImGui::TextWrapped("No operators registered.");
            ImGui::TextWrapped("Call ctx.registerOperator() in setup()");
        } else {
            // Build operator -> node ID mapping for links
            std::unordered_map<Operator*, int> opToNodeId;
            for (size_t i = 0; i < operators.size(); ++i) {
                opToNodeId[operators[i].op] = static_cast<int>(i);
            }

            ImNodes::BeginNodeEditor();

            // Auto-layout on first render or when operators change
            if (needsLayout_) {
                buildGraphLayout(ctx);
            }

            // Render each operator as a node
            for (size_t i = 0; i < operators.size(); ++i) {
                const OperatorInfo& info = operators[i];
                int nodeId = static_cast<int>(i);

                if (!info.op) continue;

                ImNodes::BeginNode(nodeId);

                // Title bar
                ImNodes::BeginNodeTitleBar();
                std::string title = info.name.empty() ? info.op->typeName() : info.name;
                ImGui::TextUnformatted(title.c_str());
                ImNodes::EndNodeTitleBar();

                // Count actual inputs
                int inputCount = 0;
                for (int inputIdx = 0; inputIdx < 4; ++inputIdx) {
                    if (info.op->getInput(inputIdx)) {
                        inputCount = inputIdx + 1;
                    }
                }
                // Show at least 1 input pin for operators that can have inputs
                if (inputCount == 0 && info.op->typeName() != "Noise" &&
                    info.op->typeName() != "SolidColor" &&
                    info.op->typeName() != "Gradient" &&
                    info.op->typeName() != "Shape") {
                    inputCount = 1;
                }

                // Input attributes (pins on left)
                for (int inputIdx = 0; inputIdx < inputCount; ++inputIdx) {
                    ImNodes::BeginInputAttribute(inputAttrId(nodeId, inputIdx));
                    if (inputCount > 1) {
                        ImGui::Text("In %d", inputIdx);
                    } else {
                        ImGui::Text("In");
                    }
                    ImNodes::EndInputAttribute();
                }

                // Thumbnail
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
                                displayHeight = displayWidth / aspect;
                            } else {
                                displayWidth = displayHeight * aspect;
                            }
                        }

                        ImTextureID texId = reinterpret_cast<ImTextureID>(srv);
                        ImGui::Image(texId, ImVec2(displayWidth, displayHeight));
                    } else {
                        // Placeholder
                        ImVec2 size(static_cast<float>(thumbnailSize_), static_cast<float>(thumbnailSize_) * 0.5625f);
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            pos,
                            ImVec2(pos.x + size.x, pos.y + size.y),
                            IM_COL32(40, 40, 50, 255)
                        );
                        ImGui::Dummy(size);
                    }
                }

                // Output attribute (pin on right)
                ImNodes::BeginOutputAttribute(outputAttrId(nodeId));
                ImGui::Text("Out");
                ImNodes::EndOutputAttribute();

                ImNodes::EndNode();
            }

            // Render links
            int linkId = 0;
            for (size_t i = 0; i < operators.size(); ++i) {
                Operator* op = operators[i].op;
                int destNodeId = static_cast<int>(i);

                for (int inputIdx = 0; inputIdx < 4; ++inputIdx) {
                    Operator* inputOp = op->getInput(inputIdx);
                    if (inputOp && opToNodeId.count(inputOp)) {
                        int sourceNodeId = opToNodeId[inputOp];
                        ImNodes::Link(linkId++,
                            outputAttrId(sourceNodeId),
                            inputAttrId(destNodeId, inputIdx));
                    }
                }
            }

            ImNodes::EndNodeEditor();
        }
    }
    ImGui::End();
}

void ChainVisualizer::setError(const std::string& errorMessage) {
    hasError_ = true;
    errorMessage_ = errorMessage;
}

void ChainVisualizer::clearError() {
    hasError_ = false;
    errorMessage_.clear();
}

void ChainVisualizer::renderErrorOverlay() {
    if (!hasError_ || errorMessage_.empty()) {
        return;
    }

    // Get window size for centering
    ImGuiIO& io = ImGui::GetIO();
    float windowWidth = 600.0f;
    float windowHeight = 400.0f;
    float posX = (io.DisplaySize.x - windowWidth) * 0.5f;
    float posY = (io.DisplaySize.y - windowHeight) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

    // Red-tinted window style for errors
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.1f, 0.1f, 0.95f));

    bool open = true;
    if (ImGui::Begin("Compilation Error", &open, ImGuiWindowFlags_NoCollapse)) {
        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("Failed to compile chain.cpp");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Compiler output in scrollable region
        ImGui::Text("Compiler Output:");
        ImGui::Spacing();

        // Scrollable child region for error text
        float footerHeight = ImGui::GetFrameHeightWithSpacing() + 10.0f;
        ImVec2 childSize(0, -footerHeight);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        if (ImGui::BeginChild("ErrorText", childSize, true)) {
            // Use monospace-style rendering
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.8f, 1.0f));
            ImGui::TextUnformatted(errorMessage_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Footer hint
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("Fix the error and save to reload");
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleColor(3);
}

} // namespace vivid
