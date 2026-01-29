// Immediate-mode GUI widget system
// Built on OverlayCanvas for efficient GPU rendering

#include <vivid/gui/gui.h>
#include <vivid/gui/ui_style.h>
#include <vivid/context.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// RGB (0-1) to HSV (H: 0-360, S: 0-1, V: 0-1)
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    v = maxVal;
    s = (maxVal > 0.0001f) ? (delta / maxVal) : 0.0f;

    if (delta < 0.0001f) {
        h = 0.0f;
    } else if (maxVal == r) {
        h = 60.0f * std::fmod((g - b) / delta + 6.0f, 6.0f);
    } else if (maxVal == g) {
        h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        h = 60.0f * ((r - g) / delta + 4.0f);
    }
}

// HSV (H: 0-360, S: 0-1, V: 0-1) to RGB (0-1)
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rp, gp, bp;
    if (h < 60.0f)       { rp = c; gp = x; bp = 0; }
    else if (h < 120.0f) { rp = x; gp = c; bp = 0; }
    else if (h < 180.0f) { rp = 0; gp = c; bp = x; }
    else if (h < 240.0f) { rp = 0; gp = x; bp = c; }
    else if (h < 300.0f) { rp = x; gp = 0; bp = c; }
    else                 { rp = c; gp = 0; bp = x; }

    r = rp + m;
    g = gp + m;
    b = bp + m;
}

} // anonymous namespace

namespace vivid {

// Thread-local interaction state (persists across frames, thread-safe)
thread_local Gui::InteractionState Gui::s_state;

// Thread-local mouse state tracking for click detection
static thread_local bool s_lastMouseDown = false;

// -------------------------------------------------------------------------
// Construction
// -------------------------------------------------------------------------

// Helper to convert FrameInput to gui::InputState
static gui::InputState toInputState(const FrameInput& input) {
    gui::InputState state;
    state.width = input.width;
    state.height = input.height;
    state.contentScale = input.contentScale;
    state.dt = input.dt;
    state.time = input.time;
    state.mousePos = input.mousePos;
    state.mouseDelta = input.mouseDelta;
    state.scroll = input.scroll;
    std::memcpy(state.mouseDown, input.mouseDown, sizeof(state.mouseDown));
    std::memcpy(state.mouseClicked, input.mouseClicked, sizeof(state.mouseClicked));
    std::memcpy(state.mouseReleased, input.mouseReleased, sizeof(state.mouseReleased));
    state.keyCtrl = input.keyCtrl;
    state.keyShift = input.keyShift;
    state.keyAlt = input.keyAlt;
    state.keySuper = input.keySuper;
    std::memcpy(state.keyPressed, input.keyPressed, sizeof(state.keyPressed));
    std::memcpy(state.keyDown, input.keyDown, sizeof(state.keyDown));
    return state;
}

Gui::Gui(OverlayCanvas& canvas, const FrameInput& input)
    : m_canvas(canvas)
    , m_input(toInputState(input))
{
    // Track mouse state for click detection
    m_mouseClicked = input.mouseDown[0] && !s_lastMouseDown;
    m_mouseReleased = !input.mouseDown[0] && s_lastMouseDown;
    s_lastMouseDown = input.mouseDown[0];
    m_mousePos = input.mousePos;
}

Gui::Gui(OverlayCanvas& canvas, const gui::InputState& input)
    : m_canvas(canvas)
    , m_input(input)
{
    // Track mouse state for click detection
    m_mouseClicked = input.mouseDown[0] && !s_lastMouseDown;
    m_mouseReleased = !input.mouseDown[0] && s_lastMouseDown;
    s_lastMouseDown = input.mouseDown[0];
    m_mousePos = input.mousePos;
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

uint32_t Gui::hashId(const char* label) const {
    // Simple FNV-1a hash
    uint32_t hash = 2166136261u;

    // First, mix in all ID stack prefixes
    for (const auto& prefix : m_idStack) {
        for (char c : prefix) {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        // Add separator
        hash ^= static_cast<uint32_t>('.');
        hash *= 16777619u;
    }

    // Then hash the label itself
    for (const char* p = label; *p; ++p) {
        hash ^= static_cast<uint32_t>(*p);
        hash *= 16777619u;
    }

    // Mix in panel context so same label in different panels gets different ID
    if (m_inPanel) {
        for (char c : m_panel.title) {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
    }
    return hash;
}

float Gui::contentWidth() const {
    return m_panel.w - m_style.padding * 2;
}

void Gui::advanceCursor(float height) {
    // Track widget bounds for visibility testing
    m_lastWidgetTop = m_panel.cursorY;
    m_lastWidgetBottom = m_panel.cursorY + height;

    m_panel.cursorY += height + m_style.spacing;
    m_panel.contentHeight = m_panel.cursorY - m_panel.y - m_style.titleHeight;
}

bool Gui::isMouseInRect(float x, float y, float w, float h) const {
    return m_mousePos.x >= x && m_mousePos.x < x + w &&
           m_mousePos.y >= y && m_mousePos.y < y + h;
}

void Gui::drawWidgetBackground(float x, float y, float w, float h, bool hovered, bool active) {
    glm::vec4 bg = active ? m_style.widgetActive :
                   hovered ? m_style.widgetHover :
                   m_style.widgetBackground;
    m_canvas.fillRoundedRect(x, y, w, h, m_style.cornerRadius, bg);
    m_canvas.strokeRoundedRect(x, y, w, h, m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);
}

void Gui::drawLabel(float x, float y, const char* text) {
    float ascent = m_canvas.fontAscent(0);
    float descent = std::abs(m_canvas.fontDescent(0));
    float baseline = y + m_style.widgetHeight * 0.5f + (ascent - descent) * 0.5f;
    m_canvas.text(text, x, baseline, m_style.text, 0);
}

// -------------------------------------------------------------------------
// Panels
// -------------------------------------------------------------------------

void Gui::beginPanel(const char* title, float x, float y, float w, float h) {
    m_inPanel = true;
    m_panel.x = x;
    m_panel.y = y;
    m_panel.w = w;
    m_panel.h = h;
    m_panel.title = title;
    m_panel.cursorY = y + m_style.titleHeight + m_style.padding;

    // Draw panel background
    m_canvas.setLayer(UILayer::Panels);
    m_canvas.fillRoundedRect(x, y, w, h, m_style.cornerRadius * 1.5f, m_style.panelBackground);
    m_canvas.strokeRoundedRect(x, y, w, h, m_style.cornerRadius * 1.5f, m_style.borderWidth, m_style.panelBorder);

    // Draw title bar
    m_canvas.fillRoundedRect(x, y, w, m_style.titleHeight, m_style.cornerRadius * 1.5f, m_style.panelHeader);
    float ascent = m_canvas.fontAscent(0);
    float descent = std::abs(m_canvas.fontDescent(0));
    float titleBaseline = y + m_style.titleHeight * 0.5f + (ascent - descent) * 0.5f;
    m_canvas.text(title, x + m_style.padding, titleBaseline, m_style.text, 0);

    // Set up clipping for content area
    m_canvas.beginClipRect(x, y + m_style.titleHeight, w, h - m_style.titleHeight);
}

void Gui::endPanel() {
    m_canvas.endClipRect();
    m_inPanel = false;
}

void Gui::beginArea(float x, float y, float w, float h) {
    m_inPanel = true;  // Enable widgets
    m_panel.x = x;
    m_panel.y = y;
    m_panel.w = w;
    m_panel.h = h;
    m_panel.title.clear();
    m_panel.cursorY = y;

    // Set up clipping
    m_canvas.beginClipRect(x, y, w, h);
}

void Gui::endArea() {
    m_canvas.endClipRect();
    m_inPanel = false;
}

void Gui::setCursorY(float y) {
    m_panel.cursorY = y;
}

// -------------------------------------------------------------------------
// Layout Helpers
// -------------------------------------------------------------------------

void Gui::spacing(float height) {
    if (!m_inPanel) return;
    m_panel.cursorY += height > 0 ? height : m_style.spacing;
}

void Gui::separator() {
    if (!m_inPanel) return;

    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY + m_style.spacing * 0.5f;
    float w = contentWidth();

    m_canvas.line(x, y, x + w, y, 1.0f, m_style.widgetBorder);
    m_panel.cursorY += m_style.spacing;
}

// -------------------------------------------------------------------------
// Widgets
// -------------------------------------------------------------------------

void Gui::label(const char* text) {
    if (!m_inPanel) return;

    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;

    drawLabel(x, y, text);
    advanceCursor(m_style.widgetHeight);
}

bool Gui::button(const char* label) {
    if (!m_inPanel) return false;

    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();
    float h = m_style.widgetHeight;

    bool hovered = isMouseInRect(x, y, w, h);
    bool clicked = hovered && m_mouseClicked;

    // Draw button
    drawWidgetBackground(x, y, w, h, hovered, false);

    // Center text
    float textW = m_canvas.measureText(label, 0);
    float textX = x + (w - textW) * 0.5f;
    float btnAscent = m_canvas.fontAscent(0);
    float btnDescent = std::abs(m_canvas.fontDescent(0));
    float textY = y + h * 0.5f + (btnAscent - btnDescent) * 0.5f;
    m_canvas.text(label, textX, textY, m_style.text, 0);

    advanceCursor(h);
    return clicked;
}

bool Gui::checkbox(const char* label, bool* value) {
    if (!m_inPanel || !value) return false;

    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float boxSize = m_style.widgetHeight - 4;
    float boxX = x;
    float boxY = y + 2;

    bool hovered = isMouseInRect(boxX, boxY, boxSize, boxSize);
    bool clicked = hovered && m_mouseClicked;

    if (clicked) {
        *value = !*value;
    }

    // Draw checkbox box
    drawWidgetBackground(boxX, boxY, boxSize, boxSize, hovered, false);

    // Draw checkmark if checked
    if (*value) {
        float cx = boxX + boxSize * 0.5f;
        float cy = boxY + boxSize * 0.5f;
        float s = boxSize * 0.3f;
        // Simple checkmark as two lines
        m_canvas.line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, 2.0f, m_style.checkmark);
        m_canvas.line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.5f, 2.0f, m_style.checkmark);
    }

    // Draw label
    float labelX = boxX + boxSize + m_style.padding;
    drawLabel(labelX, y, label);

    advanceCursor(m_style.widgetHeight);
    return clicked;
}

bool Gui::slider(const char* label, float* value, float min, float max) {
    if (!m_inPanel || !value) return false;

    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();
    float h = m_style.widgetHeight;

    // Calculate layout based on style options
    float sliderX, sliderY, sliderW, sliderH;
    float totalHeight = h;

    if (m_style.labelPosition == LabelPosition::Above) {
        // Label on its own row, slider below
        float labelH = m_canvas.fontLineHeight(0);
        if (labelH <= 0) labelH = 16.0f;

        sliderX = x;
        sliderY = y + labelH + 2;
        sliderW = w;
        sliderH = h - 4;
        totalHeight = labelH + 2 + h;

        // Adjust for value display on right
        if (m_style.valuePosition == ValuePosition::Right) {
            sliderW = w - m_style.valueWidth - m_style.padding;
        }
    } else {
        // Label to the left
        sliderX = x + m_style.labelWidth;
        sliderY = y + 2;
        sliderW = w - m_style.labelWidth;
        sliderH = h - 4;

        if (m_style.valuePosition == ValuePosition::Right) {
            sliderW -= m_style.valueWidth + m_style.padding;
        }
    }

    bool hovered = isMouseInRect(sliderX, sliderY, sliderW, sliderH);
    bool active = (s_state.activeSlider == id);

    // Start drag
    if (hovered && m_mouseClicked) {
        s_state.activeSlider = id;
        s_state.sliderStartValue = *value;
        s_state.sliderStartMouseX = m_mousePos.x;
        active = true;
    }

    // Continue drag
    bool changed = false;
    if (active) {
        if (m_input.mouseDown[0]) {
            // Calculate new value from mouse position
            float t = std::clamp((m_mousePos.x - sliderX) / sliderW, 0.0f, 1.0f);
            float newValue = min + t * (max - min);
            if (newValue != *value) {
                *value = newValue;
                changed = true;
            }
        } else {
            // End drag - track that this slider's drag ended
            s_state.activeSlider = 0;
            m_sliderDragEnded = true;
            m_lastSliderDragId = id;
        }
    }

    // Draw label
    if (m_style.labelPosition == LabelPosition::Above) {
        float baseline = y + m_canvas.fontAscent(0);
        m_canvas.text(label, x, baseline, m_style.textDim, 0);
    } else {
        drawLabel(x, y, label);
    }

    // Draw slider background
    m_canvas.fillRoundedRect(sliderX, sliderY, sliderW, sliderH, m_style.cornerRadius, m_style.widgetBackground);

    // Draw fill
    float range = max - min;
    float norm = range > 0 ? std::clamp((*value - min) / range, 0.0f, 1.0f) : 0.0f;
    float fillW = norm * sliderW;
    if (fillW > 0) {
        glm::vec4 fillColor = active ? m_style.sliderFillActive : m_style.sliderFill;
        m_canvas.fillRoundedRect(sliderX, sliderY, fillW, sliderH, m_style.cornerRadius, fillColor);
    }

    // Draw border
    m_canvas.strokeRoundedRect(sliderX, sliderY, sliderW, sliderH, m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Draw value text based on valuePosition
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "%.2f", *value);

    float sliderAscent = m_canvas.fontAscent(0);
    float sliderDescent = std::abs(m_canvas.fontDescent(0));
    if (m_style.valuePosition == ValuePosition::Center) {
        float textW = m_canvas.measureText(valueBuf, 0);
        float textX = sliderX + (sliderW - textW) * 0.5f;
        float textY = sliderY + sliderH * 0.5f + (sliderAscent - sliderDescent) * 0.5f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    } else if (m_style.valuePosition == ValuePosition::Right) {
        float textX = sliderX + sliderW + m_style.padding;
        float textY = sliderY + sliderH * 0.5f + (sliderAscent - sliderDescent) * 0.5f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    }
    // ValuePosition::None - don't draw value

    advanceCursor(totalHeight);
    return changed;
}

bool Gui::slider(const char* label, int* value, int min, int max) {
    if (!value) return false;
    float fval = static_cast<float>(*value);
    bool changed = slider(label, &fval, static_cast<float>(min), static_cast<float>(max));
    if (changed) {
        *value = static_cast<int>(std::round(fval));
    }
    return changed;
}

Gui::SliderResult Gui::sliderEx(const char* label, float* value, float min, float max) {
    SliderResult result;
    if (!m_inPanel || !value) return result;

    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();
    float h = m_style.widgetHeight;

    // Calculate layout based on style options
    float sliderX, sliderY, sliderW, sliderH;
    float totalHeight = h;

    if (m_style.labelPosition == LabelPosition::Above) {
        // Label on its own row, slider below
        float labelH = m_canvas.fontLineHeight(0);
        if (labelH <= 0) labelH = 16.0f;

        sliderX = x;
        sliderY = y + labelH + 2;
        sliderW = w;
        sliderH = h - 4;
        totalHeight = labelH + 2 + h;

        // Adjust for value display on right
        if (m_style.valuePosition == ValuePosition::Right) {
            sliderW = w - m_style.valueWidth - m_style.padding;
        }
    } else {
        // Label to the left
        sliderX = x + m_style.labelWidth;
        sliderY = y + 2;
        sliderW = w - m_style.labelWidth;
        sliderH = h - 4;

        if (m_style.valuePosition == ValuePosition::Right) {
            sliderW -= m_style.valueWidth + m_style.padding;
        }
    }

    bool hovered = isMouseInRect(sliderX, sliderY, sliderW, sliderH);
    bool active = (s_state.activeSlider == id);

    // Start drag
    if (hovered && m_mouseClicked) {
        s_state.activeSlider = id;
        s_state.sliderStartValue = *value;
        s_state.sliderStartMouseX = m_mousePos.x;
        active = true;
        result.dragStarted = true;
        result.startValue = *value;
    }

    // Continue drag
    if (active) {
        result.startValue = s_state.sliderStartValue;

        if (m_input.mouseDown[0]) {
            // Calculate new value from mouse position
            float t = std::clamp((m_mousePos.x - sliderX) / sliderW, 0.0f, 1.0f);
            float newValue = min + t * (max - min);
            if (newValue != *value) {
                *value = newValue;
                result.changed = true;
            }
        } else {
            // End drag
            s_state.activeSlider = 0;
            result.dragEnded = true;
            m_sliderDragEnded = true;
            m_lastSliderDragId = id;
        }
    }

    // Draw label
    if (m_style.labelPosition == LabelPosition::Above) {
        float baseline = y + m_canvas.fontAscent(0);
        m_canvas.text(label, x, baseline, m_style.textDim, 0);
    } else {
        drawLabel(x, y, label);
    }

    // Draw slider background
    m_canvas.fillRoundedRect(sliderX, sliderY, sliderW, sliderH, m_style.cornerRadius, m_style.widgetBackground);

    // Draw fill
    float range = max - min;
    float norm = range > 0 ? std::clamp((*value - min) / range, 0.0f, 1.0f) : 0.0f;
    float fillW = norm * sliderW;
    if (fillW > 0) {
        glm::vec4 fillColor = active ? m_style.sliderFillActive : m_style.sliderFill;
        m_canvas.fillRoundedRect(sliderX, sliderY, fillW, sliderH, m_style.cornerRadius, fillColor);
    }

    // Draw border
    m_canvas.strokeRoundedRect(sliderX, sliderY, sliderW, sliderH, m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Draw value text based on valuePosition
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "%.2f", *value);

    float sliderExAscent = m_canvas.fontAscent(0);
    float sliderExDescent = std::abs(m_canvas.fontDescent(0));
    if (m_style.valuePosition == ValuePosition::Center) {
        float textW = m_canvas.measureText(valueBuf, 0);
        float textX = sliderX + (sliderW - textW) * 0.5f;
        float textY = sliderY + sliderH * 0.5f + (sliderExAscent - sliderExDescent) * 0.5f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    } else if (m_style.valuePosition == ValuePosition::Right) {
        float textX = sliderX + sliderW + m_style.padding;
        float textY = sliderY + sliderH * 0.5f + (sliderExAscent - sliderExDescent) * 0.5f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    }
    // ValuePosition::None - don't draw value

    advanceCursor(totalHeight);
    return result;
}

bool Gui::dropdown(const char* label, int* index, const std::vector<std::string>& options) {
    if (!m_inPanel || !index || options.empty()) return false;

    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();
    float h = m_style.widgetHeight;

    // Layout variables - depend on label position style
    float buttonX, buttonY, buttonW;
    float totalHeight = h;

    if (m_style.labelPosition == LabelPosition::Above) {
        // Label on its own row, dropdown below at full width
        float labelH = m_canvas.fontLineHeight(0);
        if (labelH <= 0) labelH = 16.0f;

        buttonX = x;
        buttonY = y + labelH + 2;
        buttonW = w;
        totalHeight = labelH + 2 + h;
    } else {
        // Label to the left (original side-by-side layout)
        buttonX = x + m_style.labelWidth;
        buttonY = y;
        buttonW = w - m_style.labelWidth;
    }

    bool isOpen = (s_state.openDropdown == id);
    bool hovered = isMouseInRect(buttonX, buttonY, buttonW, h);

    // Draw label (position depends on style)
    if (m_style.labelPosition == LabelPosition::Above) {
        float baseline = y + m_canvas.fontAscent(0);
        m_canvas.text(label, x, baseline, m_style.textDim, 0);
    } else {
        drawLabel(x, y, label);
    }

    // Draw dropdown button
    drawWidgetBackground(buttonX, buttonY, buttonW, h, hovered || isOpen, isOpen);

    // Draw current selection
    int safeIndex = std::clamp(*index, 0, static_cast<int>(options.size()) - 1);
    const std::string& current = options[safeIndex];
    float ddAscent = m_canvas.fontAscent(0);
    float ddDescent = std::abs(m_canvas.fontDescent(0));
    float textY = buttonY + h * 0.5f + (ddAscent - ddDescent) * 0.5f;
    m_canvas.text(current.c_str(), buttonX + m_style.padding, textY, m_style.text, 0);

    // Draw arrow
    const char* arrow = isOpen ? "▲" : "▼";
    float arrowW = m_canvas.measureText(arrow, 0);
    m_canvas.text(arrow, buttonX + buttonW - arrowW - m_style.padding, textY, m_style.textDim, 0);

    // Toggle open/close
    if (hovered && m_mouseClicked) {
        if (isOpen) {
            s_state.openDropdown = 0;
        } else {
            s_state.openDropdown = id;
        }
    }

    bool changed = false;

    // Draw menu if open
    if (isOpen) {
        m_canvas.setLayer(UILayer::Menus);

        float menuY = buttonY + h;
        float menuH = options.size() * h;

        // Menu background
        m_canvas.fillRoundedRect(buttonX, menuY, buttonW, menuH, m_style.cornerRadius, m_style.panelBackground);
        m_canvas.strokeRoundedRect(buttonX, menuY, buttonW, menuH, m_style.cornerRadius, m_style.borderWidth, m_style.panelBorder);

        // Menu items
        float itemAscent = m_canvas.fontAscent(0);
        float itemDescent = std::abs(m_canvas.fontDescent(0));
        for (size_t i = 0; i < options.size(); ++i) {
            float itemY = menuY + i * h;
            bool itemHovered = isMouseInRect(buttonX, itemY, buttonW, h);

            if (itemHovered) {
                m_canvas.fillRoundedRect(buttonX + 2, itemY + 2, buttonW - 4, h - 4, m_style.cornerRadius, m_style.widgetHover);
            }

            float itemTextY = itemY + h * 0.5f + (itemAscent - itemDescent) * 0.5f;

            // Checkmark for selected
            if (static_cast<int>(i) == safeIndex) {
                m_canvas.text("✓", buttonX + m_style.padding, itemTextY, m_style.checkmark, 0);
            }

            m_canvas.text(options[i].c_str(), buttonX + m_style.padding * 2.5f, itemTextY, m_style.text, 0);

            // Select on click
            if (itemHovered && m_mouseClicked) {
                *index = static_cast<int>(i);
                s_state.openDropdown = 0;
                changed = true;
            }
        }

        // Close if clicked outside
        if (m_mouseClicked && !isMouseInRect(buttonX, buttonY, buttonW, h + menuH)) {
            s_state.openDropdown = 0;
        }

        m_canvas.setLayer(UILayer::Panels);
    }

    advanceCursor(totalHeight);
    return changed;
}

bool Gui::colorPicker(const char* label, glm::vec4* color) {
    if (!m_inPanel || !color) return false;

    // For now, just show a color swatch - full HSV picker can be added later
    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();
    float h = m_style.widgetHeight;

    float labelW = m_style.labelWidth;
    float swatchX = x + labelW;
    float swatchW = h;  // Square swatch

    bool hovered = isMouseInRect(swatchX, y, swatchW, h);
    bool isExpanded = (s_state.expandedColorPicker == id);

    // Draw label
    drawLabel(x, y, label);

    // Draw color swatch
    m_canvas.fillRoundedRect(swatchX, y + 2, swatchW - 4, h - 4, m_style.cornerRadius, *color);
    m_canvas.strokeRoundedRect(swatchX, y + 2, swatchW - 4, h - 4, m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Draw hex value
    char hexBuf[16];
    int r = static_cast<int>(std::clamp(color->r, 0.0f, 1.0f) * 255);
    int g = static_cast<int>(std::clamp(color->g, 0.0f, 1.0f) * 255);
    int b = static_cast<int>(std::clamp(color->b, 0.0f, 1.0f) * 255);
    snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", r, g, b);
    float cpAscent = m_canvas.fontAscent(0);
    float cpDescent = std::abs(m_canvas.fontDescent(0));
    float textY = y + h * 0.5f + (cpAscent - cpDescent) * 0.5f;
    m_canvas.text(hexBuf, swatchX + swatchW + m_style.padding, textY, m_style.textDim, 0);

    // TODO: Expand/collapse for full HSV picker
    // For now, clicking cycles through some preset colors
    bool changed = false;
    if (hovered && m_mouseClicked) {
        // Simple: cycle hue
        // Convert to HSV, rotate hue, convert back
        float maxC = std::max({color->r, color->g, color->b});
        float minC = std::min({color->r, color->g, color->b});
        float h_ = 0, s_ = 0, v_ = maxC;
        float delta = maxC - minC;
        if (delta > 0.001f) {
            s_ = delta / maxC;
            if (color->r >= maxC) h_ = (color->g - color->b) / delta;
            else if (color->g >= maxC) h_ = 2.0f + (color->b - color->r) / delta;
            else h_ = 4.0f + (color->r - color->g) / delta;
            h_ *= 60.0f;
            if (h_ < 0) h_ += 360.0f;
        }
        // Rotate hue by 30 degrees
        h_ = std::fmod(h_ + 30.0f, 360.0f);
        // Convert back to RGB
        float c = v_ * s_;
        float x_ = c * (1.0f - std::abs(std::fmod(h_ / 60.0f, 2.0f) - 1.0f));
        float m = v_ - c;
        float r_, g_, b_;
        if (h_ < 60) { r_ = c; g_ = x_; b_ = 0; }
        else if (h_ < 120) { r_ = x_; g_ = c; b_ = 0; }
        else if (h_ < 180) { r_ = 0; g_ = c; b_ = x_; }
        else if (h_ < 240) { r_ = 0; g_ = x_; b_ = c; }
        else if (h_ < 300) { r_ = x_; g_ = 0; b_ = c; }
        else { r_ = c; g_ = 0; b_ = x_; }
        color->r = r_ + m;
        color->g = g_ + m;
        color->b = b_ + m;
        changed = true;
    }

    advanceCursor(h);
    return changed;
}

// -------------------------------------------------------------------------
// ID Scoping
// -------------------------------------------------------------------------

void Gui::pushId(const char* id) {
    m_idStack.push_back(id);
}

void Gui::popId() {
    if (!m_idStack.empty()) {
        m_idStack.pop_back();
    }
}

// -------------------------------------------------------------------------
// Scroll Control
// -------------------------------------------------------------------------

void Gui::beginScrollArea(float height, float contentHeight, float* scrollOffset) {
    if (!m_inPanel || !scrollOffset) return;

    m_scrollArea.active = true;
    m_scrollArea.visibleTop = m_panel.cursorY;
    m_scrollArea.visibleBottom = m_panel.cursorY + height;
    m_scrollArea.scrollOffset = scrollOffset;

    // Handle scroll input if mouse is in scroll area
    float x = m_panel.x;
    float y = m_panel.cursorY;
    float w = m_panel.w;

    if (isMouseInRect(x, y, w, height) && (m_input.scroll.y != 0.0f)) {
        *scrollOffset -= m_input.scroll.y * 30.0f;
        float maxScroll = std::max(0.0f, contentHeight - height);
        *scrollOffset = std::clamp(*scrollOffset, 0.0f, maxScroll);
    }

    // Apply scroll offset to cursor position
    m_panel.cursorY -= *scrollOffset;

    // Set up clipping for scroll area
    m_canvas.beginClipRect(x, y, w, height);
}

void Gui::endScrollArea() {
    if (m_scrollArea.active) {
        m_canvas.endClipRect();

        // Restore cursor position
        if (m_scrollArea.scrollOffset) {
            m_panel.cursorY += *m_scrollArea.scrollOffset;
        }

        m_scrollArea.active = false;
        m_scrollArea.scrollOffset = nullptr;
    }
}

bool Gui::isLastWidgetVisible() const {
    if (!m_scrollArea.active) {
        return true;  // Not in scroll area, always visible
    }
    // Widget is visible if it overlaps with visible region
    return m_lastWidgetBottom > m_scrollArea.visibleTop &&
           m_lastWidgetTop < m_scrollArea.visibleBottom;
}

// -------------------------------------------------------------------------
// Color Picker HSV
// -------------------------------------------------------------------------

Gui::ColorPickerResult Gui::colorPickerHSV(const char* label, glm::vec4* color, bool* expanded) {
    ColorPickerResult result;
    if (!m_inPanel || !color || !expanded) return result;

    const float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    const float w = contentWidth();

    uint32_t id = hashId(label);

    // Swatch dimensions
    const float swatchHeight = m_style.widgetHeight;
    const float swatchWidth = 50.0f;

    // Track widget bounds for visibility
    m_lastWidgetTop = y;
    m_lastWidgetBottom = y + swatchHeight;

    // Label
    drawLabel(x, y, label);
    y += m_canvas.fontLineHeight(0) + 2.0f;

    // Color swatch with checkerboard alpha preview
    float swatchX = x;
    float swatchY = y;

    // Draw checkerboard pattern for alpha visualization
    float checkSize = 6.0f;
    glm::vec4 checkLight = {0.7f, 0.7f, 0.7f, 1.0f};
    glm::vec4 checkDark = {0.4f, 0.4f, 0.4f, 1.0f};
    for (float cx = swatchX; cx < swatchX + swatchWidth; cx += checkSize) {
        for (float cy = swatchY; cy < swatchY + swatchHeight; cy += checkSize) {
            int xi = static_cast<int>((cx - swatchX) / checkSize);
            int yi = static_cast<int>((cy - swatchY) / checkSize);
            bool dark = (xi + yi) % 2 == 0;
            float cw = std::min(checkSize, swatchX + swatchWidth - cx);
            float ch = std::min(checkSize, swatchY + swatchHeight - cy);
            m_canvas.fillRect(cx, cy, cw, ch, dark ? checkDark : checkLight);
        }
    }

    // Draw color on top
    m_canvas.fillRoundedRect(swatchX, swatchY, swatchWidth, swatchHeight,
                             m_style.cornerRadius, *color);
    m_canvas.strokeRoundedRect(swatchX, swatchY, swatchWidth, swatchHeight,
                               m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Hex value display
    char hexBuf[16];
    int ri = static_cast<int>(std::round(color->r * 255.0f));
    int gi = static_cast<int>(std::round(color->g * 255.0f));
    int bi = static_cast<int>(std::round(color->b * 255.0f));
    ri = std::clamp(ri, 0, 255);
    gi = std::clamp(gi, 0, 255);
    bi = std::clamp(bi, 0, 255);
    snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", ri, gi, bi);
    m_canvas.text(hexBuf, swatchX + swatchWidth + 8.0f, swatchY + m_canvas.fontAscent(0),
                  m_style.text, 0);

    // Expand/collapse arrow
    const char* arrow = *expanded ? "▲" : "▼";
    m_canvas.text(arrow, x + w - 20.0f, swatchY + m_canvas.fontAscent(0), m_style.textDim, 0);

    // Handle click on swatch area to expand/collapse
    bool inSwatch = isMouseInRect(swatchX, swatchY, w, swatchHeight);
    if (m_mouseClicked && inSwatch) {
        *expanded = !*expanded;
    }

    float totalHeight = swatchHeight + m_canvas.fontLineHeight(0) + 4.0f;

    // If expanded, show H/S/V/A sliders
    if (*expanded) {
        float sliderY = swatchY + swatchHeight + m_style.spacing;
        const float sliderHeight = m_style.widgetHeight;
        const float sliderWidth = w - m_style.valueWidth - m_style.padding;

        // Convert RGB to HSV
        float h, s, v;
        rgbToHsv(color->r, color->g, color->b, h, s, v);

        const char* hsvLabels[] = {"H", "S", "V", "A"};
        float hsvValues[] = {h / 360.0f, s, v, color->a};

        for (int c = 0; c < 4; ++c) {
            uint32_t sliderId = id + c + 1;  // Unique ID for each slider

            // Label
            m_canvas.text(hsvLabels[c], x, sliderY + m_canvas.fontAscent(0), m_style.textDim, 0);

            float barX = x;
            float barY = sliderY + m_canvas.fontLineHeight(0) + 2.0f;

            // Slider background
            m_canvas.fillRoundedRect(barX, barY, sliderWidth, sliderHeight,
                                     m_style.cornerRadius, m_style.widgetBackground);

            // For Hue slider, draw rainbow gradient
            if (c == 0) {
                float gradW = sliderWidth / 6.0f;
                for (int i = 0; i < 6; ++i) {
                    float hStart = i * 60.0f;
                    float r1, g1, b1;
                    hsvToRgb(hStart, 1.0f, 1.0f, r1, g1, b1);
                    glm::vec4 hueColor = {r1, g1, b1, 1.0f};
                    m_canvas.fillRect(barX + i * gradW, barY, gradW + 1.0f, sliderHeight, hueColor);
                }
            } else {
                // Fill based on normalized value
                float fillWidth = hsvValues[c] * sliderWidth;
                if (fillWidth > 0) {
                    bool isActive = s_state.activeColorSlider == sliderId;
                    glm::vec4 fillColor = isActive ? m_style.sliderFillActive : m_style.sliderFill;
                    m_canvas.fillRoundedRect(barX, barY, fillWidth, sliderHeight,
                                             m_style.cornerRadius, fillColor);
                }
            }

            // Value display
            char valBuf[16];
            if (c == 0) {
                snprintf(valBuf, sizeof(valBuf), "%.0f°", h);
            } else if (c == 3) {
                snprintf(valBuf, sizeof(valBuf), "%.0f%%", color->a * 100.0f);
            } else {
                snprintf(valBuf, sizeof(valBuf), "%.0f%%", hsvValues[c] * 100.0f);
            }
            m_canvas.text(valBuf, barX + sliderWidth + 8.0f, barY + m_canvas.fontAscent(0),
                          m_style.text, 0);

            // Handle slider interaction
            bool inSlider = isMouseInRect(barX, barY, sliderWidth, sliderHeight);

            // Start drag
            if (m_mouseClicked && inSlider) {
                s_state.activeColorSlider = sliderId;
                s_state.colorStartValue = *color;
                result.dragStarted = true;
                result.startColor = *color;
            }

            // Continue drag
            if (s_state.activeColorSlider == sliderId) {
                float newNormalized = (m_mousePos.x - barX) / sliderWidth;
                newNormalized = std::clamp(newNormalized, 0.0f, 1.0f);

                // Update HSV value and convert back to RGB
                float newH = h, newS = s, newV = v, newA = color->a;
                if (c == 0) newH = newNormalized * 360.0f;
                else if (c == 1) newS = newNormalized;
                else if (c == 2) newV = newNormalized;
                else newA = newNormalized;

                float newR, newG, newB;
                hsvToRgb(newH, newS, newV, newR, newG, newB);

                color->r = newR;
                color->g = newG;
                color->b = newB;
                color->a = newA;
                result.changed = true;

                // End drag
                if (m_mouseReleased) {
                    s_state.activeColorSlider = 0;
                    result.dragEnded = true;
                    result.startColor = s_state.colorStartValue;
                    m_colorDragEnded = true;
                    m_lastColorDragId = sliderId;
                }
            }

            sliderY += m_canvas.fontLineHeight(0) + sliderHeight + m_style.spacing + 4.0f;
        }

        totalHeight = sliderY - swatchY;
    }

    advanceCursor(totalHeight + m_style.spacing);
    return result;
}

// -------------------------------------------------------------------------
// XY Pad
// -------------------------------------------------------------------------

Gui::XYPadResult Gui::xyPad(const char* label, glm::vec2* value, float min, float max, float size) {
    return xyPadEx(label, value, min, max, min, max, size);
}

Gui::XYPadResult Gui::xyPadEx(const char* label, glm::vec2* value,
                               float minX, float maxX, float minY, float maxY, float size) {
    XYPadResult result;
    if (!m_inPanel || !value) return result;

    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();

    // Label height
    float labelH = m_canvas.fontLineHeight(0);
    if (labelH <= 0) labelH = 16.0f;

    // Pad size: default to widgetHeight * 3.5 or 60% of width, whichever is smaller
    float padSize = size > 0 ? size : std::min(m_style.widgetHeight * 2.5f, w * 0.6f);

    // Layout positions
    float padX = x;
    float padY = y + labelH + 4.0f;

    // Value display area (to the right of pad)
    float valueDisplayX = padX + padSize + m_style.padding;

    // Total height
    float totalHeight = labelH + 4.0f + padSize;

    // Track widget bounds
    m_lastWidgetTop = y;
    m_lastWidgetBottom = y + totalHeight;

    // Draw label
    float baseline = y + m_canvas.fontAscent(0);
    m_canvas.text(label, x, baseline, m_style.textDim, 0);

    // Draw pad background
    m_canvas.fillRoundedRect(padX, padY, padSize, padSize,
                             m_style.cornerRadius, m_style.widgetBackground);

    // Calculate normalized position (0-1)
    float rangeX = maxX - minX;
    float rangeY = maxY - minY;
    float normX = rangeX > 0 ? std::clamp((value->x - minX) / rangeX, 0.0f, 1.0f) : 0.5f;
    float normY = rangeY > 0 ? std::clamp((value->y - minY) / rangeY, 0.0f, 1.0f) : 0.5f;

    // Position in pad coordinates (Y is inverted: top = max, bottom = min)
    float indicatorX = padX + normX * padSize;
    float indicatorY = padY + (1.0f - normY) * padSize;

    // Hit testing and drag tracking
    bool hovered = isMouseInRect(padX, padY, padSize, padSize);
    bool active = (s_state.activeXYPad == id);

    // Start drag
    if (hovered && m_mouseClicked) {
        s_state.activeXYPad = id;
        s_state.xyPadStartValue = *value;
        active = true;
        result.dragStarted = true;
        result.startValue = *value;
    }

    // Continue drag
    if (active) {
        result.startValue = s_state.xyPadStartValue;

        if (m_input.mouseDown[0]) {
            // Calculate new values from mouse position
            float tX = std::clamp((m_mousePos.x - padX) / padSize, 0.0f, 1.0f);
            float tY = std::clamp((m_mousePos.y - padY) / padSize, 0.0f, 1.0f);

            float newX = minX + tX * rangeX;
            float newY = maxY - tY * rangeY;  // Invert Y: top = max

            if (newX != value->x || newY != value->y) {
                value->x = newX;
                value->y = newY;
                result.changed = true;
            }

            // Update indicator position after change
            normX = rangeX > 0 ? std::clamp((value->x - minX) / rangeX, 0.0f, 1.0f) : 0.5f;
            normY = rangeY > 0 ? std::clamp((value->y - minY) / rangeY, 0.0f, 1.0f) : 0.5f;
            indicatorX = padX + normX * padSize;
            indicatorY = padY + (1.0f - normY) * padSize;
        } else {
            // End drag
            s_state.activeXYPad = 0;
            result.dragEnded = true;
            m_xyPadDragEnded = true;
            m_lastXYPadDragId = id;
        }
    }

    // Draw crosshairs
    glm::vec4 crosshairColor = active ? m_style.sliderFillActive : m_style.sliderFill;
    glm::vec4 lineColor = {crosshairColor.r, crosshairColor.g, crosshairColor.b, 0.4f};

    // Vertical line
    m_canvas.line(indicatorX, padY, indicatorX, padY + padSize, 1.0f, lineColor);
    // Horizontal line
    m_canvas.line(padX, indicatorY, padX + padSize, indicatorY, 1.0f, lineColor);

    // Draw indicator circle at intersection
    float indicatorRadius = 5.0f;
    m_canvas.fillCircle(indicatorX, indicatorY, indicatorRadius, crosshairColor, 16);
    m_canvas.strokeCircle(indicatorX, indicatorY, indicatorRadius, 1.0f, m_style.widgetBorder, 16);

    // Draw border
    m_canvas.strokeRoundedRect(padX, padY, padSize, padSize,
                               m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Draw value labels to the right
    char xBuf[32], yBuf[32];
    snprintf(xBuf, sizeof(xBuf), "X: %.2f", value->x);
    snprintf(yBuf, sizeof(yBuf), "Y: %.2f", value->y);
    float textY1 = padY + padSize * 0.33f;
    float textY2 = padY + padSize * 0.66f;
    float xyAscent = m_canvas.fontAscent(0);
    float xyDescent = std::abs(m_canvas.fontDescent(0));
    float xyOffset = (xyAscent - xyDescent) * 0.5f;
    m_canvas.text(xBuf, valueDisplayX, textY1 + xyOffset, m_style.text, 0);
    m_canvas.text(yBuf, valueDisplayX, textY2 + xyOffset, m_style.text, 0);

    advanceCursor(totalHeight);
    return result;
}

// -------------------------------------------------------------------------
// Vec3 Row
// -------------------------------------------------------------------------

Gui::Vec3RowResult Gui::vec3Row(const char* label, glm::vec3* value, float min, float max) {
    return vec3RowEx(label, value, glm::vec3(min), glm::vec3(max));
}

Gui::Vec3RowResult Gui::vec3RowEx(const char* label, glm::vec3* value,
                                   const glm::vec3& mins, const glm::vec3& maxs) {
    Vec3RowResult result;
    if (!m_inPanel || !value) return result;

    uint32_t baseId = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();

    // Layout constants
    float labelH = m_canvas.fontLineHeight(0);
    if (labelH <= 0) labelH = 16.0f;

    float sliderH = m_style.widgetHeight - 4.0f;
    float componentLabelH = labelH * 0.8f;

    // Three sliders with spacing between them
    float spacing = 4.0f;
    float sliderW = (w - spacing * 2) / 3.0f;

    // Total height: main label + component labels + sliders
    float totalHeight = labelH + 2.0f + componentLabelH + 2.0f + sliderH;

    // Starting Y positions
    float mainLabelY = y;
    float componentLabelsY = y + labelH + 2.0f;
    float slidersY = componentLabelsY + componentLabelH + 2.0f;

    // Track widget bounds
    m_lastWidgetTop = y;
    m_lastWidgetBottom = y + totalHeight;

    // Draw main label
    float mainBaseline = mainLabelY + m_canvas.fontAscent(0);
    m_canvas.text(label, x, mainBaseline, m_style.textDim, 0);

    const char* componentLabels[] = {"X", "Y", "Z"};
    const glm::vec4 componentColors[] = {
        {0.9f, 0.5f, 0.5f, 1.0f},  // X = red-ish
        {0.5f, 0.9f, 0.5f, 1.0f},  // Y = green-ish
        {0.5f, 0.6f, 0.9f, 1.0f}   // Z = blue-ish
    };

    for (int c = 0; c < 3; ++c) {
        uint32_t sliderId = baseId + c + 1;
        float sliderX = x + c * (sliderW + spacing);

        // Component label (X/Y/Z) with color hint
        float compBaseline = componentLabelsY + m_canvas.fontAscent(0) * 0.8f;
        m_canvas.text(componentLabels[c], sliderX, compBaseline, componentColors[c], 0);

        // Mini-slider background
        m_canvas.fillRoundedRect(sliderX, slidersY, sliderW, sliderH,
                                 m_style.cornerRadius, m_style.widgetBackground);

        // Calculate fill
        float range = maxs[c] - mins[c];
        float norm = range > 0 ? std::clamp(((*value)[c] - mins[c]) / range, 0.0f, 1.0f) : 0.0f;
        float fillW = norm * sliderW;

        // Hit testing
        bool hovered = isMouseInRect(sliderX, slidersY, sliderW, sliderH);
        bool active = (s_state.activeVec3Slider == sliderId);

        // Start drag
        if (hovered && m_mouseClicked) {
            s_state.activeVec3Slider = sliderId;
            s_state.vec3ActiveComponent = c;
            s_state.vec3StartValue = *value;
            active = true;
            result.dragStarted = true;
            result.startValue = *value;
            result.activeComponent = c;
        }

        // Continue drag
        if (active) {
            result.startValue = s_state.vec3StartValue;
            result.activeComponent = c;

            if (m_input.mouseDown[0]) {
                float t = std::clamp((m_mousePos.x - sliderX) / sliderW, 0.0f, 1.0f);
                float newVal = mins[c] + t * range;

                if (newVal != (*value)[c]) {
                    (*value)[c] = newVal;
                    result.changed = true;
                    // Update fill after change
                    norm = range > 0 ? std::clamp(((*value)[c] - mins[c]) / range, 0.0f, 1.0f) : 0.0f;
                    fillW = norm * sliderW;
                }
            } else {
                // End drag
                s_state.activeVec3Slider = 0;
                s_state.vec3ActiveComponent = -1;
                result.dragEnded = true;
                m_vec3DragEnded = true;
                m_lastVec3DragId = sliderId;
            }
        }

        // Draw fill
        if (fillW > 0) {
            glm::vec4 fillColor = active ? m_style.sliderFillActive : m_style.sliderFill;
            m_canvas.fillRoundedRect(sliderX, slidersY, fillW, sliderH,
                                     m_style.cornerRadius, fillColor);
        }

        // Border
        m_canvas.strokeRoundedRect(sliderX, slidersY, sliderW, sliderH,
                                   m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

        // Value text (centered, use 1 decimal for compact display)
        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.1f", (*value)[c]);
        float textW = m_canvas.measureText(valBuf, 0);
        float textX = sliderX + (sliderW - textW) * 0.5f;
        float v3Ascent = m_canvas.fontAscent(0);
        float v3Descent = std::abs(m_canvas.fontDescent(0));
        float textY = slidersY + sliderH * 0.5f + (v3Ascent - v3Descent) * 0.5f;
        m_canvas.text(valBuf, textX, textY, m_style.text, 0);
    }

    advanceCursor(totalHeight);
    return result;
}

// -------------------------------------------------------------------------
// ADSR Envelope Widget
// -------------------------------------------------------------------------

Gui::ADSRResult Gui::adsrEnvelope(const char* label,
                                   float* attack, float* decay,
                                   float* sustain, float* release,
                                   float maxTime) {
    ADSRResult result;
    if (!m_inPanel || !attack || !decay || !sustain || !release) return result;

    uint32_t id = hashId(label);
    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();

    // Label height
    float labelH = m_canvas.fontLineHeight(0);
    if (labelH <= 0) labelH = 16.0f;

    // Graph dimensions
    float graphH = 60.0f;
    float graphY = y + labelH + 4.0f;

    // Mini-sliders below graph
    float slidersY = graphY + graphH + 6.0f;
    float sliderH = m_style.widgetHeight * 0.8f;
    float sliderSpacing = 4.0f;
    float sliderW = (w - 3 * sliderSpacing) / 4.0f;

    // Total height
    float totalHeight = labelH + 4.0f + graphH + 6.0f + sliderH + 4.0f;

    // Track widget bounds
    m_lastWidgetTop = y;
    m_lastWidgetBottom = y + totalHeight;

    // Draw label
    float baseline = y + m_canvas.fontAscent(0);
    m_canvas.text(label, x, baseline, m_style.textDim, 0);

    // Draw graph background
    m_canvas.fillRoundedRect(x, graphY, w, graphH, m_style.cornerRadius, m_style.widgetBackground);

    // Calculate envelope curve points using FIXED scale
    // This prevents the graph from rescaling during drag, making interaction intuitive
    float sustainDisplayTime = maxTime * 0.1f;  // Fixed sustain display width
    float fixedTotalTime = maxTime * 3.0f + sustainDisplayTime;  // A + D + S + R at max
    float timeScale = w / fixedTotalTime;

    // X positions for envelope points
    float x0 = x;                                           // Start (0, 0)
    float x1 = x + (*attack) * timeScale;                   // End of attack (peak)
    float x2 = x1 + (*decay) * timeScale;                   // End of decay (sustain start)
    float x3 = x2 + sustainDisplayTime * timeScale;         // End of sustain
    float x4 = x3 + (*release) * timeScale;                 // End of release

    // Clamp to graph bounds
    x1 = std::min(x1, x + w);
    x2 = std::min(x2, x + w);
    x3 = std::min(x3, x + w);
    x4 = std::min(x4, x + w);

    // Y positions (inverted: top = 1, bottom = 0)
    float graphBottom = graphY + graphH - 4.0f;
    float graphTop = graphY + 4.0f;
    float graphRange = graphBottom - graphTop;

    float y0 = graphBottom;                                 // Start at 0
    float y1 = graphTop;                                    // Peak at 1
    float y2 = graphTop + (1.0f - *sustain) * graphRange;   // Sustain level
    float y3 = y2;                                          // Sustain continues
    float y4 = graphBottom;                                 // End at 0

    // Draw envelope curve
    glm::vec4 curveColor = {0.4f, 0.7f, 0.9f, 1.0f};
    glm::vec4 curveColorDim = {0.3f, 0.5f, 0.7f, 0.6f};

    // Fill under curve
    m_canvas.fillTriangle({x0, y0}, {x1, y1}, {x1, y0}, curveColorDim);  // Attack triangle
    m_canvas.fillTriangle({x1, y1}, {x2, y2}, {x1, y0}, curveColorDim);  // Decay part 1
    m_canvas.fillTriangle({x2, y2}, {x2, y0}, {x1, y0}, curveColorDim);  // Decay part 2
    m_canvas.fillRect(x2, y2, x3 - x2, y0 - y2, curveColorDim);          // Sustain
    m_canvas.fillTriangle({x3, y2}, {x4, y4}, {x3, y0}, curveColorDim);  // Release

    // Draw curve lines
    float lineWidth = 2.0f;
    m_canvas.line(x0, y0, x1, y1, lineWidth, curveColor);  // Attack
    m_canvas.line(x1, y1, x2, y2, lineWidth, curveColor);  // Decay
    m_canvas.line(x2, y2, x3, y3, lineWidth, curveColor);  // Sustain
    m_canvas.line(x3, y3, x4, y4, lineWidth, curveColor);  // Release

    // Draw control points
    float pointRadius = 5.0f;
    glm::vec4 pointColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 pointColorActive = {1.0f, 0.8f, 0.3f, 1.0f};

    bool active = (s_state.activeADSR == id);
    int activeComp = s_state.adsrActiveComponent;

    // Control point positions and hit areas
    struct ControlPoint {
        float cx, cy;
        int component;  // 0=A, 1=D, 2=S, 3=R
    };
    ControlPoint points[] = {
        {x1, y1, 0},  // Attack (peak point)
        {x2, y2, 1},  // Decay end / Sustain start
        {(x2 + x3) * 0.5f, y2, 2},  // Sustain level (middle of sustain line)
        {x4, y4, 3},  // Release end
    };

    // Hit testing for control points
    int hoveredPoint = -1;
    for (int i = 0; i < 4; ++i) {
        float dx = m_mousePos.x - points[i].cx;
        float dy = m_mousePos.y - points[i].cy;
        if (dx * dx + dy * dy < 100.0f) {  // 10px radius
            hoveredPoint = i;
            break;
        }
    }

    // Start drag on click
    if (hoveredPoint >= 0 && m_mouseClicked) {
        s_state.activeADSR = id;
        s_state.adsrActiveComponent = hoveredPoint;
        s_state.adsrStartA = *attack;
        s_state.adsrStartD = *decay;
        s_state.adsrStartS = *sustain;
        s_state.adsrStartR = *release;
        result.dragStarted = true;
        result.startA = *attack;
        result.startD = *decay;
        result.startS = *sustain;
        result.startR = *release;
        active = true;
        activeComp = hoveredPoint;
    }

    // Continue drag
    if (active && s_state.activeADSR == id) {
        result.startA = s_state.adsrStartA;
        result.startD = s_state.adsrStartD;
        result.startS = s_state.adsrStartS;
        result.startR = s_state.adsrStartR;

        if (m_input.mouseDown[0]) {
            float mouseX = m_mousePos.x;
            float mouseY = m_mousePos.y;

            switch (activeComp) {
                case 0: {  // Attack - horizontal drag changes attack time
                    float newAttack = std::clamp((mouseX - x) / timeScale, 0.001f, maxTime);
                    if (newAttack != *attack) {
                        *attack = newAttack;
                        result.changed = true;
                    }
                    break;
                }
                case 1: {  // Decay - horizontal drag changes decay time
                    float decayStart = x + (*attack) * timeScale;
                    float newDecay = std::clamp((mouseX - decayStart) / timeScale, 0.001f, maxTime);
                    if (newDecay != *decay) {
                        *decay = newDecay;
                        result.changed = true;
                    }
                    break;
                }
                case 2: {  // Sustain - vertical drag changes sustain level
                    float newSustain = std::clamp(1.0f - (mouseY - graphTop) / graphRange, 0.0f, 1.0f);
                    if (newSustain != *sustain) {
                        *sustain = newSustain;
                        result.changed = true;
                    }
                    break;
                }
                case 3: {  // Release - horizontal drag changes release time
                    float releaseStart = x + (*attack + *decay + sustainDisplayTime) * timeScale;
                    float newRelease = std::clamp((mouseX - releaseStart) / timeScale, 0.001f, maxTime);
                    if (newRelease != *release) {
                        *release = newRelease;
                        result.changed = true;
                    }
                    break;
                }
            }
        } else {
            // End drag
            s_state.activeADSR = 0;
            s_state.adsrActiveComponent = -1;
            result.dragEnded = true;
            m_adsrDragEnded = true;
            m_lastADSRDragId = id;
        }
    }

    // Draw control points
    for (int i = 0; i < 4; ++i) {
        bool isActive = (active && activeComp == i);
        bool isHovered = (hoveredPoint == i);
        glm::vec4 color = isActive ? pointColorActive : (isHovered ? m_style.widgetHover : pointColor);
        float r = isActive ? pointRadius * 1.3f : (isHovered ? pointRadius * 1.1f : pointRadius);
        m_canvas.fillCircle(points[i].cx, points[i].cy, r, color);
    }

    // Draw graph border
    m_canvas.strokeRoundedRect(x, graphY, w, graphH, m_style.cornerRadius, m_style.borderWidth, m_style.widgetBorder);

    // Draw mini-sliders below graph
    const char* sliderLabels[] = {"A", "D", "S", "R"};
    float* sliderValues[] = {attack, decay, sustain, release};
    float sliderMins[] = {0.001f, 0.001f, 0.0f, 0.001f};
    float sliderMaxs[] = {maxTime, maxTime, 1.0f, maxTime};

    for (int i = 0; i < 4; ++i) {
        float sliderX = x + i * (sliderW + sliderSpacing);

        // Slider background
        m_canvas.fillRoundedRect(sliderX, slidersY, sliderW, sliderH,
                                 m_style.cornerRadius * 0.5f, m_style.widgetBackground);

        // Slider fill
        float range = sliderMaxs[i] - sliderMins[i];
        float fillRatio = range > 0 ? std::clamp((*sliderValues[i] - sliderMins[i]) / range, 0.0f, 1.0f) : 0.0f;
        float fillW = fillRatio * (sliderW - 4.0f);
        if (fillW > 0) {
            m_canvas.fillRoundedRect(sliderX + 2.0f, slidersY + 2.0f, fillW, sliderH - 4.0f,
                                     m_style.cornerRadius * 0.5f, m_style.sliderFill);
        }

        // Draw label and value centered, label on left half, value on right half
        float adsrAscent = m_canvas.fontAscent(0);
        float adsrDescent = std::abs(m_canvas.fontDescent(0));
        float labelY = slidersY + sliderH * 0.5f + (adsrAscent - adsrDescent) * 0.5f;

        // Label (left-aligned in left portion)
        float labelX = sliderX + 3.0f;
        m_canvas.text(sliderLabels[i], labelX, labelY, m_style.textDim, 0);

        // Value (right-aligned) - use shorter format to avoid overlap
        char valBuf[16];
        if (i == 2) {  // Sustain is 0-1
            snprintf(valBuf, sizeof(valBuf), ".%d", static_cast<int>(*sliderValues[i] * 100));
        } else {  // Times in seconds - compact format
            if (*sliderValues[i] < 0.1f) {
                snprintf(valBuf, sizeof(valBuf), "%dms", static_cast<int>(*sliderValues[i] * 1000));
            } else {
                snprintf(valBuf, sizeof(valBuf), "%.1f", *sliderValues[i]);
            }
        }
        float textW = m_canvas.measureText(valBuf, 0);
        float textX = sliderX + sliderW - textW - 3.0f;
        m_canvas.text(valBuf, textX, labelY, m_style.text, 0);

        // Mini-slider interaction
        if (isMouseInRect(sliderX, slidersY, sliderW, sliderH)) {
            if (m_mouseClicked) {
                // Start drag on mini-slider
                s_state.activeADSR = id;
                s_state.adsrActiveComponent = i + 10;  // Offset to distinguish from graph points
                s_state.adsrStartA = *attack;
                s_state.adsrStartD = *decay;
                s_state.adsrStartS = *sustain;
                s_state.adsrStartR = *release;
                result.dragStarted = true;
                result.startA = *attack;
                result.startD = *decay;
                result.startS = *sustain;
                result.startR = *release;
            }
        }

        // Handle mini-slider drag
        if (s_state.activeADSR == id && s_state.adsrActiveComponent == i + 10) {
            if (m_input.mouseDown[0]) {
                float t = std::clamp((m_mousePos.x - sliderX) / sliderW, 0.0f, 1.0f);
                float newVal = sliderMins[i] + t * range;
                if (newVal != *sliderValues[i]) {
                    *sliderValues[i] = newVal;
                    result.changed = true;
                }
            } else {
                s_state.activeADSR = 0;
                s_state.adsrActiveComponent = -1;
                result.dragEnded = true;
                m_adsrDragEnded = true;
                m_lastADSRDragId = id;
            }
        }

        // Border
        m_canvas.strokeRoundedRect(sliderX, slidersY, sliderW, sliderH,
                                   m_style.cornerRadius * 0.5f, m_style.borderWidth * 0.5f, m_style.widgetBorder);
    }

    advanceCursor(totalHeight);
    return result;
}

// -------------------------------------------------------------------------
// Graph Widget
// -------------------------------------------------------------------------

Gui::GraphResult Gui::graph(const char* label, const float* data, size_t count,
                             const GraphConfig& config, float height) {
    GraphSeries series;
    series.data = data;
    series.count = count;
    series.offset = 0;
    return graph(label, &series, 1, config, height);
}

Gui::GraphResult Gui::graph(const char* label, const GraphSeries* series, size_t seriesCount,
                             const GraphConfig& config, float height) {
    GraphResult result;
    if (!m_inPanel || !series || seriesCount == 0) return result;

    float x = m_panel.x + m_style.padding;
    float y = m_panel.cursorY;
    float w = contentWidth();

    // Label height
    float labelH = m_canvas.fontLineHeight(0);
    if (labelH <= 0) labelH = 16.0f;

    // Graph dimensions
    float graphH = height > 0 ? height : 60.0f;
    float graphY = y + labelH + 4.0f;

    // Account for Y-axis labels on the left
    float yLabelWidth = config.showYLabels ? 40.0f : 0.0f;
    float graphX = x + yLabelWidth;
    float graphW = w - yLabelWidth;

    // Total height
    float totalHeight = labelH + 4.0f + graphH;

    // Track widget bounds
    m_lastWidgetTop = y;
    m_lastWidgetBottom = y + totalHeight;

    // Draw label
    float baseline = y + m_canvas.fontAscent(0);
    m_canvas.text(label, x, baseline, m_style.textDim, 0);

    // Calculate Y range
    float yMin = config.yMin;
    float yMax = config.yMax;

    if (config.autoScaleY) {
        yMin = std::numeric_limits<float>::max();
        yMax = std::numeric_limits<float>::lowest();

        for (size_t s = 0; s < seriesCount; ++s) {
            const GraphSeries& ser = series[s];
            if (!ser.data || ser.count == 0) continue;

            // For ring buffers: offset tells us where oldest element is
            // Physical data layout: [newest-n ... newest | oldest ... oldest+n]
            //                                   ^offset
            // Logical iteration: start at offset, wrap at count
            for (size_t i = 0; i < ser.count; ++i) {
                // Logical index i maps to physical (offset + i) % count
                size_t physIdx = (ser.offset + i) % ser.count;
                float val = ser.data[physIdx];
                if (val < yMin) yMin = val;
                if (val > yMax) yMax = val;
            }
        }

        // Add 10% padding
        float range = yMax - yMin;
        if (range < 0.001f) range = 1.0f;
        yMin -= range * 0.05f;
        yMax += range * 0.05f;
    }

    float yRange = yMax - yMin;
    if (yRange < 0.001f) yRange = 1.0f;

    // Draw graph background
    m_canvas.fillRoundedRect(graphX, graphY, graphW, graphH,
                             m_style.cornerRadius, m_style.graphBackground);

    // Draw grid lines
    if (config.showGrid) {
        const int gridLines = 4;
        for (int i = 0; i <= gridLines; ++i) {
            float t = static_cast<float>(i) / gridLines;
            float lineY = graphY + graphH - t * graphH;
            m_canvas.line(graphX, lineY, graphX + graphW, lineY, 1.0f, m_style.graphGrid);
        }
    }

    // Draw Y-axis labels
    if (config.showYLabels) {
        char labelBuf[32];
        float textAscent = m_canvas.fontAscent(0);

        // Min label
        snprintf(labelBuf, sizeof(labelBuf), config.yFormat, yMin);
        m_canvas.text(labelBuf, x, graphY + graphH - 2, m_style.textDim, 0);

        // Max label
        snprintf(labelBuf, sizeof(labelBuf), config.yFormat, yMax);
        m_canvas.text(labelBuf, x, graphY + textAscent, m_style.textDim, 0);
    }

    // Check hover state
    result.hovered = isMouseInRect(graphX, graphY, graphW, graphH);

    // Draw each series
    for (size_t s = 0; s < seriesCount; ++s) {
        const GraphSeries& ser = series[s];
        if (!ser.data || ser.count < 2) continue;

        // Build polyline points
        std::vector<glm::vec2> points;
        points.reserve(ser.count);

        for (size_t i = 0; i < ser.count; ++i) {
            // Ring buffer: logical index i maps to physical (offset + i) % count
            size_t physIdx = (ser.offset + i) % ser.count;
            float val = ser.data[physIdx];

            float px = graphX + (static_cast<float>(i) / (ser.count - 1)) * graphW;
            float py = graphY + graphH - ((val - yMin) / yRange) * graphH;

            // Clamp Y to graph bounds
            py = std::clamp(py, graphY, graphY + graphH);

            points.push_back({px, py});
        }

        // Draw filled area if requested
        if (ser.filled && points.size() >= 2) {
            std::vector<glm::vec2> fillPoints;
            fillPoints.reserve(points.size() + 2);

            // Start at bottom-left
            fillPoints.push_back({points.front().x, graphY + graphH});

            // Add all line points
            for (const auto& p : points) {
                fillPoints.push_back(p);
            }

            // End at bottom-right
            fillPoints.push_back({points.back().x, graphY + graphH});

            glm::vec4 fillColor = ser.color;
            fillColor.a *= 0.2f;
            m_canvas.fillPolygon(fillPoints, fillColor);
        }

        // Draw the line
        if (points.size() >= 2) {
            m_canvas.polyline(points, ser.lineWidth, ser.color, false);
        }
    }

    // Handle tooltip on hover
    if (result.hovered && config.showTooltip && seriesCount > 0) {
        const GraphSeries& ser = series[0];  // Use first series for tooltip
        if (ser.data && ser.count > 0) {
            // Calculate which sample is under the mouse
            float relX = m_mousePos.x - graphX;
            float t = std::clamp(relX / graphW, 0.0f, 1.0f);
            int sampleIdx = static_cast<int>(t * (ser.count - 1) + 0.5f);
            sampleIdx = std::clamp(sampleIdx, 0, static_cast<int>(ser.count) - 1);

            // Get value at sample
            size_t dataIdx = sampleIdx;
            if (ser.offset > 0) {
                dataIdx = (ser.offset + sampleIdx) % ser.count;
            }
            float val = ser.data[dataIdx];

            result.hoveredSampleIndex = sampleIdx;
            result.hoveredValue = val;

            // Draw tooltip
            m_canvas.setLayer(UILayer::Tooltips);

            char tooltipBuf[32];
            snprintf(tooltipBuf, sizeof(tooltipBuf), config.yFormat, val);

            float tooltipW = m_canvas.measureText(tooltipBuf, 0) + 8.0f;
            float tooltipH = labelH + 4.0f;
            float tooltipX = m_mousePos.x - tooltipW / 2;
            float tooltipY = m_mousePos.y - tooltipH - 4.0f;

            // Keep tooltip in bounds
            tooltipX = std::clamp(tooltipX, graphX, graphX + graphW - tooltipW);
            if (tooltipY < graphY) tooltipY = m_mousePos.y + 8.0f;

            m_canvas.fillRoundedRect(tooltipX, tooltipY, tooltipW, tooltipH,
                                     m_style.cornerRadius, m_style.panelBackground);
            m_canvas.strokeRoundedRect(tooltipX, tooltipY, tooltipW, tooltipH,
                                       m_style.cornerRadius, 1.0f, m_style.panelBorder);

            float textAscent = m_canvas.fontAscent(0);
            float textDescent = std::abs(m_canvas.fontDescent(0));
            float textY = tooltipY + tooltipH * 0.5f + (textAscent - textDescent) * 0.5f;
            m_canvas.text(tooltipBuf, tooltipX + 4.0f, textY, m_style.text, 0);

            // Draw indicator line
            float indicatorX = graphX + t * graphW;
            float indicatorY = graphY + graphH - ((val - yMin) / yRange) * graphH;
            indicatorY = std::clamp(indicatorY, graphY, graphY + graphH);

            m_canvas.setLayer(UILayer::Panels);
            m_canvas.line(indicatorX, graphY, indicatorX, graphY + graphH, 1.0f,
                          glm::vec4(1.0f, 1.0f, 1.0f, 0.3f));
            m_canvas.fillCircle(indicatorX, indicatorY, 3.0f, ser.color, 8);
        }
    }

    // Draw border
    m_canvas.strokeRoundedRect(graphX, graphY, graphW, graphH,
                               m_style.cornerRadius, m_style.borderWidth, m_style.graphBorder);

    advanceCursor(totalHeight);
    return result;
}

} // namespace vivid
