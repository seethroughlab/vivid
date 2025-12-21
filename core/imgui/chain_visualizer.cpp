// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections
//
// Addon-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on audio, render3d, or other addons.

#include "chain_visualizer.h"
#include <vivid/operator_viz.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_graph.h>
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

// Platform-specific memory monitoring
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vivid::imgui {

// Special node IDs for output nodes
static constexpr int SCREEN_NODE_ID = 9999;
static constexpr int SPEAKERS_NODE_ID = 9998;

// Thumbnail sizes (16:9 aspect ratio)
static constexpr float THUMB_WIDTH = 100.0f;
static constexpr float THUMB_HEIGHT = 56.0f;
static constexpr float FOCUSED_SCALE = 3.0f;  // 3x larger when focused

// Get process memory usage in bytes
static size_t getProcessMemoryUsage() {
#if defined(__APPLE__)
    task_vm_info_data_t vmInfo;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count) == KERN_SUCCESS) {
        return vmInfo.phys_footprint;  // Actual physical memory used
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t size, resident;
        statm >> size >> resident;
        return resident * sysconf(_SC_PAGESIZE);
    }
    return 0;
#else
    return 0;
#endif
}

// Format bytes as human-readable string (MB or GB)
static std::string formatMemory(size_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return buf;
}

ChainVisualizer::~ChainVisualizer() {
    shutdown();
}

void ChainVisualizer::init() {
    if (m_initialized) return;

    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    // Enable trackpad-friendly panning: Control + Left Click to pan
    // (Can't use Cmd on Mac - system intercepts Cmd+Click)
    ImNodesIO& io = ImNodes::GetIO();
    io.EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyCtrl;

    // Configure style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8.0f, 8.0f);
    style.LinkThickness = 3.0f;
    style.PinCircleRadius = 4.0f;

    // Transparent background - nodes float over the chain output
    ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(0, 0, 0, 0);  // Fully transparent
    ImNodes::GetStyle().Colors[ImNodesCol_GridLine] = IM_COL32(60, 60, 80, 40);     // Very subtle grid
    ImNodes::GetStyle().Colors[ImNodesCol_GridLinePrimary] = IM_COL32(80, 80, 100, 60);

    // Semi-transparent node backgrounds
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackground] = IM_COL32(30, 30, 40, 200);
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(40, 40, 55, 220);
    ImNodes::GetStyle().Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(50, 50, 70, 240);

    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

    // Exit solo mode if active
    if (m_inSoloMode) {
        exitSoloMode();
    }

    ImNodes::DestroyContext();
    m_initialized = false;
    m_layoutBuilt = false;
    m_opToNodeId.clear();
    m_nodePositioned.clear();
}

void ChainVisualizer::selectNodeFromEditor(const std::string& operatorName) {
    // Store the selection to be applied in next render() call
    // (ImNodes calls must happen within the node editor context)
    m_pendingEditorSelection = operatorName;
}

// Estimate node height based on content (parameters are now in inspector panel)
float ChainVisualizer::estimateNodeHeight(const vivid::OperatorInfo& info) const {
    float height = 0.0f;

    // Title bar
    height += 24.0f;

    // Type name (if different from registered name)
    if (info.op && info.op->name() != info.name) {
        height += 18.0f;
    }

    // Input pins (~20px each)
    if (info.op) {
        int inputCount = 0;
        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                inputCount = static_cast<int>(j) + 1;
            }
        }
        height += inputCount * 20.0f;
    }

    // Thumbnail/preview area
    if (info.op) {
        vivid::OutputKind kind = info.op->outputKind();
        if (kind == vivid::OutputKind::Texture || kind == vivid::OutputKind::Geometry) {
            height += 60.0f;  // 56px image + padding
        } else {
            height += 54.0f;  // Icons are slightly smaller
        }
    }

    // Output pin
    height += 20.0f;

    // Node padding
    height += 16.0f;

    return height;
}

void ChainVisualizer::buildLayout(const std::vector<vivid::OperatorInfo>& operators) {
    m_opToNodeId.clear();
    m_nodePositioned.clear();
    m_nodePositioned[SCREEN_NODE_ID] = false;    // Reset Screen node position on layout rebuild
    m_nodePositioned[SPEAKERS_NODE_ID] = false;  // Reset Speakers node position on layout rebuild

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

    // Position nodes in columns using estimated heights
    const float xSpacing = 280.0f;   // Horizontal space between columns
    const float verticalPadding = 20.0f;  // Extra space between nodes
    const float startX = 50.0f;
    const float startY = 50.0f;

    for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
        float y = startY;

        for (size_t idx = 0; idx < columns[col].size(); ++idx) {
            int nodeId = columns[col][idx];
            float x = startX + col * xSpacing;
            ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(x, y));
            m_nodePositioned[nodeId] = true;

            // Move y down by this node's estimated height + padding
            float nodeHeight = estimateNodeHeight(operators[nodeId]);
            y += nodeHeight + verticalPadding;
        }
    }

    m_layoutBuilt = true;
}

void ChainVisualizer::enterSoloMode(vivid::Operator* op, const std::string& name) {
    m_soloOperator = op;
    m_soloOperatorName = name;
    m_inSoloMode = true;
}

void ChainVisualizer::exitSoloMode() {
    m_soloOperator = nullptr;
    m_soloOperatorName.clear();
    m_inSoloMode = false;
}

void ChainVisualizer::renderSoloOverlay(const FrameInput& input, vivid::Context& ctx) {
    if (!m_soloOperator) {
        exitSoloMode();
        return;
    }

    vivid::OutputKind kind = m_soloOperator->outputKind();

    if (kind == vivid::OutputKind::Texture) {
        // For texture operators, display their output texture
        WGPUTextureView view = m_soloOperator->outputView();
        if (view) {
            ctx.setOutputTexture(view);
        }
    }
    // For geometry/audio/other operators, just show the overlay
    // (Geometry operators render their own preview which the user can see in the node)

    // Check for Escape key to exit solo mode
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        exitSoloMode();
        return;
    }

    // Draw solo mode overlay (semi-transparent)
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.5f);  // Semi-transparent background
    ImGui::Begin("Solo Mode", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "SOLO: %s", m_soloOperatorName.c_str());
    if (ImGui::Button("Exit Solo")) {
        exitSoloMode();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "(or press ESC)");
    ImGui::End();
}

void ChainVisualizer::render(const FrameInput& input, vivid::Context& ctx) {
    if (!m_initialized) {
        init();
    }

    // Handle Escape key to exit solo mode
    if (m_inSoloMode && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        exitSoloMode();
    }

    // If in solo mode, render the solo view instead of normal UI
    if (m_inSoloMode) {
        renderSoloOverlay(input, ctx);
        return;  // Don't show the node editor in solo mode
    }

    const auto& operators = ctx.registeredOperators();

    // Menu bar with performance stats and controls
    float fps = input.dt > 0 ? 1.0f / input.dt : 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        // Performance stats on the left
        ImGui::Text("%.1f FPS", fps);
        ImGui::Separator();
        ImGui::Text("%.2fms", input.dt * 1000.0f);
        ImGui::Separator();
        ImGui::Text("%dx%d", input.width, input.height);
        ImGui::Separator();
        ImGui::Text("%zu ops", operators.size());

        // Memory usage display
        ImGui::Separator();
        size_t memBytes = getProcessMemoryUsage();
        std::string memStr = formatMemory(memBytes);
        // Color based on memory usage: green < 500MB, yellow < 2GB, red >= 2GB
        ImVec4 memColor;
        if (memBytes < 500 * 1024 * 1024) {
            memColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
        } else if (memBytes < 2ULL * 1024 * 1024 * 1024) {
            memColor = ImVec4(0.9f, 0.9f, 0.4f, 1.0f);  // Yellow
        } else {
            memColor = ImVec4(0.9f, 0.4f, 0.4f, 1.0f);  // Red
        }
        ImGui::TextColored(memColor, "MEM: %s", memStr.c_str());

        // Audio stats display (if audio is active)
        AudioGraph* audioGraph = ctx.chain().audioGraph();
        if (audioGraph && !audioGraph->empty()) {
            ImGui::Separator();

            // DSP Load with color coding
            float dspLoad = audioGraph->dspLoad();
            float peakLoad = audioGraph->peakDspLoad();
            ImVec4 dspColor;
            if (dspLoad < 0.5f) {
                dspColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
            } else if (dspLoad < 0.8f) {
                dspColor = ImVec4(0.9f, 0.9f, 0.4f, 1.0f);  // Yellow
            } else {
                dspColor = ImVec4(0.9f, 0.4f, 0.4f, 1.0f);  // Red
            }
            ImGui::TextColored(dspColor, "DSP: %.0f%%", dspLoad * 100.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("DSP Load: %.1f%% (Peak: %.1f%%)\nClick to reset peak",
                                  dspLoad * 100.0f, peakLoad * 100.0f);
            }
            if (ImGui::IsItemClicked()) {
                audioGraph->resetPeakDspLoad();
            }

            // Show dropped events if any
            uint64_t dropped = audioGraph->droppedEventCount();
            if (dropped > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "⚠ %llu dropped", dropped);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Audio events dropped (queue overflow)\nClick to reset counter");
                }
                if (ImGui::IsItemClicked()) {
                    audioGraph->resetDroppedEventCount();
                }
            }
        }

        // Controls menu
        if (ImGui::BeginMenu("Controls")) {
            ImGui::MenuItem("Tab: Toggle UI", nullptr, false, false);
            ImGui::MenuItem("F: Fullscreen", nullptr, false, false);
            ImGui::MenuItem("Ctrl+Drag: Pan graph", nullptr, false, false);
            ImGui::MenuItem("S: Solo node", nullptr, false, false);
            ImGui::MenuItem("B: Bypass node", nullptr, false, false);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // Recording controls
        if (m_exporter.isRecording()) {
            // Red recording indicator
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::Text("● REC");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("%d frames (%.1fs)", m_exporter.frameCount(), m_exporter.duration());
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) {
                stopRecording(ctx);
            }
        } else {
            if (ImGui::BeginMenu("Record")) {
                if (ImGui::MenuItem("H.264 (recommended)")) {
                    startRecording(ExportCodec::H264, ctx);
                }
                if (ImGui::MenuItem("H.265 (HEVC)")) {
                    startRecording(ExportCodec::H265, ctx);
                }
                if (ImGui::MenuItem("Animation (ProRes 4444)")) {
                    startRecording(ExportCodec::Animation, ctx);
                }
                ImGui::EndMenu();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Snapshot")) {
                requestSnapshot();
            }
        }

        ImGui::EndMainMenuBar();
    }

    // Node editor - transparent, fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(input.width), static_cast<float>(input.height)));
    ImGui::SetNextWindowBgAlpha(0.0f);  // Fully transparent background
    ImGui::Begin("Chain Visualizer", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (operators.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            "No operators registered.");
        ImGui::TextWrapped(
            "Operators are auto-registered when using chain->init(ctx). "
            "Press Tab to hide this UI.");
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

        // Color nodes based on output type
        vivid::OutputKind outputKind = info.op->outputKind();
        bool pushedStyle = false;

        if (outputKind == vivid::OutputKind::Geometry) {
            // Blue-ish tint for geometry nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 80, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 100, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 120, 180, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Value || outputKind == vivid::OutputKind::ValueArray) {
            // Orange-ish tint for value nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(120, 80, 40, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(150, 100, 50, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 120, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Camera) {
            // Green-ish tint for camera nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 100, 80, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 125, 100, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 150, 120, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Light) {
            // Yellow-ish tint for light nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(120, 100, 40, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(150, 125, 50, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::Audio) {
            // Purple-ish tint for audio nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(100, 60, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(125, 75, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        } else if (outputKind == vivid::OutputKind::AudioValue) {
            // Teal/cyan for audio analysis nodes
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(60, 100, 120, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(75, 125, 150, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(180, 150, 60, 255));
            pushedStyle = true;
        }

        ImNodes::BeginNode(nodeId);

        // Check if bypassed for visual styling
        bool isBypassed = info.op->isBypassed();

        // Title bar - show registered name with Solo and Bypass buttons
        ImNodes::BeginNodeTitleBar();

        // Dim the name if bypassed
        if (isBypassed) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", info.name.c_str());
        } else {
            ImGui::TextUnformatted(info.name.c_str());
        }

        ImGui::SameLine();
        ImGui::PushID(nodeId);

        // Solo button
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(80, 80, 100, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(100, 100, 140, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 120, 180, 255));
        if (ImGui::SmallButton("S")) {
            enterSoloMode(info.op, info.name);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Solo - view full output (or double-click node)");
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // Bypass button - highlight when active
        if (isBypassed) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 100, 40, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 120, 60, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(220, 140, 80, 255));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(80, 80, 100, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(100, 100, 140, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 120, 180, 255));
        }
        if (ImGui::SmallButton("B")) {
            info.op->setBypassed(!isBypassed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isBypassed ? "Bypass ON - click to enable" : "Bypass - skip this operator");
        }
        ImGui::PopStyleColor(3);

        ImGui::PopID();
        ImNodes::EndNodeTitleBar();

        // Show operator type if different from registered name
        std::string typeName = info.op->name();
        if (typeName != info.name) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", typeName.c_str());
        }

        // Parameters are now shown in the Inspector panel (select node to see)

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

        // Thumbnail - render based on output type
        vivid::OutputKind kind = info.op->outputKind();

        // Calculate thumbnail size (3x larger when focused from editor)
        bool nodeFocused = isFocused(info.name);
        float thumbScale = nodeFocused ? FOCUSED_SCALE : 1.0f;
        float thumbW = THUMB_WIDTH * thumbScale;
        float thumbH = THUMB_HEIGHT * thumbScale;

        if (kind == vivid::OutputKind::Texture) {
            WGPUTextureView view = info.op->outputView();
            if (view) {
                // ImGui WebGPU backend accepts WGPUTextureView as ImTextureID
                ImTextureID texId = reinterpret_cast<ImTextureID>(view);
                ImGui::Image(texId, ImVec2(thumbW, thumbH));  // 16:9 aspect ratio (3x when focused)
            } else {
                // Texture operator but no view yet
                ImGui::Dummy(ImVec2(thumbW, thumbH * 0.7f));
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(40, 40, 50, 255), 4.0f);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(min.x + 20, min.y + 12), IM_COL32(100, 100, 120, 255), "no tex");
            }
        } else if (kind == vivid::OutputKind::Geometry) {
            // Geometry operator - let operator draw its own preview
            ImGui::Dummy(ImVec2(thumbW, thumbH));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            // Let operator draw its visualization (handles its own preview texture)
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (!info.op->drawVisualization(dl, min.x, min.y, max.x, max.y)) {
                // Fallback if operator doesn't provide visualization
                dl->AddRectFilled(min, max, IM_COL32(30, 50, 70, 255), 4.0f);
                dl->AddText(ImVec2(min.x + 15, min.y + 20), IM_COL32(100, 180, 255, 255), "geometry");
            }
        } else if (kind == vivid::OutputKind::Value || kind == vivid::OutputKind::ValueArray) {
            // Value operator - show numeric display
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.7f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(50, 40, 30, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 25, min.y + 12), IM_COL32(200, 180, 100, 255),
                kind == vivid::OutputKind::Value ? "Value" : "Values");
        } else if (kind == vivid::OutputKind::Camera) {
            // Camera operator - draw camera icon
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(30, 60, 50, 255), 4.0f);

            // Draw camera body (rectangle) - scale icon with thumbnail
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f;
            float iconScale = thumbScale;
            ImU32 iconColor = IM_COL32(100, 200, 160, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 20*iconScale, cy - 10*iconScale), ImVec2(cx + 10*iconScale, cy + 10*iconScale), iconColor, 3.0f);
            // Draw lens (triangle)
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                ImVec2(cx + 10*iconScale, cy - 8*iconScale), ImVec2(cx + 25*iconScale, cy), ImVec2(cx + 10*iconScale, cy + 8*iconScale), iconColor);
            // Draw viewfinder
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 15*iconScale, cy - 18*iconScale), ImVec2(cx, cy - 10*iconScale), iconColor, 2.0f);
        } else if (kind == vivid::OutputKind::Light) {
            // Light operator - draw light bulb icon
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(60, 50, 25, 255), 4.0f);

            // Draw bulb (circle) - scale icon with thumbnail
            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f - 3 * thumbScale;
            float iconScale = thumbScale;
            ImU32 iconColor = IM_COL32(255, 220, 100, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, cy), 12 * iconScale, iconColor);
            // Draw base
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cx - 6*iconScale, cy + 10*iconScale), ImVec2(cx + 6*iconScale, cy + 18*iconScale), IM_COL32(180, 180, 180, 255), 2.0f);
            // Draw rays
            ImU32 rayColor = IM_COL32(255, 240, 150, 180);
            for (int i = 0; i < 8; i++) {
                float angle = i * 3.14159f / 4;
                float r1 = 15 * iconScale, r2 = 22 * iconScale;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cx + r1 * cos(angle), cy + r1 * sin(angle)),
                    ImVec2(cx + r2 * cos(angle), cy + r2 * sin(angle)),
                    rayColor, 2.0f);
            }
        } else if (kind == vivid::OutputKind::Audio) {
            // Audio operator - draw specialized visualization based on type
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            AudioOperator* audioOp = static_cast<AudioOperator*>(info.op);

            float cx = (min.x + max.x) * 0.5f;
            float cy = (min.y + max.y) * 0.5f;
            float height = max.y - min.y;
            float width = max.x - min.x;

            // Let operator draw its own visualization, or use generic fallback
            if (!info.op->drawVisualization(dl, min.x, min.y, max.x, max.y)) {
                // Generic audio - draw waveform
                dl->AddRectFilled(min, max, IM_COL32(50, 30, 60, 255), 4.0f);

                const AudioBuffer* buf = audioOp->outputBuffer();
                float startX = min.x + 4.0f;
                float waveWidth = width - 8.0f;

                ImU32 waveColor = IM_COL32(180, 140, 220, 255);
                ImU32 waveColorDim = IM_COL32(120, 80, 160, 200);

                if (buf && buf->isValid() && buf->sampleCount() > 0) {
                    constexpr int NUM_POINTS = 48;
                    uint32_t step = std::max(1u, buf->frameCount / NUM_POINTS);

                    float prevX = startX;
                    float prevY = cy;

                    for (int i = 0; i < NUM_POINTS && i * step < buf->frameCount; i++) {
                        uint32_t frameIdx = i * step;
                        float sample = (buf->samples[frameIdx * 2] + buf->samples[frameIdx * 2 + 1]) * 0.5f;
                        sample = std::max(-1.0f, std::min(1.0f, sample));

                        float x = startX + (waveWidth * i / (NUM_POINTS - 1));
                        float y = cy - sample * height * 0.4f;

                        if (i > 0) {
                            dl->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), waveColor, 1.5f);
                        }
                        prevX = x;
                        prevY = y;
                    }
                } else {
                    for (int i = 0; i < 3; i++) {
                        float x1 = startX + waveWidth * i / 3.0f;
                        float x2 = startX + waveWidth * (i + 1) / 3.0f;
                        float xMid = (x1 + x2) * 0.5f;
                        float yOffset = (i == 1) ? height * 0.15f : -height * 0.1f;
                        dl->AddBezierQuadratic(
                            ImVec2(x1, cy), ImVec2(xMid, cy + yOffset), ImVec2(x2, cy),
                            waveColorDim, 1.5f);
                    }
                }
            }
        } else if (kind == vivid::OutputKind::AudioValue) {
            // Audio analysis - operators provide their own visualization
            ImGui::Dummy(ImVec2(thumbW, thumbH * 0.9f));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Dark purple background
            dl->AddRectFilled(min, max, IM_COL32(40, 30, 50, 255), 4.0f);

            // Let the operator draw its own visualization
            if (!info.op->drawVisualization(dl, min.x, min.y, max.x, max.y)) {
                // Fallback: simple "AV" label if operator doesn't implement visualization
                dl->AddText(ImVec2(min.x + 35, min.y + 18), IM_COL32(150, 100, 180, 255), "AV");
            }
        } else {
            // Unknown type
            ImGui::Dummy(ImVec2(100, 40));
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(40, 40, 50, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(min.x + 20, min.y + 12), IM_COL32(100, 100, 120, 255), "???");
        }

        // Output pin
        ImNodes::BeginOutputAttribute(outputAttrId(nodeId));
        ImGui::Text("out");
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();

        // Pop color styles if we pushed them
        if (pushedStyle) {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }

    // Render the Screen output node (special node showing final output destination)
    vivid::Operator* outputOp = nullptr;
    if (ctx.hasChain()) {
        outputOp = ctx.chain().getOutput();
    }

    if (outputOp && m_opToNodeId.count(outputOp)) {
        // Position Screen node to the right of the output operator
        if (!m_nodePositioned[SCREEN_NODE_ID]) {
            int outputNodeId = m_opToNodeId[outputOp];
            ImVec2 outputPos = ImNodes::GetNodeGridSpacePos(outputNodeId);
            ImNodes::SetNodeGridSpacePos(SCREEN_NODE_ID, ImVec2(outputPos.x + 280.0f, outputPos.y));
            m_nodePositioned[SCREEN_NODE_ID] = true;
        }

        // Green color for Screen node
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 120, 60, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(50, 150, 75, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(60, 180, 90, 255));

        ImNodes::BeginNode(SCREEN_NODE_ID);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Screen");
        ImNodes::EndNodeTitleBar();

        // Input pin for the texture
        ImNodes::BeginInputAttribute(inputAttrId(SCREEN_NODE_ID, 0));
        ImGui::Text("display");
        ImNodes::EndInputAttribute();

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // Render the Speakers audio output node (if audio output is designated)
    vivid::Operator* audioOutputOp = nullptr;
    if (ctx.hasChain()) {
        audioOutputOp = ctx.chain().getAudioOutput();
    }

    if (audioOutputOp && m_opToNodeId.count(audioOutputOp)) {
        // Position Speakers node to the right of the audio output operator
        if (!m_nodePositioned[SPEAKERS_NODE_ID]) {
            int audioOutputNodeId = m_opToNodeId[audioOutputOp];
            ImVec2 audioOutputPos = ImNodes::GetNodeGridSpacePos(audioOutputNodeId);
            ImNodes::SetNodeGridSpacePos(SPEAKERS_NODE_ID, ImVec2(audioOutputPos.x + 280.0f, audioOutputPos.y));
            m_nodePositioned[SPEAKERS_NODE_ID] = true;
        }

        // Purple color for Speakers node (matches audio operators)
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(100, 60, 120, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(125, 75, 150, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(150, 90, 180, 255));

        ImNodes::BeginNode(SPEAKERS_NODE_ID);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Speakers");
        ImNodes::EndNodeTitleBar();

        // Input pin for the audio
        ImNodes::BeginInputAttribute(inputAttrId(SPEAKERS_NODE_ID, 0));
        ImGui::Text("audio");
        ImNodes::EndInputAttribute();

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
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

    // Link from output operator to Screen node
    if (outputOp && m_opToNodeId.count(outputOp)) {
        int outputNodeId = m_opToNodeId[outputOp];
        ImNodes::Link(linkId++,
            outputAttrId(outputNodeId),
            inputAttrId(SCREEN_NODE_ID, 0));
    }

    // Link from audio output operator to Speakers node
    if (audioOutputOp && m_opToNodeId.count(audioOutputOp)) {
        int audioOutputNodeId = m_opToNodeId[audioOutputOp];
        ImNodes::Link(linkId++,
            outputAttrId(audioOutputNodeId),
            inputAttrId(SPEAKERS_NODE_ID, 0));
    }

    // Handle pending editor selection (from VSCode)
    if (!m_pendingEditorSelection.empty()) {
        // Find the operator by name and select its node
        for (const auto& info : operators) {
            if (info.name == m_pendingEditorSelection && info.op && m_opToNodeId.count(info.op)) {
                int nodeId = m_opToNodeId[info.op];
                ImNodes::ClearNodeSelection();
                ImNodes::SelectNode(nodeId);
                // Center view on the selected node
                ImNodes::EditorContextMoveToNode(nodeId);
                break;
            }
        }
        m_pendingEditorSelection.clear();
    }

    ImNodes::EndNodeEditor();

    // Update selection state from ImNodes
    updateSelection(operators);

    // Show tooltip with resource stats when hovering over a node
    int hoveredNodeId = -1;
    if (ImNodes::IsNodeHovered(&hoveredNodeId) && hoveredNodeId >= 0 &&
        hoveredNodeId != SCREEN_NODE_ID && hoveredNodeId != SPEAKERS_NODE_ID) {
        // Find the operator for this node
        for (const auto& info : operators) {
            if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == hoveredNodeId) {
                ImGui::BeginTooltip();

                // Operator type and name
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", info.op->name().c_str());
                if (info.name != info.op->name()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(%s)", info.name.c_str());
                }

                ImGui::Separator();

                // Output type
                vivid::OutputKind kind = info.op->outputKind();
                const char* kindStr = "Unknown";
                switch (kind) {
                    case vivid::OutputKind::Texture: kindStr = "Texture"; break;
                    case vivid::OutputKind::Geometry: kindStr = "Geometry"; break;
                    case vivid::OutputKind::Audio: kindStr = "Audio"; break;
                    case vivid::OutputKind::AudioValue: kindStr = "Audio Value"; break;
                    case vivid::OutputKind::Value: kindStr = "Value"; break;
                    case vivid::OutputKind::ValueArray: kindStr = "Value Array"; break;
                    case vivid::OutputKind::Camera: kindStr = "Camera"; break;
                    case vivid::OutputKind::Light: kindStr = "Light"; break;
                }
                ImGui::Text("Output: %s", kindStr);

                // Resource info based on type
                if (kind == vivid::OutputKind::Texture) {
                    WGPUTexture tex = info.op->outputTexture();
                    if (tex) {
                        uint32_t w = wgpuTextureGetWidth(tex);
                        uint32_t h = wgpuTextureGetHeight(tex);
                        // RGBA16Float = 8 bytes per pixel
                        size_t memBytes = w * h * 8;
                        ImGui::Text("Size: %ux%u", w, h);
                        ImGui::Text("Memory: ~%.1f MB", memBytes / (1024.0f * 1024.0f));
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No texture");
                    }
                } else if (kind == vivid::OutputKind::Geometry) {
                    // Geometry stats - operators can provide their own via getVisualizationData
                    ImGui::Text("Type: Geometry");
                } else if (kind == vivid::OutputKind::Audio) {
                    AudioOperator* audioOp = static_cast<AudioOperator*>(info.op);
                    const AudioBuffer* buf = audioOp->outputBuffer();
                    if (buf && buf->isValid()) {
                        ImGui::Text("Channels: %u", buf->channels);
                        ImGui::Text("Frames: %u", buf->frameCount);
                    }
                }

                // Bypass status
                if (info.op->isBypassed()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "BYPASSED");
                }

                ImGui::EndTooltip();
                break;
            }
        }
    }

    // Handle blank space click to deselect
    int hoveredNodeCheck = -1;
    bool isNodeHovered = ImNodes::IsNodeHovered(&hoveredNodeCheck);
    int hoveredLink = -1;
    bool isLinkHovered = ImNodes::IsLinkHovered(&hoveredLink);
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !isNodeHovered && !isLinkHovered) {
        ImNodes::ClearNodeSelection();
        clearSelection();
    }

    // Handle double-click on nodes to enter solo mode
    int hoveredNode = -1;
    if (ImNodes::IsNodeHovered(&hoveredNode) && ImGui::IsMouseDoubleClicked(0)) {
        // Find the operator for this node
        for (const auto& info : operators) {
            if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == hoveredNode) {
                enterSoloMode(info.op, info.name);
                break;
            }
        }
    }

    ImGui::End();

    // Render debug values panel if there are any
    renderDebugPanel(ctx);
}

// -------------------------------------------------------------------------
// Debug Values Panel
// -------------------------------------------------------------------------

void ChainVisualizer::renderDebugPanel(vivid::Context& ctx) {
    const auto& debugValues = ctx.debugValues();
    if (debugValues.empty()) return;

    // Position in bottom-left corner
    ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8f);

    if (ImGui::Begin("Debug Values", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
        for (const auto& [name, dv] : debugValues) {
            // Skip values that weren't updated this frame (grayed out)
            if (!dv.updatedThisFrame) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            // Name column (fixed width)
            ImGui::Text("%-12s", name.c_str());
            ImGui::SameLine();

            // Sparkline graph
            if (!dv.history.empty()) {
                // Convert deque to vector for PlotLines
                std::vector<float> historyVec(dv.history.begin(), dv.history.end());

                // Find min/max for scaling
                float minVal = *std::min_element(historyVec.begin(), historyVec.end());
                float maxVal = *std::max_element(historyVec.begin(), historyVec.end());

                // Ensure some range even for constant values
                if (maxVal - minVal < 0.001f) {
                    minVal -= 0.5f;
                    maxVal += 0.5f;
                }

                // Draw sparkline (use ##name for unique ID)
                std::string plotId = "##" + name;
                ImGui::PlotLines(plotId.c_str(), historyVec.data(), static_cast<int>(historyVec.size()),
                               0, nullptr, minVal, maxVal, ImVec2(120, 20));
            }

            ImGui::SameLine();

            // Current value
            ImGui::Text("%7.3f", dv.current);

            if (!dv.updatedThisFrame) {
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();
}

// -------------------------------------------------------------------------
// Video Recording
// -------------------------------------------------------------------------

void ChainVisualizer::startRecording(ExportCodec codec, vivid::Context& ctx) {
    // Generate output path in the project directory (same as chain.cpp)
    std::string projectDir = ".";
    const std::string& chainPath = ctx.chainPath();
    if (!chainPath.empty()) {
        size_t lastSlash = chainPath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            projectDir = chainPath.substr(0, lastSlash);
        }
    }
    std::string outputPath = VideoExporter::generateOutputPath(projectDir, codec);

    // Get output resolution from the actual output texture
    int width = ctx.width();
    int height = ctx.height();

    WGPUTexture outputTex = ctx.chain().outputTexture();
    if (outputTex) {
        width = static_cast<int>(wgpuTextureGetWidth(outputTex));
        height = static_cast<int>(wgpuTextureGetHeight(outputTex));
    }

    float fps = 60.0f;  // TODO: Get from context if available

    // Check if chain has audio output
    bool hasAudio = ctx.chain().getAudioOutput() != nullptr;

    bool started = false;
    if (hasAudio) {
        // Start with audio muxing
        started = m_exporter.startWithAudio(outputPath, width, height, fps, codec, 48000, 2);
        if (started) {
            // Set recording mode so audio operators generate correct amount per frame
            ctx.setRecordingMode(true, fps);
            printf("[ChainVisualizer] Recording started with audio: %s\n", outputPath.c_str());
        }
    } else {
        started = m_exporter.start(outputPath, width, height, fps, codec);
        if (started) {
            ctx.setRecordingMode(true, fps);
            printf("[ChainVisualizer] Recording started: %s\n", outputPath.c_str());
        }
    }

    if (!started) {
        printf("[ChainVisualizer] Failed to start recording: %s\n", m_exporter.error().c_str());
    }
}

void ChainVisualizer::stopRecording(vivid::Context& ctx) {
    m_exporter.stop();
    ctx.setRecordingMode(false);
}

void ChainVisualizer::saveSnapshot(WGPUDevice device, WGPUQueue queue, WGPUTexture texture, vivid::Context& ctx) {
    m_snapshotRequested = false;

    if (!texture) {
        printf("[ChainVisualizer] Snapshot failed: no output texture\n");
        return;
    }

    // Generate output filename in project directory
    std::string projectDir = ".";
    const std::string& chainPath = ctx.chainPath();
    if (!chainPath.empty()) {
        fs::path p(chainPath);
        if (p.has_parent_path()) {
            projectDir = p.parent_path().string();
        }
    }

    // Find next available snapshot number
    int snapshotNum = 1;
    std::string outputPath;
    do {
        outputPath = projectDir + "/snapshot_" + std::to_string(snapshotNum) + ".png";
        snapshotNum++;
    } while (fs::exists(outputPath) && snapshotNum < 10000);

    // Delegate to VideoExporter's static snapshot utility
    if (VideoExporter::saveSnapshot(device, queue, texture, outputPath)) {
        printf("[ChainVisualizer] Snapshot saved: %s\n", outputPath.c_str());
    } else {
        printf("[ChainVisualizer] Snapshot failed: couldn't save PNG\n");
    }
}

void ChainVisualizer::updateSelection(const std::vector<vivid::OperatorInfo>& operators) {
    int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected == 1) {
        int selectedId;
        ImNodes::GetSelectedNodes(&selectedId);

        // Only update if selection changed
        if (selectedId != m_selectedNodeId) {
            m_selectedNodeId = selectedId;

            // Find the operator for this node
            m_selectedOp = nullptr;
            m_selectedOpName.clear();
            for (const auto& info : operators) {
                if (info.op && m_opToNodeId.count(info.op) && m_opToNodeId[info.op] == selectedId) {
                    m_selectedOp = info.op;
                    m_selectedOpName = info.name;
                    break;
                }
            }
        }
    } else if (numSelected == 0 || numSelected > 1) {
        // Clear selection if none or multiple selected
        if (m_selectedOp != nullptr) {
            clearSelection();
        }
    }
}

void ChainVisualizer::clearSelection() {
    m_selectedNodeId = -1;
    m_selectedOp = nullptr;
    m_selectedOpName.clear();
}

void ChainVisualizer::setFocusedNode(const std::string& operatorName) {
    m_focusedOperatorName = operatorName;
    m_focusedModeActive = !operatorName.empty();
}

void ChainVisualizer::clearFocusedNode() {
    m_focusedOperatorName.clear();
    m_focusedModeActive = false;
}

bool ChainVisualizer::isFocused(const std::string& operatorName) const {
    return m_focusedModeActive && m_focusedOperatorName == operatorName;
}

} // namespace vivid::imgui
