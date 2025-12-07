// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections

#include <vivid/imgui/chain_visualizer.h>
#include <imgui.h>
#include <imnodes.h>
#include <iostream>
#include <algorithm>

namespace vivid::imgui {

ChainVisualizer::~ChainVisualizer() {
    shutdown();
}

void ChainVisualizer::init() {
    if (m_initialized) return;

    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    // Configure style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8.0f, 8.0f);
    style.LinkThickness = 3.0f;
    style.PinCircleRadius = 4.0f;

    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

    ImNodes::DestroyContext();
    m_initialized = false;
    m_layoutBuilt = false;
    m_opToNodeId.clear();
    m_nodePositioned.clear();
}

void ChainVisualizer::buildLayout(const std::vector<vivid::OperatorInfo>& operators) {
    m_opToNodeId.clear();
    m_nodePositioned.clear();

    // Assign node IDs to operators
    for (size_t i = 0; i < operators.size(); ++i) {
        if (operators[i].op) {
            m_opToNodeId[operators[i].op] = static_cast<int>(i);
        }
    }

    // Calculate depth for each operator (distance from sources)
    std::vector<int> depths(operators.size(), 0);

    for (size_t i = 0; i < operators.size(); ++i) {
        vivid::Operator* op = operators[i].op;
        if (!op) continue;

        int maxInputDepth = -1;
        for (size_t j = 0; j < op->inputCount(); ++j) {
            vivid::Operator* input = op->getInput(static_cast<int>(j));
            if (input && m_opToNodeId.count(input)) {
                int inputNodeId = m_opToNodeId[input];
                maxInputDepth = std::max(maxInputDepth, depths[inputNodeId]);
            }
        }
        depths[i] = maxInputDepth + 1;
    }

    // Group operators by depth
    int maxDepth = 0;
    for (int d : depths) maxDepth = std::max(maxDepth, d);

    std::vector<std::vector<int>> columns(maxDepth + 1);
    for (size_t i = 0; i < operators.size(); ++i) {
        columns[depths[i]].push_back(static_cast<int>(i));
    }

    // Position nodes in columns
    const float nodeWidth = 150.0f;
    const float nodeHeight = 100.0f;
    const float xSpacing = 200.0f;
    const float ySpacing = 130.0f;
    const float startX = 50.0f;
    const float startY = 50.0f;

    for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
        float y = startY;
        for (int nodeId : columns[col]) {
            float x = startX + col * xSpacing;
            ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(x, y));
            m_nodePositioned[nodeId] = true;
            y += ySpacing;
        }
    }

    m_layoutBuilt = true;
}

void ChainVisualizer::render(const FrameInput& input, vivid::Context& ctx) {
    if (!m_initialized) {
        init();
    }

    const auto& operators = ctx.registeredOperators();

    // Performance overlay
    float fps = input.dt > 0 ? 1.0f / input.dt : 0.0f;
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::Text("DT: %.3fms", input.dt * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Size: %dx%d", input.width, input.height);
    ImGui::Text("Operators: %zu", operators.size());
    ImGui::End();

    // Controls info
    ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::Text("Tab: Toggle UI");
    ImGui::Text("F: Fullscreen");
    ImGui::End();

    // Node editor
    ImGui::SetNextWindowPos(ImVec2(220, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Chain Visualizer");

    if (operators.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            "No operators registered.");
        ImGui::TextWrapped(
            "Call ctx.registerOperator(\"name\", op) in your chain's setup() "
            "function to visualize your operator graph.");
        ImGui::End();
        return;
    }

    // Build layout if operators changed
    if (!m_layoutBuilt || m_opToNodeId.size() != operators.size()) {
        buildLayout(operators);
    }

    ImNodes::BeginNodeEditor();

    // Render nodes
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);

        ImNodes::BeginNode(nodeId);

        // Title bar - show registered name
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(info.name.c_str());
        ImNodes::EndNodeTitleBar();

        // Show operator type if different from registered name
        std::string typeName = info.op->name();
        if (typeName != info.name) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", typeName.c_str());
        }

        // Show parameters if operator declares them
        auto params = info.op->params();
        if (!params.empty()) {
            ImGui::Separator();
            for (const auto& p : params) {
                switch (p.type) {
                    case vivid::ParamType::Float:
                        ImGui::Text("%s: %.2f", p.name.c_str(), p.defaultVal[0]);
                        break;
                    case vivid::ParamType::Int:
                        ImGui::Text("%s: %d", p.name.c_str(), static_cast<int>(p.defaultVal[0]));
                        break;
                    case vivid::ParamType::Bool:
                        ImGui::Text("%s: %s", p.name.c_str(), p.defaultVal[0] > 0.5f ? "true" : "false");
                        break;
                    case vivid::ParamType::Vec2:
                        ImGui::Text("%s: (%.2f, %.2f)", p.name.c_str(), p.defaultVal[0], p.defaultVal[1]);
                        break;
                    case vivid::ParamType::Vec3:
                    case vivid::ParamType::Color:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f)", p.name.c_str(),
                            p.defaultVal[0], p.defaultVal[1], p.defaultVal[2]);
                        break;
                    case vivid::ParamType::Vec4:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f, %.2f)", p.name.c_str(),
                            p.defaultVal[0], p.defaultVal[1], p.defaultVal[2], p.defaultVal[3]);
                        break;
                    case vivid::ParamType::String:
                        // String params encode value in the name (e.g., "mode: Multiply")
                        ImGui::Text("%s", p.name.c_str());
                        break;
                    default:
                        ImGui::Text("%s", p.name.c_str());
                        break;
                }
            }
        }

        // Input pins - show one for each connected input
        int inputCount = 0;
        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                inputCount = static_cast<int>(j) + 1;
            }
        }

        for (int j = 0; j < inputCount; ++j) {
            ImNodes::BeginInputAttribute(inputAttrId(nodeId, j));
            if (inputCount > 1) {
                ImGui::Text("in %d", j);
            } else {
                ImGui::Text("in");
            }
            ImNodes::EndInputAttribute();
        }

        // Thumbnail - render operator output texture
        WGPUTextureView view = info.op->outputView();
        if (view) {
            // ImGui WebGPU backend accepts WGPUTextureView as ImTextureID
            ImTextureID texId = reinterpret_cast<ImTextureID>(view);
            ImGui::Image(texId, ImVec2(100, 56));  // 16:9 aspect ratio
        } else {
            // No texture - show placeholder
            ImGui::Dummy(ImVec2(100, 40));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(40, 40, 50, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 20, min.y + 12), IM_COL32(100, 100, 120, 255), "no tex");
        }

        // Output pin
        ImNodes::BeginOutputAttribute(outputAttrId(nodeId));
        ImGui::Text("out");
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();
    }

    // Render links
    int linkId = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int destNodeId = static_cast<int>(i);

        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            vivid::Operator* inputOp = info.op->getInput(static_cast<int>(j));
            if (inputOp && m_opToNodeId.count(inputOp)) {
                int sourceNodeId = m_opToNodeId[inputOp];
                ImNodes::Link(linkId++,
                    outputAttrId(sourceNodeId),
                    inputAttrId(destNodeId, static_cast<int>(j)));
            }
        }
    }

    ImNodes::EndNodeEditor();

    ImGui::End();
}

} // namespace vivid::imgui
