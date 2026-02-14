// Preset Panel Implementation
// Snapshot/preset management: save, recall, crossfade, reorder

#include <vivid/devtools/panels/preset_panel.h>
#include <vivid/snapshot.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <string>
#include <algorithm>
#include <cstdio>

namespace vivid {

struct PresetPanel::Impl {
    // Scroll state
    float scrollOffset = 0.0f;

    // Selection
    int selectedIndex = -1;
};

PresetPanel::PresetPanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "presets";
    m_config.title = "Presets";
    m_config.bounds = {10, 300, 260, 320};
    m_config.role = PanelRole::Floating;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 200.0f;
    m_config.minHeight = 160.0f;
}

PresetPanel::~PresetPanel() = default;

bool PresetPanel::init(Context& /*ctx*/, WGPUTextureFormat /*surfaceFormat*/) {
    return true;
}

void PresetPanel::shutdown() {
    m_impl.reset();
}

void PresetPanel::setChain(Chain* chain, const std::string& projectDir) {
    m_chain = chain;
    m_projectDir = projectDir;
    m_store = chain ? &chain->snapshots() : nullptr;
}

void PresetPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                          const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    canvas.setLayer(UILayer::FloatingPanels);

    // Panel background
    float cornerRadius = style.panelCornerRadius();
    glm::vec4 bgColor = style.panelBg;
    bgColor.a = 0.92f;

    if (cornerRadius > 0.0f) {
        canvas.fillRoundedRect(x, y, w, h, cornerRadius, bgColor);
        canvas.strokeRoundedRect(x, y, w, h, cornerRadius, 1.0f, style.panelBorder);
    } else {
        canvas.fillRect(x, y, w, h, bgColor);
        canvas.strokeRect(x, y, w, h, 1.0f, style.panelBorder);
    }

    // Title bar
    float titleH = style.titleBarHeight();
    glm::vec4 headerColor = style.headerBg;
    headerColor.a = 0.9f;
    canvas.fillRoundedRectTop(x, y, w, titleH, cornerRadius, headerColor);
    canvas.text(m_config.title, x + 10, y + 18, style.textPrimary, 0);

    // Close button (X)
    {
        float closeSize = 8.0f;
        float closePadding = 12.0f;
        float closeX = x + w - closePadding - closeSize;
        float closeY = y + titleH / 2;

        float hitPadding = 4.0f;
        bool overClose = input.mousePos.x >= closeX - closeSize - hitPadding &&
                         input.mousePos.x <= closeX + closeSize + hitPadding &&
                         input.mousePos.y >= closeY - closeSize - hitPadding &&
                         input.mousePos.y <= closeY + closeSize + hitPadding;

        if (overClose && input.mouseClicked[0]) {
            m_config.visible = false;
            return;
        }

        glm::vec4 closeColor = overClose
            ? glm::vec4(1.0f, 0.4f, 0.4f, 1.0f)
            : style.textDim;
        float lineWidth = overClose ? 2.0f : 1.5f;

        canvas.line(closeX - closeSize, closeY - closeSize,
                    closeX + closeSize, closeY + closeSize, lineWidth, closeColor);
        canvas.line(closeX + closeSize, closeY - closeSize,
                    closeX - closeSize, closeY + closeSize, lineWidth, closeColor);
    }

    // Content area
    float pad = style.padding();
    float contentX = x + pad;
    float contentY = y + titleH + style.smallPadding();
    float contentW = w - pad * 2;

    // Bottom area: crossfade slider + save button
    float bottomH = 52.0f;
    float contentH = h - titleH - style.smallPadding() - bottomH;

    canvas.beginClipRect(x, y + titleH, w, h - titleH);

    const int fontIdx = 0;
    float lineH = canvas.fontLineHeight(fontIdx);
    if (lineH <= 0) lineH = 14.0f;
    float itemH = lineH + 10.0f;
    float ascent = canvas.fontAscent(fontIdx);
    if (ascent <= 0) ascent = 10.0f;

    int snapCount = m_store ? m_store->size() : 0;
    int activeIdx = m_store ? m_store->activeIndex() : -1;

    // Snapshot list
    if (snapCount == 0) {
        canvas.text("No snapshots yet", contentX, contentY + ascent + 4, style.textDim, fontIdx);
        canvas.text("Press + to capture", contentX, contentY + ascent + 4 + lineH + 2, style.textDim, fontIdx);
    } else {
        float totalContentH = static_cast<float>(snapCount) * itemH;
        float maxScroll = std::max(0.0f, totalContentH - contentH);
        m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, maxScroll);

        if (m_focus.hovered && input.scroll.y != 0) {
            m_impl->scrollOffset -= input.scroll.y * style.scrollSpeed();
            m_impl->scrollOffset = std::clamp(m_impl->scrollOffset, 0.0f, maxScroll);
        }

        for (int i = 0; i < snapCount; i++) {
            float itemY = contentY + static_cast<float>(i) * itemH - m_impl->scrollOffset;

            if (itemY + itemH < contentY || itemY > contentY + contentH) continue;

            const Snapshot* snap = m_store->get(i);
            if (!snap) continue;

            bool isActive = (i == activeIdx);
            bool isSelected = (i == m_impl->selectedIndex);

            bool hovered = input.mousePos.x >= contentX &&
                           input.mousePos.x <= contentX + contentW &&
                           input.mousePos.y >= itemY &&
                           input.mousePos.y <= itemY + itemH &&
                           input.mousePos.y >= contentY &&
                           input.mousePos.y <= contentY + contentH;

            // Background
            if (isActive) {
                glm::vec4 activeBg = style.accent;
                activeBg.a = 0.25f;
                canvas.fillRect(contentX, itemY, contentW, itemH, activeBg);
            } else if (hovered) {
                glm::vec4 hoverBg = style.buttonHover;
                hoverBg.a = 0.4f;
                canvas.fillRect(contentX, itemY, contentW, itemH, hoverBg);
            } else if (isSelected) {
                glm::vec4 selBg = style.buttonBg;
                selBg.a = 0.3f;
                canvas.fillRect(contentX, itemY, contentW, itemH, selBg);
            }

            // Number badge (1-9)
            if (i < 9) {
                char numBuf[4];
                snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
                glm::vec4 numColor = isActive ? style.accent : style.textDim;
                canvas.text(numBuf, contentX + 4, itemY + ascent + 5, numColor, fontIdx);
            }

            // Snapshot name
            float nameX = contentX + 20;
            glm::vec4 nameColor = isActive ? style.textPrimary : style.textPrimary;
            canvas.text(snap->name, nameX, itemY + ascent + 5, nameColor, fontIdx);

            // Click to recall
            if (hovered && input.mouseClicked[0] && m_chain) {
                m_impl->selectedIndex = i;
                m_store->recall(i, *m_chain, m_crossfadeDuration);
            }

            // Right-click to delete
            if (hovered && input.mouseClicked[1]) {
                m_store->remove(i);
                if (m_impl->selectedIndex >= m_store->size()) {
                    m_impl->selectedIndex = m_store->size() - 1;
                }
                autoSave();
                break;
            }
        }
    }

    canvas.endClipRect();

    // Bottom toolbar area
    float toolbarY = y + h - bottomH;

    // Separator line
    canvas.line(x + pad, toolbarY, x + w - pad, toolbarY, 1.0f, style.panelBorder);

    // Save button (+)
    float btnW = 32.0f;
    float btnH = 24.0f;
    float btnX = contentX;
    float btnY = toolbarY + 6.0f;

    bool overSaveBtn = input.mousePos.x >= btnX &&
                       input.mousePos.x <= btnX + btnW &&
                       input.mousePos.y >= btnY &&
                       input.mousePos.y <= btnY + btnH;

    glm::vec4 saveBtnBg = overSaveBtn ? style.buttonHover : style.buttonBg;
    canvas.fillRoundedRect(btnX, btnY, btnW, btnH, style.buttonCornerRadius(), saveBtnBg);
    canvas.strokeRoundedRect(btnX, btnY, btnW, btnH, style.buttonCornerRadius(), 1.0f, style.buttonBorder);
    canvas.text("+", btnX + 11, btnY + ascent + 4, style.textPrimary, fontIdx);

    if (overSaveBtn && input.mouseClicked[0] && m_store && m_chain) {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "Snapshot %d", m_store->size() + 1);
        m_store->capture(nameBuf, *m_chain);
        autoSave();
    }

    // Crossfade duration slider
    float sliderX = btnX + btnW + 12.0f;
    float sliderW = contentW - btnW - 12.0f;

    // Label
    char durationLabel[32];
    if (m_crossfadeDuration <= 0.0f) {
        snprintf(durationLabel, sizeof(durationLabel), "Hard Cut");
    } else {
        snprintf(durationLabel, sizeof(durationLabel), "Fade: %.1fs", m_crossfadeDuration);
    }
    canvas.text(durationLabel, sliderX, btnY - 1, style.textDim, fontIdx);

    // Slider track
    float trackY = btnY + 16.0f;
    float trackH = 6.0f;
    canvas.fillRoundedRect(sliderX, trackY, sliderW, trackH, 3.0f, style.sliderBg);

    // Slider fill
    float t = m_crossfadeDuration / 10.0f;
    float fillW = sliderW * t;
    if (fillW > 0.0f) {
        canvas.fillRoundedRect(sliderX, trackY, fillW, trackH, 3.0f, style.sliderFill);
    }

    // Slider interaction
    bool overSlider = input.mousePos.x >= sliderX &&
                      input.mousePos.x <= sliderX + sliderW &&
                      input.mousePos.y >= trackY - 4.0f &&
                      input.mousePos.y <= trackY + trackH + 4.0f;

    if (overSlider && input.mouseDown[0]) {
        float normalizedX = (input.mousePos.x - sliderX) / sliderW;
        normalizedX = std::clamp(normalizedX, 0.0f, 1.0f);
        m_crossfadeDuration = normalizedX * 10.0f;
    }

    // Crossfade progress bar
    if (m_store && m_store->isCrossfading()) {
        float progressY = toolbarY + btnH + 12.0f;
        float progressH = 3.0f;
        canvas.fillRect(contentX, progressY, contentW, progressH, style.sliderBg);
        float progress = m_store->crossfadeProgress();
        canvas.fillRect(contentX, progressY, contentW * progress, progressH, style.accent);
    }
}

bool PresetPanel::handleInput(const gui::InputState& input) {
    if (!m_config.visible || !m_impl) return false;

    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;

    return input.mousePos.x >= x && input.mousePos.x < x + w &&
           input.mousePos.y >= y && input.mousePos.y < y + h;
}

void PresetPanel::onKeyDown(int key, int mods) {
    if (!m_config.visible || !m_store) return;

    // Delete/Backspace to remove selected snapshot
    if ((key == 259 || key == 261) && m_impl->selectedIndex >= 0) {
        m_store->remove(m_impl->selectedIndex);
        if (m_impl->selectedIndex >= m_store->size()) {
            m_impl->selectedIndex = m_store->size() - 1;
        }
        autoSave();
    }
}

void PresetPanel::autoSave() {
    if (!m_store || m_projectDir.empty()) return;
    m_store->save(m_projectDir + "/vivid-snapshots.json");
}

} // namespace vivid
