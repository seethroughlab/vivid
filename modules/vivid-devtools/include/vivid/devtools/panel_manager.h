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
 */

#include <vivid/devtools/panel.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace vivid {

class Context;

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
     * @param input Frame input state
     * @param screenWidth Screen width in logical pixels
     * @param screenHeight Screen height in logical pixels
     */
    void render(OverlayCanvas& canvas, const FrameInput& input,
                float screenWidth, float screenHeight);

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

private:
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::unordered_map<std::string, Panel*> m_panelMap;
    std::string m_focusedPanelId;
    bool m_consumedInput = false;

    // Layout state
    float m_screenWidth = 0;
    float m_screenHeight = 0;

    // Initialization state
    bool m_initialized = false;
    Context* m_ctx = nullptr;
    WGPUTextureFormat m_surfaceFormat;
};

} // namespace vivid
