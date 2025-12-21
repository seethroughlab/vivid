#pragma once

/**
 * @file operator_viz.h
 * @brief Visualization registry for operators
 *
 * Provides a registration system for custom operator visualizations.
 * This allows addons to register their own visualization functions
 * without modifying core code.
 */

#include <functional>
#include <unordered_map>
#include <typeindex>
#include <string>

// Forward declarations (avoid ImGui dependency in header)
struct ImDrawList;
struct ImVec2;

namespace vivid {

class Operator;

/**
 * @brief Visualization context passed to visualizer functions
 */
struct VizContext {
    ImDrawList* drawList;   ///< ImGui draw list for rendering
    float minX, minY;       ///< Top-left corner
    float maxX, maxY;       ///< Bottom-right corner
    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
    float centerX() const { return (minX + maxX) * 0.5f; }
    float centerY() const { return (minY + maxY) * 0.5f; }
};

/**
 * @brief Visualizer function signature
 * @param op The operator to visualize
 * @param ctx Visualization context with draw list and bounds
 */
using VizFunc = std::function<void(Operator* op, const VizContext& ctx)>;

/**
 * @brief Registry for operator visualizations
 *
 * Allows operators to register custom visualization functions that
 * will be called by the chain visualizer instead of the default.
 *
 * @par Usage (in addon)
 * @code
 * // In kick_viz.cpp
 * #include <vivid/operator_viz.h>
 * #include <vivid/audio/kick.h>
 * #include <imgui.h>
 *
 * static void drawKick(Operator* op, const VizContext& ctx) {
 *     auto* kick = static_cast<audio::Kick*>(op);
 *     // Draw visualization using ctx.drawList...
 * }
 *
 * static OperatorVizRegistry::Registrar<audio::Kick> _kickViz(drawKick);
 * @endcode
 */
class OperatorVizRegistry {
public:
    /**
     * @brief Register a visualizer for an operator type
     * @tparam T Operator type to register for
     * @param func Visualizer function
     */
    template<typename T>
    static void registerVisualizer(VizFunc func) {
        instance().m_visualizers[std::type_index(typeid(T))] = std::move(func);
    }

    /**
     * @brief Check if a visualizer is registered for an operator
     * @param op Operator to check
     * @return true if a custom visualizer exists
     */
    static bool hasVisualizer(Operator* op);

    /**
     * @brief Draw visualization for an operator
     * @param op Operator to visualize
     * @param ctx Visualization context
     * @return true if custom visualizer was used, false if none registered
     */
    static bool draw(Operator* op, const VizContext& ctx);

    /**
     * @brief RAII helper for static registration
     */
    template<typename T>
    struct Registrar {
        explicit Registrar(VizFunc func) {
            OperatorVizRegistry::registerVisualizer<T>(std::move(func));
        }
    };

private:
    static OperatorVizRegistry& instance();
    std::unordered_map<std::type_index, VizFunc> m_visualizers;
};

} // namespace vivid
