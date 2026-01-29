#pragma once

/**
 * @file panel_manager.h
 * @brief Manages layout and focus for devtools panels
 *
 * The PanelManager:
 * - Owns all Panel instances
 * - Handles keyboard shortcuts for panel visibility
 * - Routes input to the focused panel
 * - Manages panel z-ordering and layout
 *
 * Supports two modes:
 * - Flat mode (default): Panels are rendered based on DockSide settings
 * - Layout mode: Panels are arranged in a tree of PanelGroups and SplitContainers
 */

#include <vivid/gui/panel.h>
#include <vivid/gui/layout_node.h>
#include <vivid/gui/dock_zone.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/input_state.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace vivid {

class Context;
class PanelGroup;
class SplitContainer;
class DockManager;

/**
 * @brief Manages multiple devtools panels
 *
 * Features:
 * - Panel registration and lifecycle
 * - Input routing (focus-based)
 * - Layout calculation for docked panels
 * - Visibility state management
 *
 * Usage:
 * @code
 * PanelManager manager;
 * manager.addPanel(std::make_unique<TerminalPanel>());
 * manager.addPanel(std::make_unique<InspectorPanel>());
 *
 * // Each frame:
 * manager.update();
 * manager.render(canvas, input);
 * @endcode
 */
class PanelManager {
public:
    PanelManager();
    ~PanelManager();

    // Non-copyable
    PanelManager(const PanelManager&) = delete;
    PanelManager& operator=(const PanelManager&) = delete;

    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    /**
     * @brief Initialize the panel manager
     * @param ctx Vivid context
     * @param surfaceFormat Surface texture format
     * @return true on success
     */
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat);

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Update all panels
     */
    void update();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /**
     * @brief Render all visible panels
     * @param canvas OverlayCanvas for drawing
     * @param input Input state
     * @param screenWidth Screen width in logical pixels
     * @param screenHeight Screen height in logical pixels
     * @param style UI style for colors and layout
     */
    void render(OverlayCanvas& canvas, const gui::InputState& input,
                float screenWidth, float screenHeight, const UIStyle& style);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Panel Management
    /// @{

    /**
     * @brief Add a panel
     * @param panel Panel to add (ownership transferred)
     */
    void addPanel(std::unique_ptr<Panel> panel);

    /**
     * @brief Get a panel by ID
     * @param id Panel ID
     * @return Pointer to panel, or nullptr if not found
     */
    Panel* getPanel(const std::string& id);

    /**
     * @brief Get a panel by ID (const)
     */
    const Panel* getPanel(const std::string& id) const;

    /**
     * @brief Get a panel by type
     * @tparam T Panel type
     * @return Pointer to panel, or nullptr if not found
     */
    template<typename T>
    T* getPanelAs(const std::string& id) {
        Panel* p = getPanel(id);
        return p ? dynamic_cast<T*>(p) : nullptr;
    }

    /**
     * @brief Get number of panels
     */
    size_t panelCount() const { return m_panels.size(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Visibility
    /// @{

    /**
     * @brief Show a panel
     * @param id Panel ID
     */
    void showPanel(const std::string& id);

    /**
     * @brief Hide a panel
     * @param id Panel ID
     */
    void hidePanel(const std::string& id);

    /**
     * @brief Toggle panel visibility
     * @param id Panel ID
     */
    void togglePanel(const std::string& id);

    /**
     * @brief Check if a panel is visible
     * @param id Panel ID
     * @return true if visible
     */
    bool isPanelVisible(const std::string& id) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Focus
    /// @{

    /**
     * @brief Set focus to a panel
     * @param id Panel ID (empty string to clear focus)
     */
    void setFocus(const std::string& id);

    /**
     * @brief Get the focused panel ID
     * @return Focused panel ID, or empty string if none focused
     */
    const std::string& focusedPanelId() const { return m_focusedPanelId; }

    /**
     * @brief Get the focused panel
     * @return Pointer to focused panel, or nullptr if none
     */
    Panel* focusedPanel();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input
    /// @{

    /**
     * @brief Handle character input
     * @param codepoint Unicode codepoint
     */
    void onChar(uint32_t codepoint);

    /**
     * @brief Handle key down event
     * @param key Key code
     * @param mods Modifier flags
     */
    void onKeyDown(int key, int mods);

    /**
     * @brief Check if any panel consumed input this frame
     */
    bool consumedInput() const { return m_consumedInput; }

    /**
     * @brief Check if any panel is currently being interacted with
     */
    bool isInteracting() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layout
    /// @{

    /**
     * @brief Calculate layout for docked panels
     *
     * Updates panel bounds based on dock positions and screen size.
     * Called automatically during render().
     *
     * @param screenWidth Screen width in logical pixels
     * @param screenHeight Screen height in logical pixels
     */
    void calculateLayout(float screenWidth, float screenHeight);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layout Tree (Advanced)
    /// @{

    /**
     * @brief Enable layout tree mode
     *
     * When enabled, panels are arranged using the layout tree (PanelGroups
     * and SplitContainers) instead of the flat DockSide-based layout.
     */
    void setLayoutMode(bool enabled) { m_layoutMode = enabled; }
    bool isLayoutMode() const { return m_layoutMode; }

    /**
     * @brief Set the root of the layout tree
     * @param root Layout node (ownership transferred)
     */
    void setLayoutRoot(std::unique_ptr<LayoutNode> root);

    /**
     * @brief Get the layout root
     */
    LayoutNode* layoutRoot() { return m_layoutRoot.get(); }
    const LayoutNode* layoutRoot() const { return m_layoutRoot.get(); }

    /**
     * @brief Release ownership of the layout root
     * @return The layout root (ownership transferred to caller)
     */
    std::unique_ptr<LayoutNode> releaseLayoutRoot() { return std::move(m_layoutRoot); }

    /**
     * @brief Build a default layout from current panels
     *
     * Creates a layout tree based on panel DockSide settings:
     * - Fill panels go in the center
     * - Docked panels are placed in SplitContainers
     * - Floating panels are grouped in a PanelGroup
     */
    void buildDefaultLayout();

    /**
     * @brief Save layout to JSON
     * @param path File path to save to
     * @return true on success
     */
    bool saveLayout(const std::string& path) const;

    /**
     * @brief Load layout from JSON
     * @param path File path to load from
     * @return true on success
     */
    bool loadLayout(const std::string& path);

    /**
     * @brief Save layout to JSON string
     * @return JSON string representing the layout
     */
    std::string saveLayoutToString() const;

    /**
     * @brief Load layout from JSON string
     * @param json JSON string to load
     * @return true on success
     */
    bool loadLayoutFromString(const std::string& json);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Docking System
    /// @{

    /**
     * @brief Get the dock manager
     */
    DockManager* dockManager() { return m_dockManager.get(); }
    const DockManager* dockManager() const { return m_dockManager.get(); }

    /**
     * @brief Float a panel (remove from layout, add to floating list)
     * @param panel Panel to float
     * @param pos Position for floating panel
     */
    void floatPanel(Panel* panel, const glm::vec2& pos);

    /**
     * @brief Add a panel to the floating z-order list
     * @param id Panel ID
     */
    void addToFloatingOrder(const std::string& id);

    /**
     * @brief Remove a panel from the floating z-order list
     * @param id Panel ID
     */
    void removeFromFloatingOrder(const std::string& id);

    /**
     * @brief Hide a panel (saves tree slot for restoration)
     * @param id Panel ID
     * @param saveSlot If true, saves the panel's position for restoration
     */
    void hidePanelWithSlot(const std::string& id, bool saveSlot = true);

    /**
     * @brief Show a hidden panel (restores from tree slot if available)
     * @param id Panel ID
     */
    void showPanelFromSlot(const std::string& id);

    /**
     * @brief Get the current layout version
     *
     * Used by TreeSlot to detect when the layout has changed since capture.
     */
    int layoutVersion() const { return m_layoutVersion; }

    /**
     * @brief Increment the layout version
     *
     * Call this when the layout tree structure changes (panels added/removed,
     * splits created/removed, etc.) to invalidate saved TreeSlots.
     */
    void incrementLayoutVersion() { ++m_layoutVersion; }

    /// @}

private:
    void renderFlatMode(OverlayCanvas& canvas, const gui::InputState& input, float scale, const UIStyle& style);
    void renderLayoutMode(OverlayCanvas& canvas, const gui::InputState& input, float scale, const UIStyle& style);

    /**
     * @brief Determine which panel owns input this frame
     * @param input Input state
     * @return Panel ID that should receive input, or empty string if none
     */
    std::string determineInputTarget(const gui::InputState& input);

    std::vector<std::unique_ptr<Panel>> m_panels;
    std::unordered_map<std::string, Panel*> m_panelMap;
    std::string m_focusedPanelId;
    bool m_consumedInput = false;

    // Layout state
    float m_screenWidth = 0;
    float m_screenHeight = 0;

    // Layout tree (optional, when layoutMode is enabled)
    bool m_layoutMode = false;
    std::unique_ptr<LayoutNode> m_layoutRoot;

    // Z-order for floating panels (panel IDs in back-to-front order)
    std::vector<std::string> m_floatingZOrder;

    // Initialization state
    bool m_initialized = false;
    Context* m_ctx = nullptr;
    WGPUTextureFormat m_surfaceFormat;

    // Helper to bring a panel to the front of z-order
    void bringToFront(const std::string& id);

    // Docking system
    std::unique_ptr<DockManager> m_dockManager;

    // Layout version for TreeSlot validation
    // Incremented when the layout tree structure changes
    int m_layoutVersion = 0;

    // Tree slot storage for hidden panels (panel ID -> tree slot)
    std::unordered_map<std::string, TreeSlot> m_savedTreeSlots;

    // Saved floating bounds for hidden panels (panel ID -> bounds)
    std::unordered_map<std::string, glm::vec4> m_savedFloatBounds;

    // Wire up dock manager callbacks to PanelGroups
    void wireGroupDockCallbacks(PanelGroup* group);
};

} // namespace vivid
