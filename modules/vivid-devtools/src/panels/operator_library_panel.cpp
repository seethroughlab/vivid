// Operator Library Panel Implementation
// Displays all operators from OperatorRegistry, grouped by category
//
// Features:
// - Search filtering by name, description, or category
// - Collapsible category sections
// - Detail view with parameters when an operator is selected

#include <vivid/devtools/panels/operator_library_panel.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/gui.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace vivid {

// Helper to convert string to lowercase for case-insensitive search
static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

struct OperatorLibraryPanel::Impl {
    Context* ctx = nullptr;

    // Cached operator list (grouped by category)
    std::vector<std::string> categories;
    std::map<std::string, std::vector<const OperatorMeta*>> operatorsByCategory;

    // UI state
    std::string searchQuery;
    std::string selectedCategory;      // Empty = show all
    const OperatorMeta* selectedOp = nullptr;
    float scrollOffset = 0.0f;
    float maxScrollOffset = 0.0f;

    // Collapsed categories
    std::set<std::string> collapsedCategories;

    // Text input state for search
    bool searchFocused = false;
    size_t cursorPos = 0;

    // Refresh from registry
    void refreshOperatorList() {
        auto& registry = OperatorRegistry::instance();

        operatorsByCategory.clear();
        categories.clear();

        for (const auto& meta : registry.operators()) {
            operatorsByCategory[meta.category].push_back(&meta);
        }

        // Sort operators within each category alphabetically
        for (auto& [cat, ops] : operatorsByCategory) {
            std::sort(ops.begin(), ops.end(), [](const OperatorMeta* a, const OperatorMeta* b) {
                return a->name < b->name;
            });
        }

        // Get sorted category list
        for (const auto& [cat, _] : operatorsByCategory) {
            categories.push_back(cat);
        }
        std::sort(categories.begin(), categories.end());
    }

    // Check if an operator matches the search query
    bool matchesSearch(const OperatorMeta* meta) const {
        if (searchQuery.empty()) return true;

        std::string lowerQuery = toLower(searchQuery);
        return toLower(meta->name).find(lowerQuery) != std::string::npos ||
               toLower(meta->description).find(lowerQuery) != std::string::npos ||
               toLower(meta->category).find(lowerQuery) != std::string::npos ||
               toLower(meta->module).find(lowerQuery) != std::string::npos;
    }

    // Count operators matching search in a category
    int countMatchingInCategory(const std::string& category) const {
        auto it = operatorsByCategory.find(category);
        if (it == operatorsByCategory.end()) return 0;

        int count = 0;
        for (const auto* meta : it->second) {
            if (matchesSearch(meta)) count++;
        }
        return count;
    }
};

OperatorLibraryPanel::OperatorLibraryPanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "library";
    m_config.title = "Operator Library";
    m_config.bounds = {0, 0, 320, 500};
    m_config.dockSide = DockSide::Right;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 250.0f;
    m_config.minHeight = 300.0f;
}

OperatorLibraryPanel::~OperatorLibraryPanel() = default;

bool OperatorLibraryPanel::init(Context& ctx, WGPUTextureFormat /*surfaceFormat*/) {
    m_impl->ctx = &ctx;
    m_impl->refreshOperatorList();
    return true;
}

void OperatorLibraryPanel::shutdown() {
    m_impl.reset();
}

void OperatorLibraryPanel::update() {
    // Could refresh periodically if modules can be hot-loaded
    // For now, we refresh on init only
}

void OperatorLibraryPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                                   const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    // Render panel chrome (background, title bar)
    bool showTitle = m_display.showTitleBar;
    renderChrome(canvas, x, y, w, h, style, showTitle, &input);

    // Handle close button
    if (closeButtonClicked()) {
        m_config.visible = false;
        return;
    }

    // Content area (below title bar)
    float titleH = showTitle ? style.titleBarHeight() : 0.0f;
    float padding = style.padding();
    float contentX = x + padding;
    float contentY = y + titleH + padding;
    float contentW = w - padding * 2;
    float contentH = h - titleH - padding * 2;

    // Clip to content area
    canvas.beginClipRect(x, y + titleH, w, h - titleH);

    const int monoFont = 2;
    float lineH = canvas.fontLineHeight(monoFont);
    float ascent = canvas.fontAscent(monoFont);
    if (lineH <= 0) lineH = 16.0f;
    if (ascent <= 0) ascent = 12.0f;

    float curY = contentY;

    // Search box
    {
        float searchH = lineH + padding * 2;
        glm::vec4 searchBg = style.sliderBg;
        canvas.fillRoundedRect(contentX, curY, contentW, searchH, style.sliderCornerRadius(), searchBg);
        canvas.strokeRoundedRect(contentX, curY, contentW, searchH, style.sliderCornerRadius(), 1,
                                  m_impl->searchFocused ? style.accent : style.buttonBorder);

        // Search icon or placeholder
        float textX = contentX + padding;
        float textY = curY + padding + ascent;

        if (m_impl->searchQuery.empty() && !m_impl->searchFocused) {
            canvas.text("Search operators...", textX, textY, style.textDim, monoFont);
        } else {
            canvas.text(m_impl->searchQuery, textX, textY, style.textPrimary, monoFont);

            // Draw cursor if focused
            if (m_impl->searchFocused) {
                float cursorX = textX + canvas.measureText(m_impl->searchQuery.substr(0, m_impl->cursorPos), monoFont);
                canvas.fillRect(cursorX, curY + padding, 1, lineH, style.accent);
            }
        }

        curY += searchH + padding;
    }

    // Divider
    canvas.fillRect(contentX, curY, contentW, 1, style.textDim);
    curY += padding;

    // Calculate how much vertical space is available for the list vs detail
    float listAreaTop = curY;
    float detailHeight = 0.0f;

    // If an operator is selected, reserve space for detail view at bottom
    if (m_impl->selectedOp) {
        // Estimate detail height: header + 5 info lines + params section
        detailHeight = lineH * 8 + padding * 4;
        // Get param count for more accurate estimate
        if (m_impl->selectedOp->factory) {
            try {
                auto tempOp = m_impl->selectedOp->factory();
                if (tempOp) {
                    auto params = tempOp->params();
                    detailHeight += static_cast<float>(params.size()) * lineH;
                }
            } catch (...) {
                // Factory might fail, ignore
            }
        }
        detailHeight = std::min(detailHeight, contentH * 0.5f);  // Max 50% of height
    }

    float listAreaH = contentH - (curY - contentY) - detailHeight;

    // Begin scrollable list area
    canvas.beginClipRect(contentX, curY, contentW, listAreaH);

    float listY = curY - m_impl->scrollOffset;
    float totalListHeight = 0.0f;

    // Render categories and operators
    for (const auto& category : m_impl->categories) {
        int matchCount = m_impl->countMatchingInCategory(category);
        if (matchCount == 0) continue;

        bool collapsed = m_impl->collapsedCategories.count(category) > 0;

        // Category header
        float headerY = listY + totalListHeight;
        float headerH = lineH + padding;

        if (headerY + headerH >= curY && headerY < curY + listAreaH) {
            // Header background on hover
            bool headerHovered = input.mousePos.x >= contentX && input.mousePos.x < contentX + contentW &&
                                  input.mousePos.y >= headerY && input.mousePos.y < headerY + headerH;

            if (headerHovered) {
                canvas.fillRect(contentX, headerY, contentW, headerH, style.buttonHover);
            }

            // Collapse arrow
            const char* arrow = collapsed ? "▶" : "▼";
            canvas.text(arrow, contentX + 2, headerY + ascent + padding * 0.5f, style.textDim, monoFont);

            // Category name and count
            char categoryLabel[128];
            snprintf(categoryLabel, sizeof(categoryLabel), "%s (%d)", category.c_str(), matchCount);
            canvas.text(categoryLabel, contentX + 16, headerY + ascent + padding * 0.5f, style.textPrimary, monoFont);
        }

        totalListHeight += headerH;

        // Operators in this category (if not collapsed)
        if (!collapsed) {
            auto it = m_impl->operatorsByCategory.find(category);
            if (it != m_impl->operatorsByCategory.end()) {
                for (const auto* meta : it->second) {
                    if (!m_impl->matchesSearch(meta)) continue;

                    float itemY = listY + totalListHeight;
                    float itemH = lineH + 4;

                    if (itemY + itemH >= curY && itemY < curY + listAreaH) {
                        bool isSelected = (m_impl->selectedOp == meta);
                        bool itemHovered = input.mousePos.x >= contentX && input.mousePos.x < contentX + contentW &&
                                            input.mousePos.y >= itemY && input.mousePos.y < itemY + itemH;

                        // Background
                        if (isSelected) {
                            canvas.fillRect(contentX, itemY, contentW, itemH, style.accent);
                        } else if (itemHovered) {
                            canvas.fillRect(contentX, itemY, contentW, itemH, style.buttonHover);
                        }

                        // Indent operator name
                        float nameX = contentX + 24;
                        glm::vec4 nameColor = isSelected ? glm::vec4(1, 1, 1, 1) : style.textPrimary;
                        canvas.text(meta->name, nameX, itemY + ascent + 2, nameColor, monoFont);

                        // Module tag (right side)
                        std::string moduleTag = meta->module.empty() ? "core" : meta->module;
                        if (moduleTag.rfind("vivid-", 0) == 0) {
                            moduleTag = moduleTag.substr(6);  // Remove "vivid-" prefix
                        }
                        float tagW = canvas.measureText(moduleTag, monoFont);
                        float tagX = contentX + contentW - tagW - 4;
                        glm::vec4 tagColor = isSelected ? glm::vec4(1, 1, 1, 0.7f) : style.textDim;
                        canvas.text(moduleTag, tagX, itemY + ascent + 2, tagColor, monoFont);
                    }

                    totalListHeight += itemH;
                }
            }
        }
    }

    m_impl->maxScrollOffset = std::max(0.0f, totalListHeight - listAreaH);

    canvas.endClipRect();

    // Detail view at bottom (if operator selected)
    if (m_impl->selectedOp) {
        float detailY = curY + listAreaH;

        // Divider
        canvas.fillRect(contentX, detailY, contentW, 1, style.textDim);
        detailY += padding;

        // Selected operator info
        const auto* meta = m_impl->selectedOp;

        // Name header
        canvas.text(meta->name, contentX, detailY + ascent, style.accent, monoFont);
        detailY += lineH + 2;

        // Category and Module
        char infoBuf[256];
        snprintf(infoBuf, sizeof(infoBuf), "Category: %s", meta->category.c_str());
        canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
        detailY += lineH;

        std::string moduleDisplay = meta->module.empty() ? "vivid-core" : meta->module;
        snprintf(infoBuf, sizeof(infoBuf), "Module: %s", moduleDisplay.c_str());
        canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
        detailY += lineH;

        // Output kind
        snprintf(infoBuf, sizeof(infoBuf), "Output: %s", outputKindName(meta->outputKind));
        canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
        detailY += lineH;

        // Requires input
        snprintf(infoBuf, sizeof(infoBuf), "Requires Input: %s", meta->requiresInput ? "Yes" : "No");
        canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
        detailY += lineH;

        // Description
        if (!meta->description.empty()) {
            detailY += 4;
            canvas.text(meta->description, contentX, detailY + ascent, style.textDim, monoFont);
            detailY += lineH;
        }

        // Parameters (instantiate temp operator to get params)
        if (meta->factory) {
            try {
                auto tempOp = meta->factory();
                if (tempOp) {
                    auto params = tempOp->params();
                    if (!params.empty()) {
                        detailY += padding;
                        canvas.text("Parameters:", contentX, detailY + ascent, style.textPrimary, monoFont);
                        detailY += lineH;

                        // Clip parameters if too many
                        float remainingH = (contentY + contentH) - detailY - padding;
                        int maxParams = static_cast<int>(remainingH / lineH);

                        int paramCount = 0;
                        for (const auto& param : params) {
                            if (paramCount >= maxParams) {
                                snprintf(infoBuf, sizeof(infoBuf), "  ... and %zu more", params.size() - static_cast<size_t>(paramCount));
                                canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
                                break;
                            }

                            // Format: name: min - max (default: val)
                            if (param.type == ParamType::Float || param.type == ParamType::Int) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: %.2f - %.2f (%.2f)",
                                         param.name.c_str(), param.minVal, param.maxVal, param.defaultVal[0]);
                            } else if (param.type == ParamType::Bool) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: bool (%s)",
                                         param.name.c_str(), param.defaultVal[0] > 0.5f ? "true" : "false");
                            } else if (param.type == ParamType::Vec2) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: vec2", param.name.c_str());
                            } else if (param.type == ParamType::Vec3 || param.type == ParamType::Color) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: %s", param.name.c_str(),
                                         param.type == ParamType::Color ? "color" : "vec3");
                            } else if (param.type == ParamType::Vec4) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: vec4", param.name.c_str());
                            } else if (param.type == ParamType::FilePath) {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s: file", param.name.c_str());
                            } else {
                                snprintf(infoBuf, sizeof(infoBuf), "  %s", param.name.c_str());
                            }
                            canvas.text(infoBuf, contentX, detailY + ascent, style.textDim, monoFont);
                            detailY += lineH;
                            paramCount++;
                        }
                    }
                }
            } catch (...) {
                // Factory might fail for some operators, just skip params
            }
        }
    }

    canvas.endClipRect();
}

bool OperatorLibraryPanel::handleInput(const gui::InputState& input) {
    if (!m_config.visible || !m_impl) return false;

    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;
    float titleH = m_display.showTitleBar ? 28.0f : 0.0f;  // Approximate
    float padding = 8.0f;

    // Check if mouse is in panel
    bool inPanel = input.mousePos.x >= x && input.mousePos.x < x + w &&
                   input.mousePos.y >= y && input.mousePos.y < y + h;

    if (!inPanel) {
        m_impl->searchFocused = false;
        return false;
    }

    float contentX = x + padding;
    float contentY = y + titleH + padding;
    float contentW = w - padding * 2;
    float lineH = 16.0f;  // Approximate

    // Search box area
    float searchH = lineH + padding * 2;
    bool inSearchBox = input.mousePos.x >= contentX && input.mousePos.x < contentX + contentW &&
                       input.mousePos.y >= contentY && input.mousePos.y < contentY + searchH;

    // Click handling
    if (input.mouseClicked[0]) {
        m_impl->searchFocused = inSearchBox;

        if (!inSearchBox) {
            // Check for category header or operator clicks
            float listY = contentY + searchH + padding * 2;
            float clickY = input.mousePos.y;

            // Calculate which item was clicked (considering scroll)
            float itemY = listY - m_impl->scrollOffset;

            for (const auto& category : m_impl->categories) {
                int matchCount = m_impl->countMatchingInCategory(category);
                if (matchCount == 0) continue;

                bool collapsed = m_impl->collapsedCategories.count(category) > 0;
                float headerH = lineH + padding;

                // Check if clicked on category header
                if (clickY >= itemY && clickY < itemY + headerH) {
                    // Toggle collapse
                    if (collapsed) {
                        m_impl->collapsedCategories.erase(category);
                    } else {
                        m_impl->collapsedCategories.insert(category);
                    }
                    return true;
                }

                itemY += headerH;

                // Check operators if not collapsed
                if (!collapsed) {
                    auto it = m_impl->operatorsByCategory.find(category);
                    if (it != m_impl->operatorsByCategory.end()) {
                        for (const auto* meta : it->second) {
                            if (!m_impl->matchesSearch(meta)) continue;

                            float opH = lineH + 4;
                            if (clickY >= itemY && clickY < itemY + opH) {
                                m_impl->selectedOp = meta;
                                return true;
                            }
                            itemY += opH;
                        }
                    }
                }
            }
        }
    }

    // Scroll handling
    if (input.scroll.y != 0.0f) {
        m_impl->scrollOffset -= input.scroll.y * 20.0f;
        m_impl->scrollOffset = std::max(0.0f, std::min(m_impl->scrollOffset, m_impl->maxScrollOffset));
        return true;
    }

    return inPanel;
}

void OperatorLibraryPanel::onChar(uint32_t codepoint) {
    if (!m_impl || !m_impl->searchFocused) return;

    // Only handle printable ASCII for now
    if (codepoint >= 32 && codepoint < 127) {
        m_impl->searchQuery.insert(m_impl->cursorPos, 1, static_cast<char>(codepoint));
        m_impl->cursorPos++;
        m_impl->scrollOffset = 0.0f;  // Reset scroll when search changes
    }
}

void OperatorLibraryPanel::onKeyDown(int key, int mods) {
    if (!m_impl || !m_impl->searchFocused) return;

    // Key codes (GLFW-style)
    constexpr int KEY_BACKSPACE = 259;
    constexpr int KEY_DELETE = 261;
    constexpr int KEY_LEFT = 263;
    constexpr int KEY_RIGHT = 262;
    constexpr int KEY_HOME = 268;
    constexpr int KEY_END = 269;
    constexpr int KEY_ESCAPE = 256;

    switch (key) {
        case KEY_BACKSPACE:
            if (m_impl->cursorPos > 0) {
                m_impl->searchQuery.erase(m_impl->cursorPos - 1, 1);
                m_impl->cursorPos--;
                m_impl->scrollOffset = 0.0f;
            }
            break;
        case KEY_DELETE:
            if (m_impl->cursorPos < m_impl->searchQuery.length()) {
                m_impl->searchQuery.erase(m_impl->cursorPos, 1);
                m_impl->scrollOffset = 0.0f;
            }
            break;
        case KEY_LEFT:
            if (m_impl->cursorPos > 0) m_impl->cursorPos--;
            break;
        case KEY_RIGHT:
            if (m_impl->cursorPos < m_impl->searchQuery.length()) m_impl->cursorPos++;
            break;
        case KEY_HOME:
            m_impl->cursorPos = 0;
            break;
        case KEY_END:
            m_impl->cursorPos = m_impl->searchQuery.length();
            break;
        case KEY_ESCAPE:
            m_impl->searchFocused = false;
            break;
    }
}

} // namespace vivid
