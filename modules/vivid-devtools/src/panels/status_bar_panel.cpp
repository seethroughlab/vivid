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
    ButtonRect gridToggleButton;

    // Codec dropdown
    bool codecDropdownOpen = false;
    ButtonRect codecH264;
    ButtonRect codecH265;
    ButtonRect codecProRes;

    // Grid visibility (stored here, but should be synced with NodeGraph)
    bool showGrid = true;

    // Smoothed values for FPS
    float smoothedFps = 60.0f;
    float smoothedMs = 16.67f;

    bool isMouseInRect(const ButtonRect& r, glm::vec2 mousePos) const {
        return r.valid && mousePos.x >= r.x && mousePos.x < r.x + r.w &&
               mousePos.y >= r.y && mousePos.y < r.y + r.h;
    }
};

StatusBarPanel::StatusBarPanel() {
    m_config.id = "statusbar";
    m_config.title = "";
    m_config.bounds = {0, 0, 0, 32};  // Full width, fixed height
    m_config.dockSide = DockSide::Top;
    m_config.visible = true;
    m_config.resizable = false;
    m_config.draggable = false;
}

StatusBarPanel::~StatusBarPanel() = default;

bool StatusBarPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->ctx = &ctx;
    return true;
}

void StatusBarPanel::shutdown() {
    m_impl.reset();
}

void StatusBarPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                            const FrameInput& input, float scale, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;
    // Style parameter is available for use throughout this function

    // Use mono font metrics for bar height calculation
    const int monoFont = 2;
    float lineH = canvas.fontLineHeight(monoFont);
    float ascent = canvas.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 6.0f;
    const float barHeight = lineH + padding * 2;
    float x = padding;
    float y = padding + ascent;

    // Update smoothed FPS (exponential moving average)
    const float smoothing = 0.05f;
    float instantFps = input.dt > 0 ? 1.0f / input.dt : m_impl->smoothedFps;
    float instantMs = input.dt * 1000.0f;
    m_impl->smoothedFps = m_impl->smoothedFps + smoothing * (instantFps - m_impl->smoothedFps);
    m_impl->smoothedMs = m_impl->smoothedMs + smoothing * (instantMs - m_impl->smoothedMs);

    // Semi-transparent background
    glm::vec4 barBg = style.headerBg;
    barBg.a = 0.95f;
    canvas.fillRect(0, 0, static_cast<float>(input.width), barHeight, barBg);

    // Colors from style
    glm::vec4 textColor = style.textPrimary;
    glm::vec4 dimColor = style.textDim;
    glm::vec4 greenColor = style.success;
    glm::vec4 yellowColor = style.warning;
    glm::vec4 redColor = style.error;

    char buf[64];
    const float sepInset = padding;

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

    // Grid toggle checkbox
    {
        canvas.fillRect(x, sepInset, 1, barHeight - sepInset * 2, dimColor);
        x += padding * 2;

        float checkSize = lineH * 0.7f;
        float checkY = (barHeight - checkSize) * 0.5f;
        glm::vec4 checkBg = style.checkboxBg;
        glm::vec4 checkBorder = style.checkboxBorder;
        glm::vec4 checkFill = style.checkboxCheck;

        canvas.fillRect(x, checkY, checkSize, checkSize, checkBg);
        canvas.strokeRect(x, checkY, checkSize, checkSize, 1, checkBorder);

        if (m_impl->showGrid) {
            float cx = x + checkSize * 0.5f;
            float cy = checkY + checkSize * 0.5f;
            float s = checkSize * 0.3f;
            canvas.line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, 2.0f, checkFill);
            canvas.line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.5f, 2.0f, checkFill);
        }

        float checkboxWidth = checkSize;
        x += checkboxWidth + 4;

        canvas.text("Grid", x, y, textColor, monoFont);
        float labelWidth = canvas.measureText("Grid", monoFont);

        m_impl->gridToggleButton = {x - checkboxWidth - 4, checkY, checkboxWidth + 4 + labelWidth, checkSize, true};
        x += labelWidth + padding * 2;
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
        float stopBtnX = input.width - stopBtnW - padding;
        float stopBtnY = (barHeight - stopBtnH) * 0.5f;

        m_impl->stopButton = {stopBtnX, stopBtnY, stopBtnW, stopBtnH, true};
        canvas.fillRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 4, buttonBg);
        canvas.strokeRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 4, 1, redColor);
        canvas.text(stopText, stopBtnX + buttonPadX, stopBtnY + buttonPadY + ascent, redColor, monoFont);

        float recX = stopBtnX - recTextWidth - 24 - buttonSpacing;
        canvas.fillCircle(recX + 6, barHeight * 0.5f, 4, redColor);
        canvas.text(buf, recX + 16, y, redColor, monoFont);
    } else {
        // Not recording
        float rightX = input.width - padding;

        // Snapshot button
        const char* snapText = "Snapshot";
        float snapTextWidth = canvas.measureText(snapText, monoFont);
        float snapBtnW = snapTextWidth + buttonPadX * 2;
        float snapBtnH = lineH + buttonPadY * 2;
        float snapBtnX = rightX - snapBtnW;
        float snapBtnY = (barHeight - snapBtnH) * 0.5f;

        m_impl->snapshotButton = {snapBtnX, snapBtnY, snapBtnW, snapBtnH, true};
        canvas.fillRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, 4, buttonBg);
        canvas.strokeRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, 4, 1, buttonBorder);
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
        canvas.fillRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, 4, recBtnBg);
        canvas.strokeRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, 4, 1, redColor);
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

            canvas.fillRoundedRect(menuX, menuY, menuWidth, menuH, 4, menuBg);
            canvas.strokeRoundedRect(menuX, menuY, menuWidth, menuH, 4, 1, buttonBorder);

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
}

bool StatusBarPanel::handleInput(const FrameInput& input) {
    if (!m_impl) return false;

    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    glm::vec2 mousePos = input.mousePos * scale;

    // Check for mouse click
    static bool lastMouseDown = false;
    bool mouseClicked = input.mouseDown[0] && !lastMouseDown;
    lastMouseDown = input.mouseDown[0];

    if (!mouseClicked) return false;

    // Check codec dropdown first
    if (m_impl->codecDropdownOpen) {
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
    } else if (m_impl->isMouseInRect(m_impl->gridToggleButton, mousePos)) {
        m_impl->showGrid = !m_impl->showGrid;
        // TODO: Sync with NodeGraph
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

} // namespace vivid
