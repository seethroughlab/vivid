#include "ui/dialogs/dialog_manager.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/text_edit.h"
#include "ui/ui_command_sink.h"
#include <algorithm>
#include <cctype>

namespace vivid::ui {

namespace {
bool same_package_browser_entries(const std::vector<PackageBrowserEntry>& a,
                                  const std::vector<PackageBrowserEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& lhs = a[i];
        const auto& rhs = b[i];
        if (lhs.name != rhs.name ||
            lhs.description != rhs.description ||
            lhs.version != rhs.version ||
            lhs.author != rhs.author ||
            lhs.category != rhs.category ||
            lhs.tags != rhs.tags ||
            lhs.installed != rhs.installed ||
            lhs.linked != rhs.linked) {
            return false;
        }
    }
    return true;
}
} // namespace

DialogManager::DialogManager(UICommandSink& commands)
    : commands_(commands) {}

bool DialogManager::wants_keyboard() const {
    return about.open
        || save_confirm.open
        || clone_confirm.open
        || mcp_setup.open
        || graph_meta.open
        || asset_browser.open
        || prefs.open
        || pkg_browser.open
        || example_browser.open
        || create_popup.open
        || preset_name.open;
    // Note: core_update is a banner, not a modal — it does NOT want keyboard
}

bool DialogManager::any_open() const {
    return about.open
        || save_confirm.open
        || clone_confirm.open
        || mcp_setup.open
        || graph_meta.open
        || asset_browser.open
        || prefs.open
        || pkg_browser.open
        || example_browser.open
        || create_popup.open
        || preset_name.open;
    // Note: core_update is a banner, not a modal — not included in any_open
}

void DialogManager::open_save_confirm(SaveConfirmAction action) {
    save_confirm.action = action;
    save_confirm.open = true;
}

void DialogManager::open_clone_confirm(const std::string& type_name, const std::string& node_id) {
    clone_confirm.type = type_name;
    clone_confirm.node_id = node_id;
    clone_confirm.project_available = commands_.has_project_clone_destination();
    clone_confirm.destination = clone_confirm.project_available ? 0 : 1;
    clone_confirm.open = true;
}

ActiveTextField DialogManager::resolve_active_field() {
    if (graph_meta.open &&
        graph_meta.active_field >= 0 &&
        graph_meta.active_field < static_cast<int>(graph_meta.fields.size()) &&
        graph_meta.fields[graph_meta.active_field]) {
        return {graph_meta.fields[graph_meta.active_field], filter_printable};
    }
    if (prefs.open && prefs.editing_custom)
        return {&prefs.custom_command, filter_printable};
    if (example_browser.open && example_browser.search_focused)
        return {&example_browser.filter, filter_printable};
    if (pkg_browser.open && pkg_browser.search_focused)
        return {&pkg_browser.filter, filter_printable};
    if (preset_name.open)
        return {&preset_name.buffer, filter_preset_name, SIZE_MAX, true};
    if (create_popup.open)
        return {&create_popup.name_buf, filter_identifier, SIZE_MAX, true};
    return {};
}

void DialogManager::on_char_post_insert() {
    if (example_browser.open) {
        example_browser.scroll = 0;
        example_browser.sel = 0;
        rebuild_example_items();
    } else if (pkg_browser.open) {
        pkg_browser.scroll = 0;
        pkg_browser.sel = 0;
        rebuild_pkg_browser_items();
    } else if (create_popup.open) {
        create_popup.error = commands_.validate_operator_name(create_popup.name_buf);
    }
}

void DialogManager::open_graph_meta_editor(const GraphMetaEditData& data,
                                           TextEditState& text_edit) {
    graph_meta.data = data;
    graph_meta.open = true;
    graph_meta.active_field = 0;
    graph_meta.error.clear();
    graph_meta.preview_picker = {};
    rebuild_graph_meta_fields();
    text_edit.reset(static_cast<int>(graph_meta.fields[graph_meta.active_field]->size()));
}

void DialogManager::set_graph_meta_save_callback(
    std::function<bool(const GraphMetaEditData&, std::string&)> cb) {
    graph_meta.save_callback = std::move(cb);
}

void DialogManager::open_asset_browser(const std::string& node_id, const std::string& param_name,
                                       const std::string& asset_kind, const std::string& current_value) {
    asset_browser.node_id = node_id;
    asset_browser.param_name = param_name;
    asset_browser.asset_kind = asset_kind;
    asset_browser.current_value = current_value;
    asset_browser.error.clear();
    asset_browser.sel = 0;
    asset_browser.scroll = 0.0f;
    asset_browser.open = true;
    refresh_asset_browser_entries();
}

void DialogManager::set_asset_browser_callbacks(AssetBrowserCallbacks callbacks) {
    asset_browser.callbacks = std::move(callbacks);
}

void DialogManager::rebuild_graph_meta_fields() {
    graph_meta.fields = {
        &graph_meta.data.id,
        &graph_meta.data.title,
        &graph_meta.data.description,
        &graph_meta.data.tags_csv,
        &graph_meta.data.difficulty,
        &graph_meta.data.domains_csv,
        &graph_meta.data.requires_packages_csv,
        &graph_meta.data.featured_rank,
        &graph_meta.data.content_kind,
        &graph_meta.data.category,
        &graph_meta.data.family,
        &graph_meta.data.role,
        &graph_meta.data.playability
    };
    for (auto& ctrl : graph_meta.data.preview_controls)
        graph_meta.fields.push_back(&ctrl.label);
    if (graph_meta.active_field < 0) graph_meta.active_field = 0;
    if (graph_meta.active_field >= static_cast<int>(graph_meta.fields.size()))
        graph_meta.active_field = static_cast<int>(graph_meta.fields.size()) - 1;
}

void DialogManager::refresh_asset_browser_entries() {
    asset_browser.entries.clear();
    if (asset_browser.callbacks.list_entries) {
        asset_browser.entries = asset_browser.callbacks.list_entries(asset_browser.asset_kind);
        std::sort(asset_browser.entries.begin(), asset_browser.entries.end(),
                  [](const AssetBrowserEntry& a, const AssetBrowserEntry& b) {
            if (a.scope != b.scope) return a.scope < b.scope;
            if (a.package_name != b.package_name) return a.package_name < b.package_name;
            return a.display_name < b.display_name;
        });
    }
    if (!asset_browser.entries.empty()) {
        int selected = 0;
        for (int i = 0; i < static_cast<int>(asset_browser.entries.size()); ++i) {
            if (asset_browser.entries[i].canonical_path == asset_browser.current_value) {
                selected = i;
                break;
            }
        }
        asset_browser.sel = std::max(0, std::min(selected, static_cast<int>(asset_browser.entries.size()) - 1));
    } else {
        asset_browser.sel = 0;
    }
}

void DialogManager::toggle_preferences() {
    if (prefs.open) {
        // Cancel: revert style
        if (style_ptr_ && prefs.saved_style_sel >= 0 &&
            prefs.saved_style_sel < static_cast<int>(prefs.styles.size())) {
            *style_ptr_ = prefs.styles[prefs.saved_style_sel];
        }
        prefs.open = false;
        prefs.editing_custom = false;
    } else {
        prefs.open = true;
        prefs.editing_custom = false;
        prefs.saved_style_sel = prefs.style_sel;
        // Sync pan gesture selection from current state
        if (pan_gesture_ptr_) {
            if (*pan_gesture_ptr_ == "left") prefs.pan_gesture_sel = 1;
            else if (*pan_gesture_ptr_ == "right") prefs.pan_gesture_sel = 2;
            else prefs.pan_gesture_sel = 0;
        }
        prefs.saved_pan_gesture_sel = prefs.pan_gesture_sel;
    }
}

void DialogManager::set_editor_options(std::vector<std::string> names,
                                       std::vector<std::string> ids,
                                       int current_idx,
                                       const std::string& custom_command) {
    prefs.editor_names = std::move(names);
    prefs.editor_ids = std::move(ids);
    prefs.editor_sel = current_idx;
    prefs.custom_command = custom_command;
}

void DialogManager::set_style_options(std::vector<UIStyle> styles, int current_idx,
                                      std::vector<ThemeInfo> themes) {
    prefs.styles = std::move(styles);
    prefs.themes = std::move(themes);
    prefs.style_sel = current_idx;
    prefs.saved_style_sel = current_idx;
    if (style_ptr_ && current_idx >= 0 &&
        current_idx < static_cast<int>(prefs.styles.size())) {
        *style_ptr_ = prefs.styles[current_idx];
    }
}

// -----------------------------------------------------------------------
// Package browser
// -----------------------------------------------------------------------

bool DialogManager::pkg_action_uses_build_console(PkgBrowserState::ActionKind kind) {
    return kind == PkgBrowserState::ActionKind::Install ||
           kind == PkgBrowserState::ActionKind::Link ||
           kind == PkgBrowserState::ActionKind::Rebuild;
}

void DialogManager::clear_pkg_action_feedback() {
    pkg_browser.action_error.clear();
    pkg_browser.action_error_display.clear();
    pkg_browser.action_error_console_backed = false;
    pkg_browser.footer_action_btn = {};
}

void DialogManager::begin_pkg_action(PkgBrowserState::ActionKind kind, const std::string& action_name) {
    pkg_browser.action_pending = true;
    pkg_browser.action_name = action_name;
    pkg_browser.action_kind = kind;
    clear_pkg_action_feedback();
}

void DialogManager::set_pkg_action_failure(const std::string& error) {
    const std::string fallback = error.empty() ? "Package action failed" : error;
    pkg_browser.action_pending = false;
    pkg_browser.action_name.clear();
    pkg_browser.action_error = fallback;
    if (pkg_action_uses_build_console(pkg_browser.action_kind)) {
        pkg_browser.action_error_display = "Build failed \xe2\x80\x94 see Build Console";
        pkg_browser.action_error_console_backed = true;
    } else {
        pkg_browser.action_error_display = fallback;
        pkg_browser.action_error_console_backed = false;
    }
    pkg_browser.footer_action_btn = {};
    pkg_browser.action_kind = PkgBrowserState::ActionKind::None;
}

void DialogManager::toggle_package_browser() {
    pkg_browser.open = !pkg_browser.open;
    if (pkg_browser.open) example_browser.open = false;
    pkg_browser.search_focused = false;
    pkg_browser.filter.clear();
    if (pkg_browser.open && pkg_browser.callbacks.list_entries) {
        // Refresh catalog and load entries
        if (pkg_browser.callbacks.fetch_state && pkg_browser.callbacks.refresh) {
            auto state = pkg_browser.callbacks.fetch_state();
            if (state == PackageBrowserFetchState::Idle ||
                state == PackageBrowserFetchState::Error) {
                pkg_browser.callbacks.refresh();
            }
        }
        pkg_browser.all = pkg_browser.callbacks.list_entries();
        rebuild_pkg_browser_items();
        pkg_browser.sel = 0;
        pkg_browser.scroll = 0;
        pkg_browser.action_pending = false;
        pkg_browser.action_name.clear();
        pkg_browser.action_kind = PkgBrowserState::ActionKind::None;
        clear_pkg_action_feedback();
    }
}

void DialogManager::set_package_browser_callbacks(PackageBrowserCallbacks callbacks) {
    pkg_browser.callbacks = std::move(callbacks);
}

void DialogManager::notify_pkg_action_complete(bool success, const std::string& error) {
    pkg_browser.action_pending = false;
    pkg_browser.action_name.clear();
    if (success) {
        pkg_browser.action_kind = PkgBrowserState::ActionKind::None;
        clear_pkg_action_feedback();
    } else {
        set_pkg_action_failure(error);
    }
    if (pkg_browser.callbacks.list_entries)
        pkg_browser.all = pkg_browser.callbacks.list_entries();
    rebuild_pkg_browser_items();
}

void DialogManager::refresh_package_browser_snapshot_if_ready() {
    if (!pkg_browser.open) return;
    if (!pkg_browser.callbacks.list_entries || !pkg_browser.callbacks.fetch_state) return;

    auto state = pkg_browser.callbacks.fetch_state();
    if (state != PackageBrowserFetchState::Ready &&
        state != PackageBrowserFetchState::Error) {
        return;
    }

    auto fresh = pkg_browser.callbacks.list_entries();
    if (!same_package_browser_entries(fresh, pkg_browser.all)) {
        pkg_browser.all = std::move(fresh);
        rebuild_pkg_browser_items();
    }
}

void DialogManager::rebuild_pkg_browser_items() {
    pkg_browser.entries.clear();

    // Category names for filtering
    static const char* cat_names[] = { "", "audio", "gpu", "control", "utility" };
    bool filter_installed = (pkg_browser.category == 5);
    const char* cat_filter = (pkg_browser.category >= 1 && pkg_browser.category <= 4)
                             ? cat_names[pkg_browser.category] : nullptr;

    for (const auto& e : pkg_browser.all) {
        // Category filter
        if (filter_installed && !e.installed) continue;
        if (cat_filter && e.category != cat_filter) continue;

        // Text search filter
        if (!pkg_browser.filter.empty()) {
            // Case-insensitive search in name, description, tags
            auto contains = [](const std::string& haystack, const std::string& needle) {
                if (needle.empty()) return true;
                auto it = std::search(haystack.begin(), haystack.end(),
                                      needle.begin(), needle.end(),
                                      [](char a, char b) {
                                          return std::tolower(static_cast<unsigned char>(a)) ==
                                                 std::tolower(static_cast<unsigned char>(b));
                                      });
                return it != haystack.end();
            };

            bool match = contains(e.name, pkg_browser.filter) ||
                         contains(e.description, pkg_browser.filter);
            if (!match) {
                for (const auto& tag : e.tags) {
                    if (contains(tag, pkg_browser.filter)) { match = true; break; }
                }
            }
            if (!match) continue;
        }

        pkg_browser.entries.push_back(e);
    }

    // Clamp selection
    if (pkg_browser.sel >= static_cast<int>(pkg_browser.entries.size()))
        pkg_browser.sel = std::max(0, static_cast<int>(pkg_browser.entries.size()) - 1);
    float pkg_max_scroll = std::max(0.0f, (static_cast<int>(pkg_browser.entries.size()) - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
    if (pkg_browser.scroll > pkg_max_scroll)
        pkg_browser.scroll = pkg_max_scroll;
}

// -----------------------------------------------------------------------
// Example browser
// -----------------------------------------------------------------------

void DialogManager::toggle_example_browser() {
    example_browser.open = !example_browser.open;
    if (example_browser.open) {
        pkg_browser.open = false;
        example_browser.search_focused = false;
        example_browser.filter.clear();
        example_browser.sel = 0;
        example_browser.scroll = 0;
        example_browser.kind = 0;
        example_browser.env = 0;
        example_browser.difficulty = 0;
        example_browser.sort = 0;
        example_browser.core_only = true;
        example_browser.package_only = false;
        example_browser.action_error.clear();
        example_browser.warn_id.clear();
        rebuild_example_items();
    }
}

void DialogManager::set_examples(std::vector<ExampleEntry> examples) {
    example_browser.entries_all = std::move(examples);
    rebuild_example_items();
}

void DialogManager::set_example_open_callback(std::function<void(const std::string&)> cb) {
    example_browser.open_callback = std::move(cb);
}

void DialogManager::set_example_package_checker(
    std::function<bool(const std::vector<std::string>&, std::string&)> cb) {
    example_browser.package_checker = std::move(cb);
}

const ExampleEntry* DialogManager::selected_example_entry() const {
    if (example_browser.sel < 0 ||
        example_browser.sel >= static_cast<int>(example_browser.entries.size()))
        return nullptr;
    return &example_browser.entries[example_browser.sel];
}

size_t DialogManager::selected_example_preview_row_count() const {
    const auto* entry = selected_example_entry();
    if (!entry || entry->content_kind != "instrument")
        return 0;
    return entry->preview_rows.size();
}

void DialogManager::rebuild_example_items() {
    example_browser.entries.clear();

    auto icontains = [](const std::string& haystack, const std::string& needle) {
        if (needle.empty()) return true;
        auto it = std::search(haystack.begin(), haystack.end(),
                              needle.begin(), needle.end(),
                              [](char a, char b) {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        return it != haystack.end();
    };

    for (const auto& e : example_browser.entries_all) {
        if (example_browser.core_only && !e.requires_packages.empty()) continue;
        if (example_browser.package_only && e.requires_packages.empty()) continue;

        if (example_browser.kind == 1) {
            if (e.content_kind != "instrument") continue;
        } else if (example_browser.kind == 2) {
            if (e.content_kind == "instrument") continue;
        }

        if (example_browser.env != 0) {
            static const char* kEnvNames[] = {"", "gpu", "audio", "control", "io"};
            const std::string target = kEnvNames[example_browser.env];
            bool found = false;
            for (const auto& d : e.domains) {
                if (icontains(d, target)) { found = true; break; }
            }
            if (!found) continue;
        }

        if (example_browser.difficulty != 0) {
            static const char* kDiffNames[] = {"", "beginner", "intermediate", "advanced"};
            if (!icontains(e.difficulty, kDiffNames[example_browser.difficulty])) continue;
        }

        if (!example_browser.filter.empty()) {
            bool match = icontains(e.title, example_browser.filter) ||
                         icontains(e.summary, example_browser.filter) ||
                         icontains(e.id, example_browser.filter) ||
                         icontains(e.path, example_browser.filter) ||
                         icontains(e.category, example_browser.filter) ||
                         icontains(e.family, example_browser.filter) ||
                         icontains(e.package_name, example_browser.filter);
            if (!match) {
                for (const auto& t : e.tags) {
                    if (icontains(t, example_browser.filter)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match) continue;
        }

        example_browser.entries.push_back(e);
    }

    if (example_browser.sort == 0) {
        std::sort(example_browser.entries.begin(), example_browser.entries.end(),
                  [](const ExampleEntry& a, const ExampleEntry& b) {
                      bool a_inst = (a.content_kind == "instrument");
                      bool b_inst = (b.content_kind == "instrument");
                      if (a_inst != b_inst) return !a_inst;
                      if (a_inst) {
                          if (a.package_name != b.package_name) return a.package_name < b.package_name;
                          if (a.category != b.category) return a.category < b.category;
                          if (a.family != b.family) return a.family < b.family;
                          return a.title < b.title;
                      }
                      if (a.featured_rank != b.featured_rank)
                          return a.featured_rank < b.featured_rank;
                      return a.title < b.title;
                  });
    } else {
        std::sort(example_browser.entries.begin(), example_browser.entries.end(),
                  [](const ExampleEntry& a, const ExampleEntry& b) {
                      return a.title < b.title;
                  });
    }

    if (example_browser.sel >= static_cast<int>(example_browser.entries.size()))
        example_browser.sel = std::max(0, static_cast<int>(example_browser.entries.size()) - 1);
    float ex_max_scroll = std::max(0.0f, (static_cast<int>(example_browser.entries.size()) - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
    if (example_browser.scroll > ex_max_scroll) example_browser.scroll = ex_max_scroll;
}

// -----------------------------------------------------------------------
// Create operator popup
// -----------------------------------------------------------------------

void DialogManager::open_create_popup() {
    create_popup.open = true;
    create_popup.env_sel = 0;
    create_popup.name_buf.clear();
    create_popup.error.clear();
    create_popup.composite = false;
    create_popup.destination = 0;
}

void DialogManager::submit_create_operator(bool empty_variant) {
    VividCreateOperatorRequest req;
    req.name = create_popup.name_buf;
    req.kind = static_cast<VividOperatorKind>(create_popup.env_sel);

    if (empty_variant) {
        req.variant = "empty";
    } else if (create_popup.composite) {
        req.variant = "composite";
    }

    const char* dest_strs[] = { "auto", "project", "core" };
    req.destination = dest_strs[create_popup.destination];

    if (commands_.create_operator(req, &create_popup.error)) {
        create_popup.open = false;
    }
}

void DialogManager::reset_create_env_defaults() {
    create_popup.composite = false;
}

// -----------------------------------------------------------------------
// Preset name popup
// -----------------------------------------------------------------------

void DialogManager::open_preset_name_popup(const std::string& node_id) {
    preset_name.open = true;
    preset_name.buffer.clear();
    preset_name.node_id = node_id;
}

// -----------------------------------------------------------------------
// Core update notice (banner)
// -----------------------------------------------------------------------

void DialogManager::show_core_update_notice(const std::string& version,
                                            const std::string& summary) {
    core_update.open = true;
    core_update.version = version;
    core_update.summary = summary;
}

void DialogManager::clear_core_update_notice() {
    core_update.open = false;
    core_update.version.clear();
    core_update.summary.clear();
    core_update.button_rects.clear();
}

void DialogManager::set_core_update_notice_callbacks(std::function<void()> install_cb,
                                                     std::function<void()> skip_cb,
                                                     std::function<void()> later_cb) {
    core_update.on_install = std::move(install_cb);
    core_update.on_skip = std::move(skip_cb);
    core_update.on_later = std::move(later_cb);
}

} // namespace vivid::ui
