#pragma once

#include "ui/graph/dialog_types.h"
#include "ui/rendering/overlay_layouts.h"
#include "ui/active_text_field.h"
#include "ui/style/ui_style.h"
#include "ui/style/theme_loader.h"
#include "operator_api/create_request.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>

namespace vivid::ui {

class Renderer2D;
class UICommandSink;
struct MouseState;
struct TextEditState;

class DialogManager {
public:
    explicit DialogManager(UICommandSink& commands);

    // --- State structs ---

    struct AboutState {
        bool open = false;
        float scroll = 0.0f;
        float max_scroll = 0.0f;
    };

    struct SaveConfirmState {
        bool open = false;
        SaveConfirmAction action = SaveConfirmAction::kNewGraph;
    };

    struct CloneConfirmState {
        bool open = false;
        std::string type;
        bool project_available = false;
        int destination = 0; // 0=Project Package, 1=Core
    };

    struct McpSetupState {
        bool open = false;
        std::string mcp_dir;  // directory containing vivid_mcp.py / vivid_opdev_mcp.py

        struct ButtonRect { float x, y, w, h; int action; };  // action: 0=copy_vivid, 1=copy_opdev, 2=done, 3=close
        std::vector<ButtonRect> button_rects;

        struct ProjectConfig {
            bool scanned = false;
            std::string scanned_for_path;
            bool vivid_configured = false;
            bool opdev_configured = false;
            std::string mcp_json_dir;
        };
        ProjectConfig project_config;

        // Ping timestamps updated each frame by NodeGraphUI
        uint64_t mcp_main_last_ping_ms = 0;
        uint64_t mcp_opdev_last_ping_ms = 0;
        // Current graph path for project config scanning
        std::string graph_path;
    };

    struct GraphMetaState {
        bool open = false;
        int active_field = 0;
        std::vector<std::string*> fields;
        GraphMetaEditData data;
        std::string error;
        std::function<bool(const GraphMetaEditData&, std::string&)> save_callback;
    };

    struct PrefsState {
        bool open = false;
        int editor_sel = 0;
        std::vector<std::string> editor_names;
        std::vector<std::string> editor_ids;
        std::string custom_command;
        bool editing_custom = false;
        int style_sel = 0;
        std::vector<UIStyle> styles;
        std::vector<ThemeInfo> themes;
        int saved_style_sel = 0;
        int pan_gesture_sel = 1;       // 0=middle, 1=left, 2=right
        int saved_pan_gesture_sel = 1;
    };

    struct PkgBrowserState {
        enum class ActionKind {
            None,
            Install,
            Uninstall,
            Unlink,
            Link,
            Rebuild,
        };
        bool open = false;
        bool search_focused = false;
        std::string filter;
        int sel = 0;
        float scroll = 0.0f;
        int category = 0;   // 0=All, 1=Audio, 2=GPU, 3=Control, 4=Utility, 5=Installed
        std::array<float, 6> tab_widths{};
        std::vector<PackageBrowserEntry> entries;   // filtered snapshot
        std::vector<PackageBrowserEntry> all;       // full snapshot
        PackageBrowserCallbacks callbacks{};
        bool action_pending = false;
        std::string action_name;
        ActionKind action_kind = ActionKind::None;
        std::string action_error;
        std::string action_error_display;
        bool action_error_console_backed = false;
        OverlayRect footer_action_btn{};  // populated during draw
    };

    struct ExampleBrowserState {
        bool open = false;
        bool search_focused = false;
        std::string filter;
        int sel = 0;
        float scroll = 0.0f;
        int env = 0;
        int difficulty = 0;
        int sort = 0;
        bool core_only = true;
        bool package_only = false;
        std::array<float, 5> env_tab_widths{};
        std::array<float, 4> diff_tab_widths{};
        std::array<float, 2> sort_tab_widths{};
        std::vector<ExampleEntry> entries_all;
        std::vector<ExampleEntry> entries;
        std::string action_error;
        std::string warn_id;
        std::function<void(const std::string&)> open_callback;
        std::function<bool(const std::vector<std::string>&, std::string&)> package_checker;
    };

    struct CreatePopupState {
        bool open = false;
        int env_sel = 0;            // 0=control, 1=audio, 2=gpu
        std::string name_buf;
        std::string error;
        bool composite = false;     // variant checkbox, control-only
        int destination = 0;        // 0=auto, 1=project, 2=core
    };

    struct PresetNameState {
        bool open = false;
        std::string buffer;
        std::string node_id;
    };

    struct CoreUpdateState {
        bool open = false;
        std::string version;
        std::string summary;
        struct ButtonRect { float x, y, w, h; int action; };
        std::vector<ButtonRect> button_rects;
        std::function<void()> on_install;
        std::function<void()> on_skip;
        std::function<void()> on_later;
    };

    // --- Per-dialog state (public for NodeGraphUI forwarding during migration) ---
    AboutState about;
    SaveConfirmState save_confirm;
    CloneConfirmState clone_confirm;
    McpSetupState mcp_setup;
    GraphMetaState graph_meta;
    PrefsState prefs;
    PkgBrowserState pkg_browser;
    ExampleBrowserState example_browser;
    CreatePopupState create_popup;
    PresetNameState preset_name;
    CoreUpdateState core_update;

    // --- Save confirm callbacks (set by main.cpp via NodeGraphUI forwarding) ---
    std::function<void()> on_save_confirm_save;
    std::function<void()> on_save_confirm_dont_save;
    std::function<void()> on_save_confirm_cancel;

    // --- Save confirm queries ---
    bool save_confirm_open() const { return save_confirm.open; }
    SaveConfirmAction save_confirm_action() const { return save_confirm.action; }

    // --- Open helpers ---
    void open_save_confirm(SaveConfirmAction action);
    void open_clone_confirm(const std::string& type_name);
    void open_mcp_setup() { mcp_setup.open = true; }

    // --- MCP setup ---
    void set_mcp_dir(const std::string& dir) { mcp_setup.mcp_dir = dir; }

    // --- Graph meta editor ---
    void open_graph_meta_editor(const GraphMetaEditData& data, TextEditState& text_edit);
    void set_graph_meta_save_callback(
        std::function<bool(const GraphMetaEditData&, std::string&)> cb);
    const GraphMetaEditData& graph_meta_data() const { return graph_meta.data; }

    // --- Preferences ---
    void toggle_preferences();
    void set_editor_options(std::vector<std::string> names, std::vector<std::string> ids,
                            int current_idx = 0, const std::string& custom_command = "");
    void set_style_options(std::vector<UIStyle> styles, int current_idx,
                            std::vector<ThemeInfo> themes = {});
    bool prefs_open() const { return prefs.open; }

    // Style/pan pointer — set during construction or via setter.
    // toggle_preferences and update_preferences use these to save/revert live style.
    void set_style_ptr(UIStyle* s) { style_ptr_ = s; }
    void set_pan_gesture_ptr(std::string* p) { pan_gesture_ptr_ = p; }

    // --- Package browser ---
    void toggle_package_browser();
    void set_package_browser_callbacks(PackageBrowserCallbacks callbacks);
    void notify_pkg_action_complete(bool success, const std::string& error);
    const std::vector<PackageBrowserEntry>& package_browser_entries() const { return pkg_browser.entries; }
    bool pkg_browser_open() const { return pkg_browser.open; }

    // --- Example browser ---
    void toggle_example_browser();
    void set_examples(std::vector<ExampleEntry> examples);
    void set_example_open_callback(std::function<void(const std::string&)> cb);
    void set_example_package_checker(
        std::function<bool(const std::vector<std::string>&, std::string&)> cb);
    bool example_browser_open() const { return example_browser.open; }

    // --- Create operator popup ---
    void open_create_popup();
    bool create_popup_open() const { return create_popup.open; }

    // --- Preset name popup ---
    void open_preset_name_popup(const std::string& node_id);
    bool preset_name_open() const { return preset_name.open; }

    // --- Core update notice (banner, not modal) ---
    void show_core_update_notice(const std::string& version,
                                 const std::string& summary = "");
    void clear_core_update_notice();
    void set_core_update_notice_callbacks(std::function<void()> install_cb,
                                          std::function<void()> skip_cb,
                                          std::function<void()> later_cb);
    bool core_update_open() const { return core_update.open; }

    // --- Queries ---
    bool wants_keyboard() const;
    bool any_open() const;

    // --- Per-frame methods ---
    void set_frame_counter(uint64_t fc) { frame_counter_ = fc; }
    void draw(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
              float popup_opacity, uint32_t win_w, uint32_t win_h,
              const TextEditState& text_edit = {}, bool cursor_blink = false);
    void draw_core_update_banner(Renderer2D& tr, const UIStyle& style,
                                 float banner_y, float max_w);
    bool on_key(int key, int action, int mods, TextEditState& text_edit,
                float& cursor_blink_time);
    bool on_scroll(float y_offset);
    void update(MouseState& mouse, uint32_t win_w, uint32_t win_h);

    // --- Character input callback (called after text_edit_insert for per-field rebuild) ---
    void on_char_post_insert();

    // --- Dialog-specific resolve for text fields ---
    ActiveTextField resolve_active_field();

    // --- About ---
    void open_about() { about.open = true; about.scroll = 0.0f; }

private:
    UICommandSink& commands_;
    UIStyle* style_ptr_ = nullptr;
    std::string* pan_gesture_ptr_ = nullptr;
    uint64_t frame_counter_ = 0;

    // --- Package browser helpers ---
    void rebuild_pkg_browser_items();
    void rebuild_example_items();
    void refresh_package_browser_snapshot_if_ready();
    void clear_pkg_action_feedback();
    void begin_pkg_action(PkgBrowserState::ActionKind kind, const std::string& action_name);
    void set_pkg_action_failure(const std::string& error);
    static bool pkg_action_uses_build_console(PkgBrowserState::ActionKind kind);

    // --- Drawing (dialog_manager_draw.cpp) ---
    void draw_about(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                    float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_save_confirm(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                           float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_clone_confirm(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                            float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_mcp_setup(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                        float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_graph_meta_editor(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                float popup_opacity, uint32_t win_w, uint32_t win_h,
                                const TextEditState& text_edit, bool cursor_blink);
    void draw_preferences(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                          float popup_opacity, uint32_t win_w, uint32_t win_h,
                          const TextEditState& text_edit, bool cursor_blink);
    void draw_package_browser(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                              float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_example_browser(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                              float popup_opacity, uint32_t win_w, uint32_t win_h);
    void draw_create_popup(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                           float popup_opacity, uint32_t win_w, uint32_t win_h,
                           const TextEditState& text_edit, bool cursor_blink);
    void draw_preset_name_popup(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                float popup_opacity, uint32_t win_w, uint32_t win_h,
                                const TextEditState& text_edit, bool cursor_blink);

    // --- Input (dialog_manager_input.cpp) ---
    void update_about(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_save_confirm(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_clone_confirm(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_mcp_setup(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_graph_meta_editor(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_preferences(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_package_browser(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_example_browser(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_create_popup(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_preset_name_popup(MouseState& mouse, uint32_t win_w, uint32_t win_h);
    void update_core_update_buttons(MouseState& mouse);
    void submit_create_operator(bool empty_variant);
    void reset_create_env_defaults();

    // --- MCP helpers ---
    void scan_mcp_project_config();
};

} // namespace vivid::ui
