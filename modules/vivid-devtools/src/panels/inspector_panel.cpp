// Inspector Panel Implementation
// Extracted from ChainVisualizer - shows parameter sliders for selected operator
//
// Features:
// - Float/Int/Bool sliders
// - Vec2 XY pads
// - Vec3/Vec4 component sliders
// - Color picker with HSV
// - ADSR envelope widget
// - Enum/DeviceList dropdowns
// - Scroll for long parameter lists
// - Parameter change callbacks for Claude workflow

#include <vivid/devtools/panels/inspector_panel.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/gui.h>
#include <vivid/gui/ui_style.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace vivid {

struct InspectorPanel::Impl {
    // Selected operator
    Operator* selectedOp = nullptr;
    std::string selectedName;

    // Callback
    ParamChangeCallback paramCallback;

    // Scroll state
    float scrollOffset = 0.0f;
    float contentHeight = 0.0f;

    // Color picker expanded state
    struct ColorPickerState {
        bool expanded = false;
        std::string operatorName;
        std::string paramName;
        float originalColor[4] = {0, 0, 0, 1};
    };
    ColorPickerState colorPicker;

    // Active drag context for callbacks
    struct ActiveDragContext {
        std::string operatorName;
        std::string paramName;
        int sourceLine = 0;
        float originalValue[4] = {0, 0, 0, 0};
        bool active = false;
    };
    ActiveDragContext activeDrag;

    // Context pointer
    Context* ctx = nullptr;
};

InspectorPanel::InspectorPanel() {
    m_config.id = "inspector";
    m_config.title = "Inspector";
    m_config.bounds = {20, 60, 280, 400};   // Floating, positioned on left side (away from minimap)
    m_config.dockSide = DockSide::None;     // Floating panel
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;              // Allow dragging
    m_config.minWidth = 200.0f;
    m_config.minHeight = 200.0f;
}

InspectorPanel::~InspectorPanel() = default;

bool InspectorPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->ctx = &ctx;
    return true;
}

void InspectorPanel::shutdown() {
    m_impl.reset();
}

void InspectorPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                             const FrameInput& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    // Handle drag/resize like other floating panels
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float screenW = static_cast<float>(input.width) / scale;
    float screenH = static_cast<float>(input.height) / scale;
    handleDragAndResize(input, screenW, screenH);

    // Get bounds after drag/resize
    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;

    // Render standard panel chrome (background, border, title bar)
    renderChrome(canvas, x, y, w, h, style);

    // If no operator selected, just show empty panel
    if (!m_impl->selectedOp) {
        m_impl->scrollOffset = 0.0f;
        return;
    }

    Operator* op = m_impl->selectedOp;
    const std::string& title = m_impl->selectedName;

    // Get parameters
    auto params = op->params();
    if (params.empty()) return;

    // Font metrics
    const int labelFont = 0;  // JetBrains Mono 14px
    float lineH = canvas.fontLineHeight(labelFont);
    float ascent = canvas.fontAscent(labelFont);
    if (lineH <= 0) lineH = 20.0f;
    if (ascent <= 0) ascent = 14.0f;

    // Layout values (in logical pixels)
    float inspectorWidth = w;
    const float padding = 12.0f;
    const float sliderHeight = 20.0f;
    const float rowHeight = lineH + sliderHeight + 4.0f;
    const float titleBarHeight = style.titleBarHeight();

    // Create Gui instance
    Gui gui(canvas, input);
    gui.style().labelPosition = LabelPosition::Above;
    gui.style().valuePosition = ValuePosition::Right;
    gui.style().padding = padding;
    gui.style().widgetHeight = sliderHeight;
    gui.style().valueWidth = 60.0f;
    gui.style().cornerRadius = style.sliderCornerRadius();
    gui.style().widgetBackground = style.sliderBg;
    gui.style().sliderFill = style.sliderFill;
    gui.style().sliderFillActive = style.sliderActive;
    gui.style().text = style.textPrimary;
    gui.style().textDim = style.textDim;

    // Calculate total content height
    float totalRowsHeight = 0;
    for (const auto& p : params) {
        switch (p.type) {
            case ParamType::Vec2:
                totalRowsHeight += rowHeight * 2.5f;
                break;
            case ParamType::Vec3:
                totalRowsHeight += rowHeight * 2.0f;
                break;
            case ParamType::Vec4:
            case ParamType::Color:
                totalRowsHeight += rowHeight * 4;
                break;
            case ParamType::Enum:
            case ParamType::DeviceList:
                totalRowsHeight += rowHeight;
                break;
            case ParamType::ADSR:
                totalRowsHeight += rowHeight * 4;
                break;
            default:
                totalRowsHeight += rowHeight;
                break;
        }
    }

    // Content area starts below the title bar
    float contentY = y + titleBarHeight;
    float contentAreaHeight = h - titleBarHeight;
    m_impl->contentHeight = totalRowsHeight + padding * 2;

    // Handle scroll
    glm::vec2 mousePos = input.mousePos;
    bool mouseInPanel = mousePos.x >= x && mousePos.x <= x + w &&
                        mousePos.y >= y && mousePos.y <= y + h;

    if (mouseInPanel && input.scroll.y != 0.0f) {
        m_impl->scrollOffset -= input.scroll.y * 30.0f;
        float maxScroll = std::max(0.0f, m_impl->contentHeight - contentAreaHeight);
        m_impl->scrollOffset = std::max(0.0f, std::min(m_impl->scrollOffset, maxScroll));
    }

    // Draw operator name in content area header
    glm::vec4 titleColor = style.textTitle;
    std::string headerTitle = op->name() + " (" + title + ")";
    canvas.text(headerTitle, x + padding, contentY + padding + ascent, titleColor, labelFont);
    contentY += lineH + padding;

    // Mouse state
    bool mouseDown = input.mouseDown[0];
    static bool lastMouseDown = false;
    bool mouseReleased = !mouseDown && lastMouseDown;
    lastMouseDown = mouseDown;

    // Visible content bounds
    float visibleTop = contentY;
    float visibleBottom = y + h;

    // Content area with scroll
    float scrolledY = contentY - m_impl->scrollOffset;
    float contentAreaX = x + padding;
    float contentAreaW = inspectorWidth - padding * 2;

    // Note: gui.beginArea() already sets up clipping internally
    gui.beginArea(contentAreaX, visibleTop, contentAreaW, visibleBottom - visibleTop);

    for (const auto& p : params) {
        float value[4] = {0};
        op->getParam(p.name, value);

        int componentCount = 1;
        const char* componentLabels[] = {"", "", "", ""};

        switch (p.type) {
            case ParamType::Vec2:
                componentCount = 2;
                componentLabels[0] = "X"; componentLabels[1] = "Y";
                break;
            case ParamType::Vec3:
                componentCount = 3;
                componentLabels[0] = "X"; componentLabels[1] = "Y"; componentLabels[2] = "Z";
                break;
            case ParamType::Vec4:
                componentCount = 4;
                componentLabels[0] = "X"; componentLabels[1] = "Y";
                componentLabels[2] = "Z"; componentLabels[3] = "W";
                break;
            case ParamType::Color:
                componentCount = 4;
                componentLabels[0] = "R"; componentLabels[1] = "G";
                componentLabels[2] = "B"; componentLabels[3] = "A";
                break;
            default:
                break;
        }

        // Handle Enum type
        if (p.type == ParamType::Enum) {
            float itemY = scrolledY;
            float itemBottom = itemY + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible && !p.enumLabels.empty()) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                int currentIndex = static_cast<int>(value[0]);
                int prevIndex = currentIndex;

                if (gui.dropdown(p.name.c_str(), &currentIndex, p.enumLabels)) {
                    float newValue[4] = {static_cast<float>(currentIndex), 0, 0, 0};
                    op->setParam(p.name, newValue);

                    if (m_impl->paramCallback) {
                        float oldValue[4] = {static_cast<float>(prevIndex), 0, 0, 0};
                        m_impl->paramCallback(title, p.name, oldValue, newValue, op->sourceLine);
                    }
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += rowHeight;
            continue;
        }

        // Handle DeviceList type
        if (p.type == ParamType::DeviceList) {
            float itemY = scrolledY;
            float itemBottom = itemY + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible && p.deviceListProvider) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                std::vector<std::string> deviceList = p.deviceListProvider();
                int currentIndex = static_cast<int>(value[0]);
                int prevIndex = currentIndex;

                if (currentIndex < 0) currentIndex = 0;
                if (currentIndex >= static_cast<int>(deviceList.size())) {
                    currentIndex = 0;
                }

                if (gui.dropdown(p.name.c_str(), &currentIndex, deviceList)) {
                    float newValue[4] = {static_cast<float>(currentIndex), 0, 0, 0};
                    op->setParam(p.name, newValue);

                    if (m_impl->paramCallback) {
                        float oldValue[4] = {static_cast<float>(prevIndex), 0, 0, 0};
                        m_impl->paramCallback(title, p.name, oldValue, newValue, op->sourceLine);
                    }
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += rowHeight;
            continue;
        }

        // Handle Color type
        if (p.type == ParamType::Color) {
            float itemY = scrolledY;
            float itemBottom = itemY + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                bool isExpanded = m_impl->colorPicker.expanded &&
                                 m_impl->colorPicker.operatorName == title &&
                                 m_impl->colorPicker.paramName == p.name;
                bool wasExpanded = isExpanded;

                glm::vec4 color = {value[0], value[1], value[2], value[3]};
                auto result = gui.colorPickerHSV(p.name.c_str(), &color, &isExpanded);

                if (isExpanded != wasExpanded) {
                    if (isExpanded) {
                        m_impl->colorPicker.expanded = true;
                        m_impl->colorPicker.operatorName = title;
                        m_impl->colorPicker.paramName = p.name;
                        for (int i = 0; i < 4; ++i) {
                            m_impl->colorPicker.originalColor[i] = value[i];
                        }
                    } else {
                        m_impl->colorPicker.expanded = false;
                    }
                }

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    m_impl->activeDrag.originalValue[0] = result.startColor.r;
                    m_impl->activeDrag.originalValue[1] = result.startColor.g;
                    m_impl->activeDrag.originalValue[2] = result.startColor.b;
                    m_impl->activeDrag.originalValue[3] = result.startColor.a;
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {color.r, color.g, color.b, color.a};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4] = {color.r, color.g, color.b, color.a};
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            float swatchHeight = sliderHeight + lineH + 8.0f;
            bool isExpanded = m_impl->colorPicker.expanded &&
                             m_impl->colorPicker.operatorName == title &&
                             m_impl->colorPicker.paramName == p.name;
            scrolledY += swatchHeight;
            if (isExpanded) {
                scrolledY += 4 * rowHeight;
            }
            continue;
        }

        // Handle ADSR type
        if (p.type == ParamType::ADSR) {
            float itemY = scrolledY;
            float adsrHeight = rowHeight * 4;
            float itemBottom = itemY + adsrHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                float attack = value[0];
                float decay = value[1];
                float sustain = value[2];
                float release = value[3];

                auto result = gui.adsrEnvelope(p.name.c_str(), &attack, &decay, &sustain, &release, p.maxVal);

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    m_impl->activeDrag.originalValue[0] = result.startA;
                    m_impl->activeDrag.originalValue[1] = result.startD;
                    m_impl->activeDrag.originalValue[2] = result.startS;
                    m_impl->activeDrag.originalValue[3] = result.startR;
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {attack, decay, sustain, release};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4] = {attack, decay, sustain, release};
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += adsrHeight;
            continue;
        }

        // Simple params (Float/Int/Bool)
        if (componentCount == 1) {
            float itemY = scrolledY;
            float itemBottom = itemY + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                auto result = gui.sliderEx(p.name.c_str(), &value[0], p.minVal, p.maxVal);

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    for (int i = 0; i < 4; ++i) {
                        m_impl->activeDrag.originalValue[i] = value[i];
                    }
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {value[0], 0, 0, 0};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4] = {value[0], 0, 0, 0};
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += rowHeight;
            continue;
        }

        // Vec2 params - XY pad
        if (p.type == ParamType::Vec2) {
            float itemY = scrolledY;
            float padHeight = rowHeight * 2.5f;
            float itemBottom = itemY + padHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                glm::vec2 vec2Value = {value[0], value[1]};
                auto result = gui.xyPadEx(p.name.c_str(), &vec2Value,
                                          p.minVal, p.maxVal, p.minVal, p.maxVal, 0);

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    m_impl->activeDrag.originalValue[0] = result.startValue.x;
                    m_impl->activeDrag.originalValue[1] = result.startValue.y;
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {vec2Value.x, vec2Value.y, 0, 0};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4] = {vec2Value.x, vec2Value.y, 0, 0};
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += padHeight;
            continue;
        }

        // Vec3 params - compact row
        if (p.type == ParamType::Vec3) {
            float itemY = scrolledY;
            float rowH = rowHeight * 2.0f;
            float itemBottom = itemY + rowH;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(title.c_str());
                gui.pushId(p.name.c_str());

                glm::vec3 vec3Value = {value[0], value[1], value[2]};
                auto result = gui.vec3Row(p.name.c_str(), &vec3Value, p.minVal, p.maxVal);

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    m_impl->activeDrag.originalValue[0] = result.startValue.x;
                    m_impl->activeDrag.originalValue[1] = result.startValue.y;
                    m_impl->activeDrag.originalValue[2] = result.startValue.z;
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4] = {vec3Value.x, vec3Value.y, vec3Value.z, 0};
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4] = {vec3Value.x, vec3Value.y, vec3Value.z, 0};
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
                gui.popId();
            }

            scrolledY += rowH;
            continue;
        }

        // Vec4 - individual sliders
        gui.pushId(title.c_str());
        gui.pushId(p.name.c_str());

        for (int c = 0; c < componentCount; ++c) {
            float itemY = scrolledY;
            float itemBottom = itemY + rowHeight;
            bool isVisible = (itemBottom > visibleTop) && (itemY < visibleBottom);

            if (isVisible) {
                gui.setCursorY(itemY);
                gui.pushId(std::to_string(c).c_str());

                std::string label = p.name + "." + componentLabels[c];
                auto result = gui.sliderEx(label.c_str(), &value[c], p.minVal, p.maxVal);

                if (result.dragStarted) {
                    m_impl->activeDrag.operatorName = title;
                    m_impl->activeDrag.paramName = p.name;
                    m_impl->activeDrag.sourceLine = op->sourceLine;
                    for (int i = 0; i < 4; ++i) {
                        m_impl->activeDrag.originalValue[i] = value[i];
                    }
                    m_impl->activeDrag.active = true;
                }

                if (result.changed) {
                    float newValues[4];
                    for (int i = 0; i < 4; ++i) newValues[i] = value[i];
                    op->setParam(p.name, newValues);
                }

                if (result.dragEnded && m_impl->activeDrag.active &&
                    m_impl->activeDrag.operatorName == title &&
                    m_impl->activeDrag.paramName == p.name) {
                    float newValues[4];
                    for (int i = 0; i < 4; ++i) newValues[i] = value[i];
                    if (m_impl->paramCallback) {
                        m_impl->paramCallback(m_impl->activeDrag.operatorName, m_impl->activeDrag.paramName,
                                              m_impl->activeDrag.originalValue, newValues,
                                              m_impl->activeDrag.sourceLine);
                    }
                    m_impl->activeDrag.active = false;
                }

                gui.popId();
            }

            scrolledY += rowHeight;
        }

        gui.popId();
        gui.popId();
    }

    // Cancel drag if mouse released outside
    if (mouseReleased && m_impl->activeDrag.active) {
        m_impl->activeDrag.active = false;
    }

    gui.endArea();
}

bool InspectorPanel::handleInput(const FrameInput& input) {
    // Input is handled through the Gui widgets in render()
    return false;
}

void InspectorPanel::setSelectedOperator(Operator* op, const std::string& name) {
    if (m_impl) {
        m_impl->selectedOp = op;
        m_impl->selectedName = name;
        m_impl->scrollOffset = 0.0f;  // Reset scroll on selection change
    }
}

void InspectorPanel::clearSelection() {
    if (m_impl) {
        m_impl->selectedOp = nullptr;
        m_impl->selectedName.clear();
        m_impl->scrollOffset = 0.0f;
    }
}

void InspectorPanel::onParamChange(ParamChangeCallback callback) {
    if (m_impl) {
        m_impl->paramCallback = std::move(callback);
    }
}

} // namespace vivid
