#pragma once

/**
 * @file preset_panel.h
 * @brief Snapshot/preset management panel
 *
 * Floating panel for saving, recalling, and crossfading chain snapshots.
 * - Scrollable list of named snapshots with active highlight
 * - "+" button to capture current state
 * - Click to recall (hard cut or crossfade)
 * - Crossfade duration slider
 * - Number keys 1-9 recall by position (via ShortcutManager)
 * - Toggled with Cmd+3
 */

#include <vivid/gui/panel.h>
#include <memory>
#include <string>

namespace vivid {

class Chain;
class SnapshotStore;

/**
 * @brief Preset/snapshot management panel
 */
class PresetPanel : public Panel {
public:
    PresetPanel();
    ~PresetPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;
    void onKeyDown(int key, int mods) override;

    /**
     * @brief Set the chain and snapshot store to manage
     *
     * Called by DevTools after a chain is loaded. The PresetPanel does not
     * own the chain or store — it holds non-owning pointers.
     */
    void setChain(Chain* chain, const std::string& projectDir);

    /** @brief Get crossfade duration setting */
    float crossfadeDuration() const { return m_crossfadeDuration; }

    /** @brief Set crossfade duration */
    void setCrossfadeDuration(float seconds) { m_crossfadeDuration = seconds; }

private:
    void autoSave();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    Chain* m_chain = nullptr;
    SnapshotStore* m_store = nullptr;
    std::string m_projectDir;
    float m_crossfadeDuration = 0.0f;  // 0 = hard cut
};

} // namespace vivid
