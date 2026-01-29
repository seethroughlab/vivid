#pragma once

/**
 * @file operator_library_panel.h
 * @brief Operator library panel for browsing all available operators
 *
 * Displays all registered operators from ALL linked modules, grouped by category.
 * Features:
 * - Search filtering by name, description, or category
 * - Category-based grouping with collapsible sections
 * - Detail view showing operator metadata and parameters
 */

#include <vivid/gui/panel.h>
#include <memory>

namespace vivid {

/**
 * @brief Panel displaying the operator library from OperatorRegistry
 *
 * Accesses OperatorRegistry::instance() directly to display all available
 * operators from vivid-core and all linked modules (audio, video, etc.).
 */
class OperatorLibraryPanel : public Panel {
public:
    OperatorLibraryPanel();
    ~OperatorLibraryPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void update() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;
    void onChar(uint32_t codepoint) override;
    void onKeyDown(int key, int mods) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
