#pragma once

/**
 * @file inspector_panel.h
 * @brief Inspector panel for operator parameters
 *
 * Displays parameter sliders for the selected operator.
 * Extracted from ChainVisualizer.
 */

#include <vivid/gui/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class Operator;

/**
 * @brief Inspector panel for editing operator parameters
 */
class InspectorPanel : public Panel {
public:
    InspectorPanel();
    ~InspectorPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;

    // Inspector specific
    void setSelectedOperator(Operator* op, const std::string& name);
    void clearSelection();

    // Parameter change callback
    using ParamChangeCallback = std::function<void(const std::string& opName,
                                                    const std::string& paramName,
                                                    const float oldVal[4],
                                                    const float newVal[4],
                                                    int sourceLine)>;
    void onParamChange(ParamChangeCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
