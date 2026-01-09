// Immediate-mode GUI widget system
// Built on OverlayCanvas for efficient GPU rendering

#include <vivid/gui/gui.h>
#include <vivid/gui/ui_style.h>
#include <vivid/context.h>
#include <algorithm>
#include <cmath>

namespace vivid {

// Static interaction state (persists across frames)
Gui::InteractionState Gui::s_state;

// -------------------------------------------------------------------------
// Construction
// -------------------------------------------------------------------------

Gui::Gui(OverlayCanvas& canvas, const FrameInput& input)
    : m_canvas(canvas)
    , m_input(input)
{
    // Track mouse state for click detection
    static bool lastMouseDown = false;
    m_mouseClicked = input.mouseDown[0] && !lastMouseDown;
    m_mouseReleased = !input.mouseDown[0] && lastMouseDown;
    lastMouseDown = input.mouseDown[0];
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
    float baseline = y + m_style.widgetHeight * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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
    float titleBaseline = y + m_style.titleHeight * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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
    float textY = y + h * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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

    if (m_style.valuePosition == ValuePosition::Center) {
        float textW = m_canvas.measureText(valueBuf, 0);
        float textX = sliderX + (sliderW - textW) * 0.5f;
        float textY = sliderY + sliderH * 0.5f + m_canvas.fontAscent(0) * 0.35f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    } else if (m_style.valuePosition == ValuePosition::Right) {
        float textX = sliderX + sliderW + m_style.padding;
        float textY = sliderY + sliderH * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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

    if (m_style.valuePosition == ValuePosition::Center) {
        float textW = m_canvas.measureText(valueBuf, 0);
        float textX = sliderX + (sliderW - textW) * 0.5f;
        float textY = sliderY + sliderH * 0.5f + m_canvas.fontAscent(0) * 0.35f;
        m_canvas.text(valueBuf, textX, textY, m_style.text, 0);
    } else if (m_style.valuePosition == ValuePosition::Right) {
        float textX = sliderX + sliderW + m_style.padding;
        float textY = sliderY + sliderH * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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

    // Layout: [label] [dropdown button]
    float labelW = m_style.labelWidth;
    float buttonX = x + labelW;
    float buttonW = w - labelW;

    bool isOpen = (s_state.openDropdown == id);
    bool hovered = isMouseInRect(buttonX, y, buttonW, h);

    // Draw label
    drawLabel(x, y, label);

    // Draw dropdown button
    drawWidgetBackground(buttonX, y, buttonW, h, hovered || isOpen, isOpen);

    // Draw current selection
    int safeIndex = std::clamp(*index, 0, static_cast<int>(options.size()) - 1);
    const std::string& current = options[safeIndex];
    float textY = y + h * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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

        float menuY = y + h;
        float menuH = options.size() * h;

        // Menu background
        m_canvas.fillRoundedRect(buttonX, menuY, buttonW, menuH, m_style.cornerRadius, m_style.panelBackground);
        m_canvas.strokeRoundedRect(buttonX, menuY, buttonW, menuH, m_style.cornerRadius, m_style.borderWidth, m_style.panelBorder);

        // Menu items
        for (size_t i = 0; i < options.size(); ++i) {
            float itemY = menuY + i * h;
            bool itemHovered = isMouseInRect(buttonX, itemY, buttonW, h);

            if (itemHovered) {
                m_canvas.fillRoundedRect(buttonX + 2, itemY + 2, buttonW - 4, h - 4, m_style.cornerRadius, m_style.widgetHover);
            }

            float itemTextY = itemY + h * 0.5f + m_canvas.fontAscent(0) * 0.35f;

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
        if (m_mouseClicked && !isMouseInRect(buttonX, y, buttonW, h + menuH)) {
            s_state.openDropdown = 0;
        }

        m_canvas.setLayer(UILayer::Panels);
    }

    advanceCursor(h);
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
    float textY = y + h * 0.5f + m_canvas.fontAscent(0) * 0.35f;
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

} // namespace vivid
