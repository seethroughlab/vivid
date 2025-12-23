// Vivid Chain Visualizer Implementation
// Shows registered operators as nodes with connections
//
// Addon-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on audio, render3d, or other addons.

#include <vivid/chain_visualizer.h>
#include <vivid/viz_draw_list.h>
#include <vivid/operator_viz.h>
#include <vivid/audio_operator.h>
#include <vivid/audio_graph.h>
#include <vivid/asset_loader.h>
#include <vivid/frame_input.h>
#include <vivid/effects/texture_operator.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <climits>
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

namespace vivid {

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
    // NodeGraph initialization happens in initNodeGraph() - called lazily
    m_initialized = true;
}

void ChainVisualizer::shutdown() {
    if (!m_initialized) return;

    // Exit solo mode if active
    if (m_inSoloMode) {
        exitSoloMode();
    }

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
    // Match the actual calculation in node_graph.cpp endNode():
    // node.size.y = nodeTitleHeight + contentAreaHeight + pinsHeight
    // where pinsHeight = max(1, maxPins) * pinSpacing + nodeContentPadding * 2
    //
    // NodeGraphStyle defaults:
    //   nodeTitleHeight = 48.0f
    //   pinSpacing = 40.0f
    //   nodeContentPadding = 8.0f
    //   contentAreaHeight = 128.0f (if has content callback)

    const float nodeTitleHeight = 48.0f;
    const float pinSpacing = 40.0f;
    const float nodeContentPadding = 8.0f;
    const float contentAreaHeight = 128.0f;

    // Count pins
    int maxPins = 1;  // At least 1 for output
    if (info.op) {
        int inputCount = 0;
        for (size_t j = 0; j < info.op->inputCount(); ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                inputCount = static_cast<int>(j) + 1;
            }
        }
        maxPins = std::max(maxPins, inputCount);
    }

    float pinsHeight = maxPins * pinSpacing + nodeContentPadding * 2;
    float height = nodeTitleHeight + contentAreaHeight + pinsHeight;

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
            // Position is now managed by NodeGraph, not imnodes
            m_nodeGraph.setNodePosition(nodeId, {x, y});
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

// =========================================================================
// OLD IMNODES-BASED CODE - DISABLED
// This code is no longer used. renderNodeGraph() uses the new OverlayCanvas.
// =========================================================================
#if 0

void ChainVisualizer::renderSoloOverlay(const FrameInput& input, vivid::Context& ctx) {
    // Old ImGui-based implementation - now handled in renderNodeGraph()
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

#endif // disabled old imnodes code

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

#if 0 // disabled - uses ImNodes
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
#endif

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

// -------------------------------------------------------------------------
// New NodeGraph System (testing)
// -------------------------------------------------------------------------

void ChainVisualizer::initNodeGraph(vivid::Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_nodeGraphInitialized) return;

    // Initialize overlay canvas with correct surface format
    if (!m_overlay.init(ctx, surfaceFormat)) {
        std::cerr << "[ChainVisualizer] Failed to initialize OverlayCanvas\n";
        return;
    }

    // Load fonts for node graph:
    // Font index 0: Inter Regular (body text, labels)
    // Font index 1: Inter Medium (node titles only)
    // Font index 2: Roboto Mono (numeric displays - FPS, timings, etc.)
    auto exeDir = AssetLoader::instance().executableDir();
    auto projectRoot = exeDir.parent_path().parent_path();  // build/bin -> build -> project

    // Paths to font files
    std::string regularPath = (projectRoot / "assets/fonts/Inter/static/Inter_18pt-Regular.ttf").string();
    std::string mediumPath = (projectRoot / "assets/fonts/Inter/static/Inter_18pt-Medium.ttf").string();
    std::string monoPath = (projectRoot / "assets/fonts/Roboto_Mono/static/RobotoMono-Regular.ttf").string();

    // Load Inter Regular as primary font (index 0) - 36px for tooltips/labels
    if (m_overlay.loadFont(ctx, regularPath, 36.0f)) {
        std::cerr << "[ChainVisualizer] Loaded Inter Regular (36px)\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Regular font\n";
    }

    // Load Inter Medium for node titles (index 1) - larger for visibility
    if (m_overlay.loadFontSize(ctx, mediumPath, 40.0f, 1)) {
        std::cerr << "[ChainVisualizer] Loaded Inter Medium (40px) for titles\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Inter Medium font\n";
    }

    // Load Roboto Mono for numeric displays (index 2) - 32px for status bar
    if (m_overlay.loadFontSize(ctx, monoPath, 32.0f, 2)) {
        std::cerr << "[ChainVisualizer] Loaded Roboto Mono (32px) for metrics\n";
    } else {
        std::cerr << "[ChainVisualizer] Warning: Could not load Roboto Mono font\n";
    }

    m_nodeGraphInitialized = true;
    std::cerr << "[ChainVisualizer] NodeGraph initialized\n";
}

void ChainVisualizer::renderNodeGraph(WGPURenderPassEncoder pass, const FrameInput& input, vivid::Context& ctx) {
    if (!m_nodeGraphInitialized) {
        initNodeGraph(ctx, input.surfaceFormat);
        if (!m_nodeGraphInitialized) return;
    }

    const auto& operators = ctx.registeredOperators();
    if (operators.empty()) return;

    // Build input for node graph
    // Scale mouse from window coords to framebuffer coords (for HiDPI/Retina)
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    glm::vec2 scaledMousePos = input.mousePos * scale;

    vivid::NodeGraphInput graphInput;
    graphInput.mousePos = scaledMousePos;
    // Calculate mouse delta from previous frame
    static glm::vec2 lastMousePos = scaledMousePos;
    graphInput.mouseDelta = scaledMousePos - lastMousePos;
    lastMousePos = scaledMousePos;
    graphInput.scroll = input.scroll;
    graphInput.mouseDown[0] = input.mouseDown[0];
    graphInput.mouseDown[1] = input.mouseDown[1];
    graphInput.mouseDown[2] = input.mouseDown[2];
    // Track clicks
    static bool lastMouseDown[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        graphInput.mouseClicked[i] = input.mouseDown[i] && !lastMouseDown[i];
        graphInput.mouseReleased[i] = !input.mouseDown[i] && lastMouseDown[i];
        lastMouseDown[i] = input.mouseDown[i];
    }
    graphInput.keyCtrl = input.keyCtrl;
    graphInput.keyShift = input.keyShift;
    graphInput.keyAlt = input.keyAlt;

    // Begin overlay rendering
    m_overlay.begin(input.width, input.height);

    // Begin node graph editor
    m_nodeGraph.beginEditor(m_overlay, static_cast<float>(input.width), static_cast<float>(input.height), graphInput);

    // Add nodes for each operator
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);

        m_nodeGraph.beginNode(nodeId);
        m_nodeGraph.setNodeTitle(info.name);

        // Set content callback to render operator preview/thumbnail
        vivid::Operator* op = info.op;  // Capture for lambda
        m_nodeGraph.setNodeContent([op](OverlayCanvas& canvas, float x, float y, float w, float h) {
            if (!op) return;

            vivid::OutputKind kind = op->outputKind();

            // First try operator's custom visualization via VizDrawList
            VizDrawList dl(canvas);
            if (op->drawVisualization(&dl, x, y, x + w, y + h)) {
                return; // Operator drew its own visualization
            }

            // Fallback visualization based on output type
            if (kind == vivid::OutputKind::Texture) {
                // Render texture preview with aspect ratio preservation
                WGPUTextureView view = op->outputView();
                if (view) {
                    // Get texture dimensions from operator if available
                    float srcAspect = 16.0f / 9.0f;  // Default to 16:9
                    if (auto* texOp = dynamic_cast<vivid::effects::TextureOperator*>(op)) {
                        int texW = texOp->outputWidth();
                        int texH = texOp->outputHeight();
                        if (texW > 0 && texH > 0) {
                            srcAspect = static_cast<float>(texW) / static_cast<float>(texH);
                        }
                    }

                    // Preserve aspect ratio - fit image within area
                    float areaAspect = w / h;
                    float drawW, drawH, drawX, drawY;

                    if (srcAspect > areaAspect) {
                        // Image is wider - fit to width
                        drawW = w;
                        drawH = w / srcAspect;
                        drawX = x;
                        drawY = y + (h - drawH) * 0.5f;
                    } else {
                        // Image is taller - fit to height
                        drawH = h;
                        drawW = h * srcAspect;
                        drawX = x + (w - drawW) * 0.5f;
                        drawY = y;
                    }

                    canvas.texturedRect(drawX, drawY, drawW, drawH, view);
                } else {
                    // No texture yet - draw placeholder
                    canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.2f, 1.0f});
                }
            } else if (kind == vivid::OutputKind::Geometry) {
                // Geometry - draw 3D cube icon
                canvas.fillRect(x, y, w, h, {0.12f, 0.2f, 0.28f, 1.0f});
                float cx = x + w * 0.5f;
                float cy = y + h * 0.5f;
                float sz = std::min(w, h) * 0.3f;
                // Simple wireframe cube representation
                glm::vec4 lineColor = {0.4f, 0.7f, 1.0f, 0.8f};
                canvas.strokeRect(cx - sz, cy - sz * 0.6f, sz * 1.6f, sz * 1.2f, 1.5f, lineColor);
            } else if (kind == vivid::OutputKind::Audio) {
                // Audio - draw waveform icon
                canvas.fillRect(x, y, w, h, {0.2f, 0.12f, 0.25f, 1.0f});
                float centerY = y + h * 0.5f;
                glm::vec4 waveColor = {0.7f, 0.5f, 0.9f, 0.9f};
                // Draw simple wave
                float prevX = x + 4;
                float prevY = centerY;
                for (int i = 1; i <= 8; i++) {
                    float px = x + 4 + (w - 8) * i / 8.0f;
                    float amplitude = (i % 2 == 0) ? 0.3f : -0.25f;
                    float py = centerY + amplitude * h * 0.6f;
                    canvas.line(prevX, prevY, px, py, 2.0f, waveColor);
                    prevX = px;
                    prevY = py;
                }
            } else {
                // Other types - generic placeholder
                canvas.fillRect(x, y, w, h, {0.15f, 0.15f, 0.18f, 1.0f});
            }
        });

        // Add input pins for each connected input
        size_t numInputs = info.op->inputCount();
        for (size_t j = 0; j < numInputs; ++j) {
            if (info.op->getInput(static_cast<int>(j))) {
                int pinId = nodeId * 100 + static_cast<int>(j) + 1;
                m_nodeGraph.beginInputAttribute(pinId);
                // Use input name if available, otherwise "in{j}"
                std::string label = info.op->getInputName(static_cast<int>(j));
                if (label.empty()) {
                    label = "in" + std::to_string(j);
                }
                m_nodeGraph.pinLabel(label);
                m_nodeGraph.endInputAttribute();
            }
        }

        // Add output pin
        int outputPinId = nodeId * 100;
        m_nodeGraph.beginOutputAttribute(outputPinId);
        m_nodeGraph.pinLabel("out");
        m_nodeGraph.endOutputAttribute();

        m_nodeGraph.endNode();
    }

    // Add Screen output node (represents the display)
    vivid::Operator* outputOp = ctx.hasChain() ? ctx.chain().getOutput() : nullptr;
    int outputNodeId = -1;
    if (outputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == outputOp) {
                outputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (outputNodeId >= 0) {
            m_nodeGraph.beginNode(SCREEN_NODE_ID);
            m_nodeGraph.setNodeTitle("Screen");
            m_nodeGraph.beginInputAttribute(SCREEN_NODE_ID * 100 + 1);
            m_nodeGraph.pinLabel("display");
            m_nodeGraph.endInputAttribute();
            m_nodeGraph.endNode();
        }
    }

    // Add Speakers output node (represents audio output)
    vivid::Operator* audioOutputOp = ctx.hasChain() ? ctx.chain().getAudioOutput() : nullptr;
    int audioOutputNodeId = -1;
    if (audioOutputOp) {
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op == audioOutputOp) {
                audioOutputNodeId = static_cast<int>(i);
                break;
            }
        }

        if (audioOutputNodeId >= 0) {
            m_nodeGraph.beginNode(SPEAKERS_NODE_ID);
            m_nodeGraph.setNodeTitle("Speakers");
            m_nodeGraph.beginInputAttribute(SPEAKERS_NODE_ID * 100 + 1);
            m_nodeGraph.pinLabel("audio");
            m_nodeGraph.endInputAttribute();
            m_nodeGraph.endNode();
        }
    }

    // Do initial layout if not yet done
    static bool layoutDone = false;
    if (!layoutDone && !operators.empty()) {
        // Calculate depth for each operator (distance from sources)
        std::vector<int> depths(operators.size(), 0);
        std::unordered_map<vivid::Operator*, int> opToIdx;
        for (size_t i = 0; i < operators.size(); ++i) {
            if (operators[i].op) {
                opToIdx[operators[i].op] = static_cast<int>(i);
            }
        }

        // Build reverse map: for each operator, which operators use it as input
        std::vector<std::vector<int>> consumers(operators.size());
        for (size_t i = 0; i < operators.size(); ++i) {
            vivid::Operator* op = operators[i].op;
            if (!op) continue;

            for (size_t j = 0; j < op->inputCount(); ++j) {
                vivid::Operator* inputOp = op->getInput(static_cast<int>(j));
                if (inputOp && opToIdx.count(inputOp)) {
                    int inputIdx = opToIdx[inputOp];
                    consumers[inputIdx].push_back(static_cast<int>(i));
                }
            }
        }

        // First pass: Calculate forward depth (from sources)
        for (size_t i = 0; i < operators.size(); ++i) {
            vivid::Operator* op = operators[i].op;
            if (!op) continue;

            int maxInputDepth = -1;
            for (size_t j = 0; j < op->inputCount(); ++j) {
                vivid::Operator* inputOp = op->getInput(static_cast<int>(j));
                if (inputOp && opToIdx.count(inputOp)) {
                    int inputIdx = opToIdx[inputOp];
                    maxInputDepth = std::max(maxInputDepth, depths[inputIdx]);
                }
            }
            depths[i] = maxInputDepth + 1;
        }

        // Second pass: Reposition source nodes (depth 0) with consumers
        // Place them one column before their earliest consumer
        // This positions lights near the renderer and materials near their meshes
        for (size_t i = 0; i < operators.size(); ++i) {
            if (depths[i] == 0 && !consumers[i].empty()) {
                // Find minimum consumer depth
                int minConsumerDepth = INT_MAX;
                for (int consumerIdx : consumers[i]) {
                    minConsumerDepth = std::min(minConsumerDepth, depths[consumerIdx]);
                }
                // Position one column before the consumer (but at least 0)
                if (minConsumerDepth > 1) {
                    depths[i] = minConsumerDepth - 1;
                }
            }
        }

        // Group operators by depth (column)
        int maxDepth = 0;
        for (int d : depths) maxDepth = std::max(maxDepth, d);

        std::vector<std::vector<int>> columns(maxDepth + 1);
        for (size_t i = 0; i < operators.size(); ++i) {
            columns[depths[i]].push_back(static_cast<int>(i));
        }

        // Position nodes in columns (left-to-right data flow)
        // Use actual node sizes for spacing
        const float xPadding = 80.0f;
        const float yPadding = 30.0f;
        const float startX = 100.0f;
        const float startY = 100.0f;

        // Find max width for each column
        std::vector<float> columnWidths(columns.size(), 200.0f);
        for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
            for (int nodeId : columns[col]) {
                glm::vec2 size = m_nodeGraph.getNodeSize(nodeId);
                columnWidths[col] = std::max(columnWidths[col], size.x);
            }
        }

        // Calculate column X positions
        std::vector<float> columnX(columns.size());
        float currentX = startX;
        for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
            columnX[col] = currentX;
            currentX += columnWidths[col] + xPadding;
        }

        // Position nodes using actual heights
        for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
            float y = startY;
            for (int nodeId : columns[col]) {
                m_nodeGraph.setNodePosition(nodeId, glm::vec2(columnX[col], y));
                glm::vec2 size = m_nodeGraph.getNodeSize(nodeId);
                y += size.y + yPadding;
            }
        }

        // Position Screen and Speakers nodes at the end (rightmost column)
        float outputX = currentX;  // After the last column
        float outputY = startY;
        if (outputNodeId >= 0) {
            m_nodeGraph.setNodePosition(SCREEN_NODE_ID, glm::vec2(outputX, outputY));
            glm::vec2 screenSize = m_nodeGraph.getNodeSize(SCREEN_NODE_ID);
            outputY += screenSize.y + yPadding;
        }
        if (audioOutputNodeId >= 0) {
            m_nodeGraph.setNodePosition(SPEAKERS_NODE_ID, glm::vec2(outputX, outputY));
        }

        // Zoom to fit all nodes with some padding
        m_nodeGraph.zoomToFit();
        layoutDone = true;
    }

    // Add links based on operator connections
    int linkId = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        const vivid::OperatorInfo& info = operators[i];
        if (!info.op) continue;

        int nodeId = static_cast<int>(i);
        size_t numInputs = info.op->inputCount();

        for (size_t j = 0; j < numInputs; ++j) {
            vivid::Operator* inputOp = info.op->getInput(static_cast<int>(j));
            if (!inputOp) continue;

            // Find the node ID for the input operator
            for (size_t k = 0; k < operators.size(); ++k) {
                if (operators[k].op == inputOp) {
                    int srcNodeId = static_cast<int>(k);
                    int srcOutputPinId = srcNodeId * 100;
                    int dstInputPinId = nodeId * 100 + static_cast<int>(j) + 1;
                    m_nodeGraph.link(linkId++, srcOutputPinId, dstInputPinId);
                    break;
                }
            }
        }
    }

    // Link from output operator to Screen node
    if (outputNodeId >= 0) {
        m_nodeGraph.link(linkId++, outputNodeId * 100, SCREEN_NODE_ID * 100 + 1);
    }

    // Link from audio output operator to Speakers node
    if (audioOutputNodeId >= 0) {
        m_nodeGraph.link(linkId++, audioOutputNodeId * 100, SPEAKERS_NODE_ID * 100 + 1);
    }

    // End node graph editor
    m_nodeGraph.endEditor();

    // Render status bar (in screen space, not node graph space)
    m_overlay.resetTransform();
    renderStatusBar(input, ctx);

    // Render tooltip for hovered node
    int hoveredNodeId = -1;
    if (m_nodeGraph.isNodeHovered(&hoveredNodeId) && hoveredNodeId >= 0 &&
        hoveredNodeId != SCREEN_NODE_ID && hoveredNodeId != SPEAKERS_NODE_ID) {
        // Find the operator for this node
        if (static_cast<size_t>(hoveredNodeId) < operators.size()) {
            const vivid::OperatorInfo& info = operators[hoveredNodeId];
            if (info.op) {
                renderTooltip(input, info);
            }
        }
    }

    // Render debug values panel (bottom-left corner)
    renderDebugPanelOverlay(input, ctx);

    // Handle keyboard shortcuts (using new key input system)
    using vivid::Key;

    // S key - solo selected node
    if (input.isKeyPressed(Key::S)) {
        int selectedNodeId = m_nodeGraph.getSelectedNode();
        if (selectedNodeId >= 0 && selectedNodeId != SCREEN_NODE_ID && selectedNodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(selectedNodeId) < operators.size()) {
                const vivid::OperatorInfo& info = operators[selectedNodeId];
                if (info.op) {
                    enterSoloMode(info.op, info.name);
                }
            }
        }
    }

    // Escape key - exit solo mode
    if (input.isKeyPressed(Key::Escape) && m_inSoloMode) {
        exitSoloMode();
    }

    // B key - toggle bypass on selected node
    if (input.isKeyPressed(Key::B)) {
        int selectedNodeId = m_nodeGraph.getSelectedNode();
        if (selectedNodeId >= 0 && selectedNodeId != SCREEN_NODE_ID && selectedNodeId != SPEAKERS_NODE_ID) {
            if (static_cast<size_t>(selectedNodeId) < operators.size()) {
                const vivid::OperatorInfo& info = operators[selectedNodeId];
                if (info.op) {
                    info.op->setBypassed(!info.op->isBypassed());
                }
            }
        }
    }

    // Render solo mode overlay (if active)
    if (m_inSoloMode && m_soloOperator) {
        // Set the output texture to the solo operator's output
        vivid::OutputKind kind = m_soloOperator->outputKind();
        if (kind == vivid::OutputKind::Texture) {
            WGPUTextureView view = m_soloOperator->outputView();
            if (view) {
                ctx.setOutputTexture(view);
            }
        }

        // Draw solo mode indicator (top-left corner, using topmost layer)
        float lineH = m_overlay.fontLineHeight(0);
        float ascent = m_overlay.fontAscent(0);
        if (lineH <= 0) lineH = 22.0f;
        if (ascent <= 0) ascent = 16.0f;

        const float padding = 10.0f;
        std::string soloText = "SOLO: " + m_soloOperatorName;
        std::string escText = "(press ESC to exit)";
        float soloWidth = m_overlay.measureText(soloText, 0);
        float escWidth = m_overlay.measureText(escText, 0);
        float boxWidth = std::max(soloWidth, escWidth) + padding * 2;
        float boxHeight = lineH * 2 + padding * 2;

        glm::vec4 bgColor = {0.15f, 0.12f, 0.05f, 0.9f};
        glm::vec4 borderColor = {0.8f, 0.6f, 0.2f, 1.0f};
        glm::vec4 soloColor = {1.0f, 0.9f, 0.4f, 1.0f};
        glm::vec4 dimColor = {0.6f, 0.6f, 0.7f, 1.0f};

        m_overlay.fillRoundedRectTopmost(padding, padding, boxWidth, boxHeight, 4.0f, bgColor);
        m_overlay.strokeRoundedRectTopmost(padding, padding, boxWidth, boxHeight, 4.0f, 1.0f, borderColor);
        m_overlay.textTopmost(soloText, padding * 2, padding + ascent, soloColor);
        m_overlay.textTopmost(escText, padding * 2, padding + lineH + ascent, dimColor);
    }

    // Render the overlay
    m_overlay.render(pass);
}

void ChainVisualizer::renderStatusBar(const FrameInput& input, vivid::Context& ctx) {
    // Use mono font metrics for bar height calculation
    const int monoFont = 2;
    float lineH = m_overlay.fontLineHeight(monoFont);
    float ascent = m_overlay.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;  // Fallback
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 6.0f;
    const float barHeight = lineH + padding * 2;
    float x = padding;
    // Position text so it's vertically centered (baseline = top padding + ascent)
    float y = padding + ascent;

    // Smoothed values for FPS and frame time (exponential moving average)
    static float smoothedFps = 60.0f;
    static float smoothedMs = 16.67f;
    const float smoothing = 0.05f;  // Lower = smoother (0.05 = ~20 frame average)

    float instantFps = input.dt > 0 ? 1.0f / input.dt : smoothedFps;
    float instantMs = input.dt * 1000.0f;
    smoothedFps = smoothedFps + smoothing * (instantFps - smoothedFps);
    smoothedMs = smoothedMs + smoothing * (instantMs - smoothedMs);

    // Semi-transparent background
    m_overlay.fillRect(0, 0, static_cast<float>(input.width), barHeight,
                       {0.1f, 0.1f, 0.12f, 0.85f});

    // Colors
    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 dimColor = {0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 greenColor = {0.4f, 0.9f, 0.4f, 1.0f};
    glm::vec4 yellowColor = {0.9f, 0.9f, 0.4f, 1.0f};
    glm::vec4 redColor = {0.9f, 0.4f, 0.4f, 1.0f};

    char buf[64];
    // Separator line inset from top/bottom
    const float sepInset = padding;

    // FPS (fixed width: 5 chars for number + " FPS")
    snprintf(buf, sizeof(buf), "%5.1f FPS", smoothedFps);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Frame time (fixed width: 6 chars for number + "ms")
    snprintf(buf, sizeof(buf), "%6.2fms", smoothedMs);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Resolution (fixed width for common resolutions)
    snprintf(buf, sizeof(buf), "%4dx%-4d", input.width, input.height);
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Operator count
    const auto& operators = ctx.registeredOperators();
    snprintf(buf, sizeof(buf), "%2zu ops", operators.size());
    m_overlay.text(buf, x, y, textColor, monoFont);
    x += m_overlay.measureText(buf, monoFont) + padding * 2;

    // Separator
    m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Memory usage (color-coded)
    size_t memBytes = getProcessMemoryUsage();
    std::string memStr = formatMemory(memBytes);
    glm::vec4 memColor;
    if (memBytes < 500 * 1024 * 1024) {
        memColor = greenColor;
    } else if (memBytes < 2ULL * 1024 * 1024 * 1024) {
        memColor = yellowColor;
    } else {
        memColor = redColor;
    }
    m_overlay.text("MEM:", x, y, dimColor);
    x += m_overlay.measureText("MEM:") + 4;
    m_overlay.text(memStr, x, y, memColor, monoFont);
    x += m_overlay.measureText(memStr, monoFont) + padding * 2;

    // Audio stats (if audio active)
    AudioGraph* audioGraph = ctx.chain().audioGraph();
    if (audioGraph && !audioGraph->empty()) {
        // Separator
        m_overlay.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        // DSP Load
        float dspLoad = audioGraph->dspLoad();
        glm::vec4 dspColor;
        if (dspLoad < 0.5f) {
            dspColor = greenColor;
        } else if (dspLoad < 0.8f) {
            dspColor = yellowColor;
        } else {
            dspColor = redColor;
        }
        m_overlay.text("DSP:", x, y, dimColor);
        x += m_overlay.measureText("DSP:") + 4;
        snprintf(buf, sizeof(buf), "%3.0f%%", dspLoad * 100.0f);
        m_overlay.text(buf, x, y, dspColor, monoFont);
        x += m_overlay.measureText(buf, monoFont) + padding * 2;

        // Dropped events (if any)
        uint64_t dropped = audioGraph->droppedEventCount();
        if (dropped > 0) {
            snprintf(buf, sizeof(buf), "%llu dropped", dropped);
            m_overlay.text(buf, x, y, redColor, monoFont);
            x += m_overlay.measureText(buf, monoFont) + padding * 2;
        }
    }

    // Recording indicator (right side)
    if (m_exporter.isRecording()) {
        snprintf(buf, sizeof(buf), "REC %d frames (%.1fs)",
                 m_exporter.frameCount(), m_exporter.duration());
        float recWidth = m_overlay.measureText(buf, monoFont) + 20;
        float recX = input.width - recWidth - padding;

        // Red dot (vertically centered)
        m_overlay.fillCircle(recX + 6, barHeight * 0.5f, 4, redColor);
        m_overlay.text(buf, recX + 16, y, redColor, monoFont);
    }
}

void ChainVisualizer::renderTooltip(const FrameInput& input, const vivid::OperatorInfo& info) {
    if (!info.op) return;

    // Use font 0 (Inter Regular) metrics for tooltip
    float lineH = m_overlay.fontLineHeight(0);
    float ascent = m_overlay.fontAscent(0);
    if (lineH <= 0) lineH = 22.0f;  // Fallback
    if (ascent <= 0) ascent = 16.0f;

    const float padding = 8.0f;
    const float lineHeight = lineH;
    float tooltipWidth = 200.0f;

    // Colors - fully opaque background for readability
    glm::vec4 bgColor = {0.12f, 0.12f, 0.14f, 1.0f};
    glm::vec4 borderColor = {0.4f, 0.4f, 0.45f, 1.0f};
    glm::vec4 titleColor = {0.5f, 0.8f, 1.0f, 1.0f};
    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 dimColor = {0.65f, 0.65f, 0.7f, 1.0f};
    glm::vec4 orangeColor = {1.0f, 0.6f, 0.3f, 1.0f};

    // Build tooltip content
    std::vector<std::pair<std::string, glm::vec4>> lines;

    // Operator type name
    lines.push_back({info.op->name(), titleColor});

    // Registered name if different
    if (info.name != info.op->name()) {
        lines.push_back({"(" + info.name + ")", dimColor});
    }

    // Output type
    vivid::OutputKind kind = info.op->outputKind();
    const char* kindStr = "Unknown";
    switch (kind) {
        case vivid::OutputKind::Texture: kindStr = "Output: Texture"; break;
        case vivid::OutputKind::Geometry: kindStr = "Output: Geometry"; break;
        case vivid::OutputKind::Audio: kindStr = "Output: Audio"; break;
        case vivid::OutputKind::AudioValue: kindStr = "Output: Audio Value"; break;
        case vivid::OutputKind::Value: kindStr = "Output: Value"; break;
        case vivid::OutputKind::ValueArray: kindStr = "Output: Value Array"; break;
        case vivid::OutputKind::Camera: kindStr = "Output: Camera"; break;
        case vivid::OutputKind::Light: kindStr = "Output: Light"; break;
    }
    lines.push_back({kindStr, textColor});

    // Resource info for textures
    if (kind == vivid::OutputKind::Texture) {
        WGPUTexture tex = info.op->outputTexture();
        if (tex) {
            uint32_t w = wgpuTextureGetWidth(tex);
            uint32_t h = wgpuTextureGetHeight(tex);
            size_t memBytes = w * h * 8;  // RGBA16Float = 8 bytes per pixel
            char buf[64];
            snprintf(buf, sizeof(buf), "Size: %ux%u", w, h);
            lines.push_back({buf, textColor});
            snprintf(buf, sizeof(buf), "Memory: ~%.1f MB", memBytes / (1024.0f * 1024.0f));
            lines.push_back({buf, textColor});
        } else {
            lines.push_back({"No texture", dimColor});
        }
    }

    // Bypass status
    if (info.op->isBypassed()) {
        lines.push_back({"BYPASSED", orangeColor});
    }

    // Calculate tooltip size
    float maxWidth = 0;
    for (const auto& line : lines) {
        float w = m_overlay.measureText(line.first);
        maxWidth = std::max(maxWidth, w);
    }
    tooltipWidth = maxWidth + padding * 2;
    float tooltipHeight = lines.size() * lineHeight + padding * 2;

    // Position tooltip near mouse (offset to not obscure cursor)
    float mouseX = input.mousePos.x * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float mouseY = input.mousePos.y * (input.contentScale > 0 ? input.contentScale : 1.0f);
    float tooltipX = mouseX + 15;
    float tooltipY = mouseY + 15;

    // Keep on screen
    if (tooltipX + tooltipWidth > input.width) {
        tooltipX = mouseX - tooltipWidth - 10;
    }
    if (tooltipY + tooltipHeight > input.height) {
        tooltipY = mouseY - tooltipHeight - 10;
    }

    // Draw background (use topmost layer so tooltips appear above thumbnails)
    m_overlay.fillRoundedRectTopmost(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 4.0f, bgColor);
    m_overlay.strokeRoundedRectTopmost(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 4.0f, 1.0f, borderColor);

    // Draw text lines (position at baseline = top + ascent)
    float textY = tooltipY + padding + ascent;
    for (const auto& line : lines) {
        m_overlay.textTopmost(line.first, tooltipX + padding, textY, line.second);
        textY += lineHeight;
    }
}

void ChainVisualizer::renderDebugPanelOverlay(const FrameInput& input, vivid::Context& ctx) {
    const auto& debugValues = ctx.debugValues();
    if (debugValues.empty()) return;

    // Use font metrics for layout
    const int monoFont = 2;
    float lineH = m_overlay.fontLineHeight(monoFont);
    float ascent = m_overlay.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;  // Fallback
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 8.0f;
    const float lineHeight = lineH + 4;  // Add some spacing between rows
    const float nameWidth = 90.0f;
    const float sparklineWidth = 100.0f;
    const float sparklineHeight = lineH - 2;
    const float valueWidth = 65.0f;
    const float panelWidth = nameWidth + sparklineWidth + valueWidth + padding * 4;
    const float panelHeight = debugValues.size() * lineHeight + padding * 2;

    // Position in bottom-left corner
    float panelX = padding;
    float panelY = input.height - panelHeight - padding;

    // Colors
    glm::vec4 bgColor = {0.12f, 0.12f, 0.15f, 0.9f};
    glm::vec4 borderColor = {0.3f, 0.3f, 0.35f, 1.0f};
    glm::vec4 textColor = {0.85f, 0.85f, 0.85f, 1.0f};
    glm::vec4 dimColor = {0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 graphColor = {0.4f, 0.7f, 0.9f, 1.0f};
    glm::vec4 graphBgColor = {0.08f, 0.08f, 0.1f, 1.0f};

    // Draw panel background
    m_overlay.fillRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f, bgColor);
    m_overlay.strokeRoundedRect(panelX, panelY, panelWidth, panelHeight, 4.0f, 1.0f, borderColor);

    float y = panelY + padding;
    for (const auto& [name, dv] : debugValues) {
        float x = panelX + padding;

        glm::vec4 color = dv.updatedThisFrame ? textColor : dimColor;

        // Name (baseline positioned with ascent)
        m_overlay.text(name, x, y + ascent, color);
        x += nameWidth;

        // Sparkline background (vertically centered in line)
        float sparkY = y + (lineHeight - sparklineHeight) * 0.5f;
        m_overlay.fillRect(x, sparkY, sparklineWidth, sparklineHeight, graphBgColor);

        // Sparkline graph
        if (!dv.history.empty()) {
            std::vector<float> historyVec(dv.history.begin(), dv.history.end());

            // Find min/max for scaling
            float minVal = *std::min_element(historyVec.begin(), historyVec.end());
            float maxVal = *std::max_element(historyVec.begin(), historyVec.end());

            // Ensure some range even for constant values
            if (maxVal - minVal < 0.001f) {
                minVal -= 0.5f;
                maxVal += 0.5f;
            }

            float range = maxVal - minVal;
            float graphX = x;
            float graphBottom = sparkY + sparklineHeight;

            // Draw line segments
            for (size_t i = 1; i < historyVec.size(); i++) {
                float x1 = graphX + (i - 1) * sparklineWidth / (historyVec.size() - 1);
                float x2 = graphX + i * sparklineWidth / (historyVec.size() - 1);
                float y1 = graphBottom - ((historyVec[i-1] - minVal) / range) * sparklineHeight;
                float y2 = graphBottom - ((historyVec[i] - minVal) / range) * sparklineHeight;
                m_overlay.line(x1, y1, x2, y2, 1.5f, graphColor);
            }
        }
        x += sparklineWidth + padding;

        // Current value (use mono font for alignment)
        char buf[32];
        snprintf(buf, sizeof(buf), "%7.3f", dv.current);
        m_overlay.text(buf, x, y + ascent, color, monoFont);

        y += lineHeight;
    }
}

} // namespace vivid
