// Status Bar Panel Implementation
// Extracted from ChainVisualizer - shows FPS, memory, recording controls
//
// Renders at the top of the screen with:
// - FPS counter
// - Frame time
// - Resolution
// - Operator count
// - Memory usage
// - Pending changes indicator
// - MCP warning
// - Audio stats
// - Grid toggle
// - Record/Snapshot buttons

#include <vivid/devtools/panels/status_bar_panel.h>
#include <vivid/video_exporter.h>
#include <vivid/context.h>
#include <vivid/audio_graph.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <iostream>

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

namespace vivid {

// Get process memory usage in bytes
static size_t getProcessMemoryUsage() {
#if defined(__APPLE__)
    task_vm_info_data_t vmInfo;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count) == KERN_SUCCESS) {
        return vmInfo.phys_footprint;
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

// Format bytes as human-readable string
static std::string formatMemory(size_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return buf;
}

struct StatusBarPanel::Impl {
    // Callbacks
    SnapshotCallback snapshotCallback;
    RecordCallback recordCallback;
    GridOpacityCallback gridOpacityCallback;
    PanelToggleCallback panelToggleCallback;
    PanelDragCallback panelDragCallback;
    PanelDockCallback panelDockCallback;

    // State
    size_t pendingChangeCount = 0;
    std::string mcpWarning;
    VideoExporter* exporter = nullptr;
    Context* ctx = nullptr;

    // Button hit regions
    struct ButtonRect { float x, y, w, h; bool valid = false; };
    ButtonRect recordButton;
    ButtonRect stopButton;
    ButtonRect snapshotButton;

    // Panel toggle buttons (left side of status bar)
    struct PanelToggle {
        std::string id;       // "terminal", "console", etc.
        std::string label;    // "T", "C", "E", "N"
        std::string tooltip;  // "Terminal (⌘1)"
        DockMode dockMode = DockMode::Hidden;
        ButtonRect hitRect;
    };
    std::vector<PanelToggle> panelToggles;
    int hoveredToggle = -1;

    // Drag state for drag-from-button
    bool draggingToggle = false;
    int dragToggleIndex = -1;
    glm::vec2 dragStartPos = {0, 0};
    static constexpr float kDragThreshold = 5.0f;

    // Context menu state
    bool contextMenuOpen = false;
    int contextMenuToggleIndex = -1;
    ButtonRect contextMenuRect;
    ButtonRect menuItemDockLeft;
    ButtonRect menuItemDockRight;
    ButtonRect menuItemDockTop;
    ButtonRect menuItemDockBottom;
    ButtonRect menuItemFloat;
    ButtonRect menuItemHide;

    // Grid opacity slider
    float gridOpacity = 0.0f;
    ButtonRect gridSliderRect;
    bool draggingGridSlider = false;

    // Codec dropdown
    bool codecDropdownOpen = false;
    ButtonRect codecH264;
    ButtonRect codecH265;
    ButtonRect codecProRes;

    // Smoothed values for FPS
    float smoothedFps = 60.0f;
    float smoothedMs = 16.67f;

    bool isMouseInRect(const ButtonRect& r, glm::vec2 mousePos) const {
        return r.valid && mousePos.x >= r.x && mousePos.x < r.x + r.w &&
               mousePos.y >= r.y && mousePos.y < r.y + r.h;
    }

    void initPanelToggles() {
        panelToggles = {
            {"terminal",    "T", "Terminal (\u23181)", DockMode::Hidden, {}},
            {"console",     "C", "Console (\u23182)", DockMode::Hidden, {}},
            {"editor",      "E", "Editor (\u23183)", DockMode::Hidden, {}},
            {"nodegraph",   "N", "Visualizer (\u23184)", DockMode::Hidden, {}},
            {"filebrowser", "F", "Files (\u23185)", DockMode::Hidden, {}}
        };
    }
};

StatusBarPanel::StatusBarPanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "statusbar";
    m_config.title = "";
    m_config.bounds = {0, 0, 0, 32};  // Full width, fixed height
    m_config.dockSide = DockSide::Top;
    m_config.visible = true;
    m_config.resizable = false;
    m_config.draggable = false;
    m_impl->initPanelToggles();
}

StatusBarPanel::~StatusBarPanel() = default;

bool StatusBarPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl->ctx = &ctx;
    return true;
}

void StatusBarPanel::shutdown() {
    m_impl.reset();
}

void StatusBarPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                            const FrameInput& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;
    // Style parameter is available for use throughout this function

    // Compute logical screen dimensions for HiDPI displays
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenWidth = static_cast<float>(input.width) / scale;

    // Use bounds height for the bar (from layout), fall back to calculated if bounds not set
    const int monoFont = 2;
    float lineH = canvas.fontLineHeight(monoFont);
    float ascent = canvas.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 6.0f;
    // Use bounds height if provided, otherwise calculate from font
    const float barHeight = (bounds.w > 0) ? bounds.w : (lineH + padding * 2);
    float x = padding;
    float y = padding + ascent;

    // Update smoothed FPS (exponential moving average)
    const float smoothing = 0.05f;
    float instantFps = input.dt > 0 ? 1.0f / input.dt : m_impl->smoothedFps;
    float instantMs = input.dt * 1000.0f;
    m_impl->smoothedFps = m_impl->smoothedFps + smoothing * (instantFps - m_impl->smoothedFps);
    m_impl->smoothedMs = m_impl->smoothedMs + smoothing * (instantMs - m_impl->smoothedMs);

    // Semi-transparent background - fill the entire allocated bounds
    glm::vec4 barBg = style.headerBg;
    barBg.a = 0.95f;
    canvas.fillRect(bounds.x, bounds.y, screenWidth, barHeight, barBg);

    // Colors from style
    glm::vec4 textColor = style.textPrimary;
    glm::vec4 dimColor = style.textDim;
    glm::vec4 greenColor = style.success;
    glm::vec4 yellowColor = style.warning;
    glm::vec4 redColor = style.error;

    char buf[64];
    const float sepInset = padding;

    // Panel toggle buttons (left side)
    glm::vec2 mousePos = input.mousePos;
    m_impl->hoveredToggle = -1;

    const float btnW = 24.0f;
    const float btnH = lineH + 4.0f;
    const float btnY = (barHeight - btnH) * 0.5f;
    const float btnGap = 4.0f;

    for (size_t i = 0; i < m_impl->panelToggles.size(); i++) {
        auto& toggle = m_impl->panelToggles[i];

        // Determine colors based on state
        bool isHovered = (mousePos.x >= x && mousePos.x < x + btnW &&
                          mousePos.y >= btnY && mousePos.y < btnY + btnH);
        if (isHovered) {
            m_impl->hoveredToggle = static_cast<int>(i);
        }

        // Three-state rendering:
        // - Docked: Filled with accent color
        // - Floating: Filled with dim color (semi-transparent)
        // - Hidden: Hollow (border only)
        glm::vec4 bgColor;
        glm::vec4 labelColor;
        glm::vec4 borderColor = style.buttonBorder;
        bool drawBorder = false;

        switch (toggle.dockMode) {
            case DockMode::Docked:
                bgColor = isHovered ? style.accentActive : style.accent;
                labelColor = glm::vec4(1, 1, 1, 1);
                break;
            case DockMode::Floating:
                bgColor = isHovered ? style.buttonHover : glm::vec4(0.3f, 0.3f, 0.35f, 0.8f);
                labelColor = textColor;
                borderColor = style.accent;
                drawBorder = true;
                break;
            case DockMode::Hidden:
            default:
                bgColor = isHovered ? style.buttonHover : glm::vec4(0, 0, 0, 0);
                labelColor = dimColor;
                drawBorder = true;
                break;
        }

        // Draw button background
        bool hasBackground = toggle.dockMode != DockMode::Hidden || isHovered;
        if (hasBackground) {
            canvas.fillRoundedRect(x, btnY, btnW, btnH, style.buttonCornerRadius(), bgColor);
        }

        // Draw border for floating/hidden states
        if (drawBorder) {
            canvas.strokeRoundedRect(x, btnY, btnW, btnH, style.buttonCornerRadius(), 1, borderColor);
        }

        // Draw label centered
        float labelW = canvas.measureText(toggle.label, monoFont);
        float labelX = x + (btnW - labelW) * 0.5f;
        canvas.text(toggle.label, labelX, btnY + ascent + 2, labelColor, monoFont);

        // Store hit rect
        toggle.hitRect = {x, btnY, btnW, btnH, true};
        x += btnW + btnGap;
    }

    // Render tooltip for hovered toggle
    if (m_impl->hoveredToggle >= 0 && m_impl->hoveredToggle < static_cast<int>(m_impl->panelToggles.size())) {
        canvas.setLayer(UILayer::Tooltips);
        const auto& toggle = m_impl->panelToggles[m_impl->hoveredToggle];
        float tooltipW = canvas.measureText(toggle.tooltip, monoFont) + padding * 2;
        float tooltipH = lineH + padding;
        float tooltipX = toggle.hitRect.x;
        float tooltipY = barHeight + 4;

        // Ensure tooltip doesn't go off screen
        if (tooltipX + tooltipW > screenWidth - padding) {
            tooltipX = screenWidth - tooltipW - padding;
        }

        glm::vec4 tooltipBg = style.panelBg;
        tooltipBg.a = 0.95f;
        canvas.fillRoundedRect(tooltipX, tooltipY, tooltipW, tooltipH, style.buttonCornerRadius(), tooltipBg);
        canvas.strokeRoundedRect(tooltipX, tooltipY, tooltipW, tooltipH, style.buttonCornerRadius(), 1, style.panelBorder);
        canvas.text(toggle.tooltip, tooltipX + padding, tooltipY + ascent + padding * 0.5f, textColor, monoFont);
        canvas.setLayer(UILayer::Panels);
    }

    // Separator after panel toggles
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Grid opacity slider
    {
        const char* gridLabel = "Grid";
        float labelW = canvas.measureText(gridLabel, monoFont);
        canvas.text(gridLabel, x, y, dimColor, monoFont);
        x += labelW + 6;

        // Slider dimensions
        const float sliderW = 60.0f;
        const float sliderH = btnH - 4.0f;
        const float sliderY = (barHeight - sliderH) * 0.5f;
        const float sliderRadius = style.sliderCornerRadius();

        // Slider track background
        glm::vec4 trackColor = style.sliderBg;
        canvas.fillRoundedRect(x, sliderY, sliderW, sliderH, sliderRadius, trackColor);

        // Filled portion based on opacity
        float fillW = sliderW * m_impl->gridOpacity;
        if (fillW > 0) {
            // Clip the fill to the slider bounds with rounded corners
            glm::vec4 fillColor = style.accent;
            fillColor.a = 0.7f;
            if (fillW >= sliderW - sliderRadius) {
                // Full or nearly full - draw full rounded rect
                canvas.fillRoundedRect(x, sliderY, fillW, sliderH, sliderRadius, fillColor);
            } else {
                // Partial fill - draw with left rounded corners only
                canvas.fillRoundedRect(x, sliderY, std::max(fillW, sliderRadius * 2), sliderH, sliderRadius, fillColor);
                // Clip off the right rounded edge if needed
                if (fillW < sliderRadius * 2) {
                    canvas.fillRect(x + fillW, sliderY, sliderRadius * 2 - fillW, sliderH, trackColor);
                }
            }
        }

        // Border
        canvas.strokeRoundedRect(x, sliderY, sliderW, sliderH, sliderRadius, 1, style.buttonBorder);

        // Store hit rect for interaction
        m_impl->gridSliderRect = {x, sliderY, sliderW, sliderH, true};
        x += sliderW + padding;
    }

    // Separator after grid slider
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // FPS
    snprintf(buf, sizeof(buf), "%5.1f FPS", m_impl->smoothedFps);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    // Separator
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Frame time
    snprintf(buf, sizeof(buf), "%6.2fms", m_impl->smoothedMs);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    // Separator
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Resolution
    snprintf(buf, sizeof(buf), "%4dx%-4d", input.width, input.height);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    // Separator
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Operator count
    if (m_impl->ctx) {
        const auto& operators = m_impl->ctx->registeredOperators();
        snprintf(buf, sizeof(buf), "%2zu ops", operators.size());
        canvas.text(buf, x, y, textColor, monoFont);
        x += canvas.measureText(buf, monoFont) + padding * 2;
    }

    // Separator
    canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
    x += padding * 2;

    // Memory usage
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
    canvas.text("MEM:", x, y, dimColor);
    x += canvas.measureText("MEM:") + 4;
    canvas.text(memStr, x, y, memColor, monoFont);
    x += canvas.measureText(memStr, monoFont) + padding * 2;

    // Pending changes indicator
    if (m_impl->pendingChangeCount > 0) {
        canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        snprintf(buf, sizeof(buf), "Pending: %zu", m_impl->pendingChangeCount);
        canvas.text(buf, x, y, yellowColor, monoFont);
        x += canvas.measureText(buf, monoFont) + padding * 2;
    }

    // MCP warning
    if (!m_impl->mcpWarning.empty()) {
        canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        canvas.text(m_impl->mcpWarning, x, y, redColor, monoFont);
        x += canvas.measureText(m_impl->mcpWarning, monoFont) + padding * 2;
    }

    // Audio stats (if audio active)
    if (m_impl->ctx && m_impl->ctx->hasChain()) {
        AudioGraph* audioGraph = m_impl->ctx->chain().audioGraph();
        if (audioGraph && !audioGraph->empty()) {
            canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
            x += padding * 2;

            float dspLoad = audioGraph->dspLoad();
            glm::vec4 dspColor;
            if (dspLoad < 0.5f) {
                dspColor = greenColor;
            } else if (dspLoad < 0.8f) {
                dspColor = yellowColor;
            } else {
                dspColor = redColor;
            }
            canvas.text("DSP:", x, y, dimColor);
            x += canvas.measureText("DSP:") + 4;
            snprintf(buf, sizeof(buf), "%3.0f%%", dspLoad * 100.0f);
            canvas.text(buf, x, y, dspColor, monoFont);
            x += canvas.measureText(buf, monoFont) + padding * 2;

            uint64_t dropped = audioGraph->droppedEventCount();
            if (dropped > 0) {
                snprintf(buf, sizeof(buf), "%llu dropped", (unsigned long long)dropped);
                canvas.text(buf, x, y, redColor, monoFont);
                x += canvas.measureText(buf, monoFont) + padding * 2;
            }
        }
    }

    // Recording controls (right side)
    glm::vec4 buttonBg = style.buttonBg;
    glm::vec4 buttonHover = style.buttonHover;
    glm::vec4 buttonBorder = style.buttonBorder;
    const float buttonPadX = 8.0f;
    const float buttonPadY = 4.0f;
    const float buttonSpacing = 6.0f;

    m_impl->recordButton.valid = false;
    m_impl->stopButton.valid = false;
    m_impl->snapshotButton.valid = false;

    if (m_impl->exporter && m_impl->exporter->isRecording()) {
        // Recording active
        snprintf(buf, sizeof(buf), "REC %d frames (%.1fs)",
                 m_impl->exporter->frameCount(), m_impl->exporter->duration());
        float recTextWidth = canvas.measureText(buf, monoFont);

        const char* stopText = "Stop";
        float stopTextWidth = canvas.measureText(stopText, monoFont);
        float stopBtnW = stopTextWidth + buttonPadX * 2;
        float stopBtnH = lineH + buttonPadY * 2;
        float stopBtnX = screenWidth - stopBtnW - padding;
        float stopBtnY = (barHeight - stopBtnH) * 0.5f;

        m_impl->stopButton = {stopBtnX, stopBtnY, stopBtnW, stopBtnH, true};
        canvas.fillRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, style.buttonCornerRadius(), buttonBg);
        canvas.strokeRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, style.buttonCornerRadius(), 1, redColor);
        canvas.text(stopText, stopBtnX + buttonPadX, stopBtnY + buttonPadY + ascent, redColor, monoFont);

        float recX = stopBtnX - recTextWidth - 24 - buttonSpacing;
        canvas.fillCircle(recX + 6, barHeight * 0.5f, 4, redColor);
        canvas.text(buf, recX + 16, y, redColor, monoFont);
    } else {
        // Not recording
        float rightX = screenWidth - padding;

        // Snapshot button
        const char* snapText = "Snapshot";
        float snapTextWidth = canvas.measureText(snapText, monoFont);
        float snapBtnW = snapTextWidth + buttonPadX * 2;
        float snapBtnH = lineH + buttonPadY * 2;
        float snapBtnX = rightX - snapBtnW;
        float snapBtnY = (barHeight - snapBtnH) * 0.5f;

        m_impl->snapshotButton = {snapBtnX, snapBtnY, snapBtnW, snapBtnH, true};
        canvas.fillRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, style.buttonCornerRadius(), buttonBg);
        canvas.strokeRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, style.buttonCornerRadius(), 1, buttonBorder);
        canvas.text(snapText, snapBtnX + buttonPadX, snapBtnY + buttonPadY + ascent, textColor, monoFont);

        // Record button
        const char* recText = "Record ▾";
        float recTextWidth = canvas.measureText(recText, monoFont);
        float recBtnW = recTextWidth + buttonPadX * 2 + 12;
        float recBtnH = lineH + buttonPadY * 2;
        float recBtnX = snapBtnX - recBtnW - buttonSpacing;
        float recBtnY = (barHeight - recBtnH) * 0.5f;

        m_impl->recordButton = {recBtnX, recBtnY, recBtnW, recBtnH, true};
        glm::vec4 recBtnBg = m_impl->codecDropdownOpen ? buttonHover : buttonBg;
        canvas.fillRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, style.buttonCornerRadius(), recBtnBg);
        canvas.strokeRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, style.buttonCornerRadius(), 1, redColor);
        canvas.fillCircle(recBtnX + buttonPadX + 4, barHeight * 0.5f, 3, redColor);
        canvas.text(recText, recBtnX + buttonPadX + 12, recBtnY + buttonPadY + ascent, textColor, monoFont);

        // Codec dropdown
        m_impl->codecH264.valid = false;
        m_impl->codecH265.valid = false;
        m_impl->codecProRes.valid = false;

        if (m_impl->codecDropdownOpen) {
            canvas.setLayer(UILayer::Menus);

            const char* items[] = {"H.264 (recommended)", "H.265", "ProRes 4444"};
            float menuWidth = 0;
            for (const char* item : items) {
                menuWidth = std::max(menuWidth, canvas.measureText(item, monoFont));
            }
            menuWidth += buttonPadX * 2;

            float menuX = recBtnX;
            float menuY = barHeight + 2;
            float itemH = lineH + buttonPadY * 2;
            float menuH = itemH * 3;

            glm::vec4 menuBg = style.panelBg;
            menuBg.a = 0.98f;

            canvas.fillRoundedRect(menuX, menuY, menuWidth, menuH, style.panelCornerRadius(), menuBg);
            canvas.strokeRoundedRect(menuX, menuY, menuWidth, menuH, style.panelCornerRadius(), 1, buttonBorder);

            float itemY = menuY;
            m_impl->codecH264 = {menuX, itemY, menuWidth, itemH, true};
            canvas.text(items[0], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_impl->codecH265 = {menuX, itemY, menuWidth, itemH, true};
            canvas.text(items[1], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);

            itemY += itemH;
            m_impl->codecProRes = {menuX, itemY, menuWidth, itemH, true};
            canvas.text(items[2], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);
        }
    }

    // Panel toggle context menu (right-click)
    m_impl->menuItemDockLeft.valid = false;
    m_impl->menuItemDockRight.valid = false;
    m_impl->menuItemDockTop.valid = false;
    m_impl->menuItemDockBottom.valid = false;
    m_impl->menuItemFloat.valid = false;
    m_impl->menuItemHide.valid = false;

    if (m_impl->contextMenuOpen && m_impl->contextMenuToggleIndex >= 0 &&
        m_impl->contextMenuToggleIndex < static_cast<int>(m_impl->panelToggles.size())) {
        canvas.setLayer(UILayer::Menus + 10);

        const auto& toggle = m_impl->panelToggles[m_impl->contextMenuToggleIndex];
        const char* menuItems[] = {"Dock Left", "Dock Right", "Dock Top", "Dock Bottom", "Float", "Hide"};
        const int numItems = 6;

        float menuWidth = 0;
        for (const char* item : menuItems) {
            menuWidth = std::max(menuWidth, canvas.measureText(item, monoFont));
        }
        menuWidth += buttonPadX * 2;

        float menuX = toggle.hitRect.x;
        float menuY = barHeight + 2;
        float itemH = lineH + buttonPadY * 2;
        float menuH = itemH * numItems;

        // Ensure menu doesn't go off screen
        if (menuX + menuWidth > screenWidth - padding) {
            menuX = screenWidth - menuWidth - padding;
        }

        glm::vec4 menuBg = style.panelBg;
        menuBg.a = 0.98f;

        m_impl->contextMenuRect = {menuX, menuY, menuWidth, menuH, true};
        canvas.fillRoundedRect(menuX, menuY, menuWidth, menuH, style.panelCornerRadius(), menuBg);
        canvas.strokeRoundedRect(menuX, menuY, menuWidth, menuH, style.panelCornerRadius(), 1, buttonBorder);

        float itemY = menuY;
        Impl::ButtonRect* menuRects[] = {
            &m_impl->menuItemDockLeft, &m_impl->menuItemDockRight,
            &m_impl->menuItemDockTop, &m_impl->menuItemDockBottom,
            &m_impl->menuItemFloat, &m_impl->menuItemHide
        };

        for (int i = 0; i < numItems; i++) {
            *menuRects[i] = {menuX, itemY, menuWidth, itemH, true};

            // Highlight hovered item
            bool itemHovered = m_impl->isMouseInRect(*menuRects[i], mousePos);
            if (itemHovered) {
                canvas.fillRoundedRect(menuX + 2, itemY + 1, menuWidth - 4, itemH - 2,
                                       style.buttonCornerRadius(), style.buttonHover);
            }

            canvas.text(menuItems[i], menuX + buttonPadX, itemY + buttonPadY + ascent, textColor, monoFont);
            itemY += itemH;
        }

        canvas.setLayer(UILayer::Panels);
    }
}

bool StatusBarPanel::handleInput(const FrameInput& input) {
    if (!m_impl) return false;

    glm::vec2 mousePos = input.mousePos;

    // Handle toggle button dragging (drag-from-button to create floating panel)
    if (m_impl->draggingToggle) {
        if (input.mouseDown[0]) {
            // Check if we've exceeded drag threshold
            float dx = mousePos.x - m_impl->dragStartPos.x;
            float dy = mousePos.y - m_impl->dragStartPos.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > Impl::kDragThreshold && m_impl->dragToggleIndex >= 0 &&
                m_impl->dragToggleIndex < static_cast<int>(m_impl->panelToggles.size())) {
                // Trigger drag callback and stop tracking
                const auto& toggle = m_impl->panelToggles[m_impl->dragToggleIndex];
                if (m_impl->panelDragCallback) {
                    m_impl->panelDragCallback(toggle.id, mousePos);
                }
                m_impl->draggingToggle = false;
                m_impl->dragToggleIndex = -1;
                return true;
            }
            return true;
        } else {
            // Mouse released without exceeding threshold - treat as click
            if (m_impl->dragToggleIndex >= 0 &&
                m_impl->dragToggleIndex < static_cast<int>(m_impl->panelToggles.size())) {
                const auto& toggle = m_impl->panelToggles[m_impl->dragToggleIndex];
                if (m_impl->panelToggleCallback) {
                    m_impl->panelToggleCallback(toggle.id);
                }
            }
            m_impl->draggingToggle = false;
            m_impl->dragToggleIndex = -1;
            return true;
        }
    }

    // Handle grid slider dragging
    if (m_impl->draggingGridSlider) {
        if (input.mouseDown[0]) {
            // Update opacity based on mouse position
            const auto& r = m_impl->gridSliderRect;
            float newOpacity = (mousePos.x - r.x) / r.w;
            newOpacity = std::max(0.0f, std::min(1.0f, newOpacity));
            if (newOpacity != m_impl->gridOpacity) {
                m_impl->gridOpacity = newOpacity;
                if (m_impl->gridOpacityCallback) {
                    m_impl->gridOpacityCallback(newOpacity);
                }
            }
            return true;
        } else {
            // Mouse released - stop dragging
            m_impl->draggingGridSlider = false;
            return true;
        }
    }

    // Check for slider click to start dragging
    if (input.mouseClicked[0] && m_impl->isMouseInRect(m_impl->gridSliderRect, mousePos)) {
        m_impl->draggingGridSlider = true;
        // Set initial value based on click position
        const auto& r = m_impl->gridSliderRect;
        float newOpacity = (mousePos.x - r.x) / r.w;
        newOpacity = std::max(0.0f, std::min(1.0f, newOpacity));
        if (newOpacity != m_impl->gridOpacity) {
            m_impl->gridOpacity = newOpacity;
            if (m_impl->gridOpacityCallback) {
                m_impl->gridOpacityCallback(newOpacity);
            }
        }
        return true;
    }

    // Handle right-click on panel toggle buttons (context menu)
    if (input.mouseClicked[1]) {  // Right click
        for (size_t i = 0; i < m_impl->panelToggles.size(); i++) {
            auto& toggle = m_impl->panelToggles[i];
            if (m_impl->isMouseInRect(toggle.hitRect, mousePos)) {
                m_impl->contextMenuOpen = true;
                m_impl->contextMenuToggleIndex = static_cast<int>(i);
                m_impl->codecDropdownOpen = false;  // Close other menus
                return true;
            }
        }
        // Right-click elsewhere closes context menu
        if (m_impl->contextMenuOpen) {
            m_impl->contextMenuOpen = false;
            m_impl->contextMenuToggleIndex = -1;
            return true;
        }
    }

    // Handle context menu clicks
    if (m_impl->contextMenuOpen && input.mouseClicked[0]) {
        const auto& toggle = m_impl->panelToggles[m_impl->contextMenuToggleIndex];

        if (m_impl->isMouseInRect(m_impl->menuItemDockLeft, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::Left);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->menuItemDockRight, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::Right);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->menuItemDockTop, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::Top);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->menuItemDockBottom, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::Bottom);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->menuItemFloat, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::Float);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->menuItemHide, mousePos)) {
            if (m_impl->panelDockCallback) m_impl->panelDockCallback(toggle.id, DockPosition::None);
            m_impl->contextMenuOpen = false;
            return true;
        } else if (!m_impl->isMouseInRect(m_impl->contextMenuRect, mousePos)) {
            // Click outside menu closes it
            m_impl->contextMenuOpen = false;
            return true;
        }
    }

    // Check codec dropdown
    if (m_impl->codecDropdownOpen && input.mouseClicked[0]) {
        if (m_impl->isMouseInRect(m_impl->codecH264, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);  // H264
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->codecH265, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);  // H265
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->codecProRes, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);  // ProRes
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (!m_impl->isMouseInRect(m_impl->recordButton, mousePos)) {
            m_impl->codecDropdownOpen = false;
            return true;
        }
    }

    // Use FrameInput's pre-computed mouseClicked for other interactions
    if (!input.mouseClicked[0]) return false;

    // Panel toggle button clicks - start potential drag
    for (size_t i = 0; i < m_impl->panelToggles.size(); i++) {
        auto& toggle = m_impl->panelToggles[i];
        if (m_impl->isMouseInRect(toggle.hitRect, mousePos)) {
            // Close context menu if open
            m_impl->contextMenuOpen = false;
            m_impl->contextMenuToggleIndex = -1;

            // Start potential drag
            m_impl->draggingToggle = true;
            m_impl->dragToggleIndex = static_cast<int>(i);
            m_impl->dragStartPos = mousePos;
            return true;
        }
    }

    // Button clicks
    if (m_impl->isMouseInRect(m_impl->recordButton, mousePos)) {
        m_impl->codecDropdownOpen = true;
        return true;
    } else if (m_impl->isMouseInRect(m_impl->stopButton, mousePos)) {
        if (m_impl->recordCallback) m_impl->recordCallback(false);
        return true;
    } else if (m_impl->isMouseInRect(m_impl->snapshotButton, mousePos)) {
        if (m_impl->snapshotCallback) m_impl->snapshotCallback();
        return true;
    }

    return false;
}

void StatusBarPanel::setPendingChangeCount(size_t count) {
    if (m_impl) m_impl->pendingChangeCount = count;
}

void StatusBarPanel::setMcpWarning(const std::string& warning) {
    if (m_impl) m_impl->mcpWarning = warning;
}

void StatusBarPanel::setVideoExporter(VideoExporter* exporter) {
    if (m_impl) m_impl->exporter = exporter;
}

void StatusBarPanel::onSnapshot(SnapshotCallback callback) {
    if (m_impl) m_impl->snapshotCallback = std::move(callback);
}

void StatusBarPanel::onRecord(RecordCallback callback) {
    if (m_impl) m_impl->recordCallback = std::move(callback);
}

void StatusBarPanel::onGridOpacityChange(GridOpacityCallback callback) {
    if (m_impl) m_impl->gridOpacityCallback = std::move(callback);
}

void StatusBarPanel::setGridOpacity(float opacity) {
    if (m_impl) m_impl->gridOpacity = std::max(0.0f, std::min(1.0f, opacity));
}

float StatusBarPanel::gridOpacity() const {
    return m_impl ? m_impl->gridOpacity : 0.0f;
}

void StatusBarPanel::setPanelVisibility(const std::string& panelId, bool visible) {
    if (!m_impl) return;

    for (auto& toggle : m_impl->panelToggles) {
        if (toggle.id == panelId) {
            // Legacy compatibility: map visibility to dock mode
            if (visible && toggle.dockMode == DockMode::Hidden) {
                toggle.dockMode = DockMode::Docked;  // Default to docked when showing
            } else if (!visible) {
                toggle.dockMode = DockMode::Hidden;
            }
            return;
        }
    }
}

void StatusBarPanel::setPanelDockMode(const std::string& panelId, DockMode mode) {
    if (!m_impl) return;

    for (auto& toggle : m_impl->panelToggles) {
        if (toggle.id == panelId) {
            toggle.dockMode = mode;
            return;
        }
    }
}

void StatusBarPanel::onPanelToggle(PanelToggleCallback callback) {
    if (m_impl) m_impl->panelToggleCallback = std::move(callback);
}

void StatusBarPanel::onPanelDrag(PanelDragCallback callback) {
    if (m_impl) m_impl->panelDragCallback = std::move(callback);
}

void StatusBarPanel::onPanelDock(PanelDockCallback callback) {
    if (m_impl) m_impl->panelDockCallback = std::move(callback);
}

} // namespace vivid
