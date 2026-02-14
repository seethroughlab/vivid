// Status Bar Panel Implementation
// Shows FPS, memory, recording controls, grid opacity

#include <vivid/devtools/panels/status_bar_panel.h>
#include <vivid/video_exporter.h>
#include <vivid/context.h>
#include <vivid/audio_graph.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <vivid/devtools/system_info.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <iostream>

namespace vivid {

struct StatusBarPanel::Impl {
    // Callbacks
    SnapshotCallback snapshotCallback;
    RecordCallback recordCallback;
    GridOpacityCallback gridOpacityCallback;

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
};

StatusBarPanel::StatusBarPanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "statusbar";
    m_config.title = "";
    m_config.bounds = {0, 0, 0, 32};  // Full width, fixed height
    m_config.role = PanelRole::StatusBar;
    m_config.visible = true;
    m_config.resizable = false;
    m_config.draggable = false;
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
                            const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    // Compute logical screen dimensions for HiDPI displays
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenWidth = static_cast<float>(input.width) / scale;

    // Use bounds height for the bar (from layout), fall back to calculated if bounds not set
    const int monoFont = 0;
    float lineH = canvas.fontLineHeight(monoFont);
    float ascent = canvas.fontAscent(monoFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    const float padding = 6.0f;
    const float barHeight = (bounds.w > 0) ? bounds.w : (lineH + padding * 2);
    float x = bounds.x + padding;
    float y = bounds.y + (barHeight + ascent) * 0.5f;

    // Update smoothed FPS (exponential moving average)
    const float smoothing = 0.05f;
    float instantFps = input.dt > 0 ? 1.0f / input.dt : m_impl->smoothedFps;
    float instantMs = input.dt * 1000.0f;
    m_impl->smoothedFps = m_impl->smoothedFps + smoothing * (instantFps - m_impl->smoothedFps);
    m_impl->smoothedMs = m_impl->smoothedMs + smoothing * (instantMs - m_impl->smoothedMs);

    // Semi-transparent background
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
    const float sepInset = bounds.y + padding;
    const float sepHeight = barHeight - padding * 2;

    // Grid opacity slider
    {
        const char* gridLabel = "Grid";
        float labelW = canvas.measureText(gridLabel, monoFont);
        canvas.text(gridLabel, x, y, dimColor, monoFont);
        x += labelW + 6;

        const float sliderW = 60.0f;
        const float sliderH = lineH;
        const float sliderY = bounds.y + (barHeight - sliderH) * 0.5f;
        const float sliderRadius = style.sliderCornerRadius();

        // Slider track background
        glm::vec4 trackColor = style.sliderBg;
        canvas.fillRoundedRect(x, sliderY, sliderW, sliderH, sliderRadius, trackColor);

        // Filled portion based on opacity
        float fillW = sliderW * m_impl->gridOpacity;
        if (fillW > 0) {
            glm::vec4 fillColor = style.accent;
            fillColor.a = 0.7f;
            if (fillW >= sliderW - sliderRadius) {
                canvas.fillRoundedRect(x, sliderY, fillW, sliderH, sliderRadius, fillColor);
            } else {
                canvas.fillRoundedRect(x, sliderY, std::max(fillW, sliderRadius * 2), sliderH, sliderRadius, fillColor);
                if (fillW < sliderRadius * 2) {
                    canvas.fillRect(x + fillW, sliderY, sliderRadius * 2 - fillW, sliderH, trackColor);
                }
            }
        }

        canvas.strokeRoundedRect(x, sliderY, sliderW, sliderH, sliderRadius, 1, style.buttonBorder);

        m_impl->gridSliderRect = {x, sliderY, sliderW, sliderH, true};
        x += sliderW + padding;
    }

    // Separator after grid slider
    canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
    x += padding * 2;

    // FPS
    snprintf(buf, sizeof(buf), "%5.1f FPS", m_impl->smoothedFps);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
    x += padding * 2;

    // Frame time
    snprintf(buf, sizeof(buf), "%6.2fms", m_impl->smoothedMs);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
    x += padding * 2;

    // Resolution
    snprintf(buf, sizeof(buf), "%4dx%-4d", input.width, input.height);
    canvas.text(buf, x, y, textColor, monoFont);
    x += canvas.measureText(buf, monoFont) + padding * 2;

    canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
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
        canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
        x += padding * 2;

        snprintf(buf, sizeof(buf), "Pending: %zu", m_impl->pendingChangeCount);
        canvas.text(buf, x, y, yellowColor, monoFont);
        x += canvas.measureText(buf, monoFont) + padding * 2;
    }

    // MCP warning
    if (!m_impl->mcpWarning.empty()) {
        canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
        x += padding * 2;

        canvas.text(m_impl->mcpWarning, x, y, redColor, monoFont);
        x += canvas.measureText(m_impl->mcpWarning, monoFont) + padding * 2;
    }

    // Audio stats (if audio active)
    if (m_impl->ctx && m_impl->ctx->hasChain()) {
        AudioGraph* audioGraph = m_impl->ctx->chain().audioGraph();
        if (audioGraph && !audioGraph->empty()) {
            canvas.fillRect(x, sepInset, 1, sepHeight, dimColor);
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
        float stopBtnY = bounds.y + (barHeight - stopBtnH) * 0.5f;

        m_impl->stopButton = {stopBtnX, stopBtnY, stopBtnW, stopBtnH, true};
        canvas.fillRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, style.buttonCornerRadius(), buttonBg);
        canvas.strokeRoundedRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, style.buttonCornerRadius(), 1, redColor);
        canvas.text(stopText, stopBtnX + buttonPadX, stopBtnY + (stopBtnH + ascent) * 0.5f, redColor, monoFont);

        float recX = stopBtnX - recTextWidth - 24 - buttonSpacing;
        float recTextY = bounds.y + (barHeight + ascent) * 0.5f;
        canvas.fillCircle(recX + 6, bounds.y + barHeight * 0.5f, 4, redColor);
        canvas.text(buf, recX + 16, recTextY, redColor, monoFont);
    } else {
        // Not recording
        float rightX = screenWidth - padding;

        // Snapshot button
        const char* snapText = "Snapshot";
        float snapTextWidth = canvas.measureText(snapText, monoFont);
        float snapBtnW = snapTextWidth + buttonPadX * 2;
        float snapBtnH = lineH + buttonPadY * 2;
        float snapBtnX = rightX - snapBtnW;
        float snapBtnY = bounds.y + (barHeight - snapBtnH) * 0.5f;

        m_impl->snapshotButton = {snapBtnX, snapBtnY, snapBtnW, snapBtnH, true};
        canvas.fillRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, style.buttonCornerRadius(), buttonBg);
        canvas.strokeRoundedRect(snapBtnX, snapBtnY, snapBtnW, snapBtnH, style.buttonCornerRadius(), 1, buttonBorder);
        canvas.text(snapText, snapBtnX + buttonPadX, snapBtnY + (snapBtnH + ascent) * 0.5f, textColor, monoFont);

        // Record button
        const char* recText = "Record \u25BE";
        float recTextWidth = canvas.measureText(recText, monoFont);
        float recBtnW = recTextWidth + buttonPadX * 2 + 12;
        float recBtnH = lineH + buttonPadY * 2;
        float recBtnX = snapBtnX - recBtnW - buttonSpacing;
        float recBtnY = bounds.y + (barHeight - recBtnH) * 0.5f;

        m_impl->recordButton = {recBtnX, recBtnY, recBtnW, recBtnH, true};
        glm::vec4 recBtnBg = m_impl->codecDropdownOpen ? buttonHover : buttonBg;
        canvas.fillRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, style.buttonCornerRadius(), recBtnBg);
        canvas.strokeRoundedRect(recBtnX, recBtnY, recBtnW, recBtnH, style.buttonCornerRadius(), 1, redColor);
        canvas.fillCircle(recBtnX + buttonPadX + 4, recBtnY + recBtnH * 0.5f, 3, redColor);
        canvas.text(recText, recBtnX + buttonPadX + 12, recBtnY + (recBtnH + ascent) * 0.5f, textColor, monoFont);

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
            float menuY = bounds.y + barHeight + 2;
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
}

bool StatusBarPanel::handleInput(const gui::InputState& input) {
    if (!m_impl) return false;

    glm::vec2 mousePos = input.mousePos;

    // Handle grid slider dragging
    if (m_impl->draggingGridSlider) {
        if (input.mouseDown[0]) {
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
            m_impl->draggingGridSlider = false;
            return true;
        }
    }

    // Check for slider click to start dragging
    if (input.mouseClicked[0] && m_impl->isMouseInRect(m_impl->gridSliderRect, mousePos)) {
        m_impl->draggingGridSlider = true;
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

    // Check codec dropdown
    if (m_impl->codecDropdownOpen && input.mouseClicked[0]) {
        if (m_impl->isMouseInRect(m_impl->codecH264, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->codecH265, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (m_impl->isMouseInRect(m_impl->codecProRes, mousePos)) {
            if (m_impl->recordCallback) m_impl->recordCallback(true);
            m_impl->codecDropdownOpen = false;
            return true;
        } else if (!m_impl->isMouseInRect(m_impl->recordButton, mousePos)) {
            m_impl->codecDropdownOpen = false;
            return true;
        }
    }

    if (!input.mouseClicked[0]) return false;

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

} // namespace vivid
