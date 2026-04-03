#include "ui/dialogs/dialog_manager.h"
#include "ui/graph/node_graph.h"
#include "ui/rendering/overlay_layouts.h"
#include "ui/style/theme_loader.h"
#include "ui/ui_command_sink.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/dialogs/file_dialog.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>

namespace vivid::ui {

bool DialogManager::on_key(int key, int /*action*/, int /*mods*/,
                           TextEditState& text_edit, float& /*cursor_blink_time*/) {
    if (save_confirm.open) {
        if (key == GLFW_KEY_ESCAPE) {
            save_confirm.open = false;
            if (on_save_confirm_cancel) on_save_confirm_cancel();
        } else if (key == GLFW_KEY_ENTER) {
            save_confirm.open = false;
            if (on_save_confirm_save) on_save_confirm_save();
        }
        return true;
    }

    if (clone_confirm.open) {
        if (key == GLFW_KEY_ESCAPE) {
            clone_confirm.open = false;
        } else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
            if (clone_confirm.project_available) {
                clone_confirm.destination = 1 - clone_confirm.destination;
            }
        } else if (key == GLFW_KEY_ENTER) {
            const char* destination = (clone_confirm.destination == 0) ? "project" : "core";
            if (!clone_confirm.node_id.empty())
                commands_.clone_and_edit_for_node(clone_confirm.node_id, clone_confirm.type, destination);
            else
                commands_.clone_and_edit(clone_confirm.type, destination);
            clone_confirm.open = false;
        }
        return true;
    }

    if (example_browser.open) {
        if (key == GLFW_KEY_ESCAPE) {
            example_browser.open = false;
            example_browser.filter.clear();
        } else if (key == GLFW_KEY_UP) {
            example_browser.sel = std::max(0, example_browser.sel - 1);
            if (example_browser.sel * kPkgBrowserItemH < example_browser.scroll)
                example_browser.scroll = example_browser.sel * kPkgBrowserItemH;
        } else if (key == GLFW_KEY_DOWN) {
            int max_sel = static_cast<int>(example_browser.entries.size()) - 1;
            example_browser.sel = std::min(max_sel, example_browser.sel + 1);
            if ((example_browser.sel + 1) * kPkgBrowserItemH > example_browser.scroll + kPkgBrowserMaxVisible * kPkgBrowserItemH)
                example_browser.scroll = (example_browser.sel - kPkgBrowserMaxVisible + 1) * kPkgBrowserItemH;
        } else if (key == GLFW_KEY_BACKSPACE && example_browser.search_focused) {
            text_edit_backspace(example_browser.filter, text_edit);
            example_browser.scroll = 0.0f;
            example_browser.sel = 0;
            rebuild_example_items();
        } else if (key == GLFW_KEY_ENTER) {
            if (example_browser.open_callback && example_browser.sel >= 0 &&
                example_browser.sel < static_cast<int>(example_browser.entries.size())) {
                const auto& e = example_browser.entries[example_browser.sel];
                std::string missing;
                bool ok = true;
                if (example_browser.package_checker) {
                    ok = example_browser.package_checker(e.requires_packages, missing);
                }
                if (ok) {
                    example_browser.action_error.clear();
                    example_browser.warn_id.clear();
                    example_browser.open_callback(e.path);
                    example_browser.open = false;
                } else if (example_browser.warn_id == e.id) {
                    example_browser.action_error = "Opening anyway with missing package: " + missing;
                    example_browser.open_callback(e.path);
                    example_browser.open = false;
                } else {
                    example_browser.warn_id = e.id;
                    example_browser.action_error =
                        "Missing package: " + missing + " (press Enter again to open anyway)";
                }
            }
        }
        return true;
    }

    if (pkg_browser.open) {
        if (key == GLFW_KEY_ESCAPE) {
            pkg_browser.open = false;
            pkg_browser.filter.clear();
        } else if (key == GLFW_KEY_UP) {
            pkg_browser.sel = std::max(0, pkg_browser.sel - 1);
            if (pkg_browser.sel * kPkgBrowserItemH < pkg_browser.scroll)
                pkg_browser.scroll = pkg_browser.sel * kPkgBrowserItemH;
        } else if (key == GLFW_KEY_DOWN) {
            int max_sel = static_cast<int>(pkg_browser.entries.size()) - 1;
            pkg_browser.sel = std::min(max_sel, pkg_browser.sel + 1);
            if ((pkg_browser.sel + 1) * kPkgBrowserItemH > pkg_browser.scroll + kPkgBrowserMaxVisible * kPkgBrowserItemH)
                pkg_browser.scroll = (pkg_browser.sel - kPkgBrowserMaxVisible + 1) * kPkgBrowserItemH;
        } else if (key == GLFW_KEY_ENTER) {
            // Trigger install/remove on selected entry
            if (pkg_browser.sel >= 0 &&
                pkg_browser.sel < static_cast<int>(pkg_browser.entries.size())) {
                const auto& entry = pkg_browser.entries[pkg_browser.sel];
                if (!pkg_browser.action_pending) {
                pkg_browser.action_error.clear();
                pkg_browser.action_pending = true;
                pkg_browser.action_name = entry.name;
                if (entry.installed) {
                    if (entry.linked) {
                        if (!pkg_browser.callbacks.unlink ||
                            !pkg_browser.callbacks.unlink(entry.name, pkg_browser.action_error)) {
                            pkg_browser.action_pending = false;
                            pkg_browser.action_name.clear();
                            if (pkg_browser.action_error.empty())
                                pkg_browser.action_error = "Failed to unlink " + entry.name;
                        }
                    } else if (!pkg_browser.callbacks.uninstall ||
                               !pkg_browser.callbacks.uninstall(entry.name, pkg_browser.action_error)) {
                        pkg_browser.action_pending = false;
                        pkg_browser.action_name.clear();
                        if (pkg_browser.action_error.empty())
                            pkg_browser.action_error = "Failed to uninstall " + entry.name;
                    }
                } else {
                    if (!pkg_browser.callbacks.install ||
                        !pkg_browser.callbacks.install(entry.name, pkg_browser.action_error)) {
                        pkg_browser.action_pending = false;
                        pkg_browser.action_name.clear();
                        if (pkg_browser.action_error.empty())
                            pkg_browser.action_error = "Failed to install " + entry.name;
                    }
                }
                }
            }
        } else if (key == GLFW_KEY_BACKSPACE && pkg_browser.search_focused) {
            text_edit_backspace(pkg_browser.filter, text_edit);
            pkg_browser.scroll = 0;
            pkg_browser.sel = 0;
            rebuild_pkg_browser_items();
        }
        return true;
    }

    if (prefs.open) {
        if (key == GLFW_KEY_ESCAPE) {
            // Cancel: revert style
            if (style_ptr_ && prefs.saved_style_sel >= 0 &&
                prefs.saved_style_sel < static_cast<int>(prefs.styles.size())) {
                *style_ptr_ = prefs.styles[prefs.saved_style_sel];
                prefs.style_sel = prefs.saved_style_sel;
            }
            prefs.open = false;
            prefs.editing_custom = false;
        } else if (prefs.editing_custom) {
            if (key == GLFW_KEY_BACKSPACE)
                text_edit_backspace(prefs.custom_command, text_edit);
        }
        return true;
    }

    if (graph_meta.open) {
        if (key == GLFW_KEY_ESCAPE) {
            graph_meta.open = false;
            graph_meta.error.clear();
        } else if (key == GLFW_KEY_TAB || key == GLFW_KEY_DOWN) {
            graph_meta.active_field =
                (graph_meta.active_field + 1) % static_cast<int>(graph_meta.fields.size());
            if (auto* f = graph_meta.fields[graph_meta.active_field])
                text_edit.reset(static_cast<int>(f->size()));
        } else if (key == GLFW_KEY_UP) {
            graph_meta.active_field--;
            if (graph_meta.active_field < 0)
                graph_meta.active_field = static_cast<int>(graph_meta.fields.size()) - 1;
            if (auto* f = graph_meta.fields[graph_meta.active_field])
                text_edit.reset(static_cast<int>(f->size()));
        } else if (key == GLFW_KEY_BACKSPACE &&
                   graph_meta.active_field >= 0 &&
                   graph_meta.active_field < static_cast<int>(graph_meta.fields.size())) {
            auto* field = graph_meta.fields[graph_meta.active_field];
            if (field) text_edit_backspace(*field, text_edit);
        } else if (key == GLFW_KEY_ENTER) {
            if (graph_meta.save_callback) {
                std::string err;
                if (graph_meta.save_callback(graph_meta.data, err)) {
                    graph_meta.open = false;
                    graph_meta.error.clear();
                } else {
                    graph_meta.error = err.empty() ? "Failed to save meta" : err;
                }
            }
        }
        return true;
    }

    if (preset_name.open) {
        if (key == GLFW_KEY_ESCAPE) {
            preset_name.open = false;
        } else if (key == GLFW_KEY_ENTER) {
            if (!preset_name.buffer.empty()) {
                commands_.save_preset(preset_name.node_id, preset_name.buffer);
                preset_name.open = false;
            }
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(preset_name.buffer, text_edit);
        }
        return true;
    }

    if (create_popup.open) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                create_popup.open = false;
                break;
            case GLFW_KEY_LEFT:
                if (create_popup.env_sel > 0) {
                    create_popup.env_sel--;
                    reset_create_env_defaults();
                }
                break;
            case GLFW_KEY_RIGHT:
                if (create_popup.env_sel < 2) {
                    create_popup.env_sel++;
                    reset_create_env_defaults();
                }
                break;
            case GLFW_KEY_TAB:
                break;  // single field, no-op
            case GLFW_KEY_BACKSPACE:
                text_edit_backspace(create_popup.name_buf, text_edit);
                create_popup.error = create_popup.name_buf.empty() ? "" :
                    commands_.validate_operator_name(create_popup.name_buf);
                break;
            case GLFW_KEY_ENTER:
                if (!create_popup.name_buf.empty() && create_popup.error.empty()) {
                    submit_create_operator(false);
                }
                break;
        }
        return true;
    }

    if (mcp_setup.open) {
        if (key == GLFW_KEY_ESCAPE) {
            mcp_setup.open = false;
            mcp_setup.project_config.scanned = false;
        }
        return true;
    }

    if (about.open) {
        if (key == GLFW_KEY_ESCAPE)
            about.open = false;
        return true;
    }
    return false;
}

bool DialogManager::on_scroll(float y_offset) {
    if (example_browser.open && !example_browser.entries.empty()) {
        example_browser.scroll -= y_offset * kPkgBrowserItemH;
        float max_scroll = std::max(0.0f, (static_cast<int>(example_browser.entries.size()) - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
        example_browser.scroll = std::max(0.0f, std::min(example_browser.scroll, max_scroll));
        return true;
    }
    if (pkg_browser.open && !pkg_browser.entries.empty()) {
        pkg_browser.scroll -= y_offset * kPkgBrowserItemH;
        float max_scroll = std::max(0.0f, (static_cast<int>(pkg_browser.entries.size()) - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
        pkg_browser.scroll = std::max(0.0f, std::min(pkg_browser.scroll, max_scroll));
        return true;
    }
    if (about.open) {
        about.scroll -= y_offset * 20.0f;
        about.scroll = std::max(0.0f, std::min(about.scroll, about.max_scroll));
        return true;
    }
    return false;
}

void DialogManager::update(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    refresh_package_browser_snapshot_if_ready();
    update_package_browser(mouse, win_w, win_h);
    update_example_browser(mouse, win_w, win_h);
    update_save_confirm(mouse, win_w, win_h);
    update_clone_confirm(mouse, win_w, win_h);
    update_mcp_setup(mouse, win_w, win_h);
    update_graph_meta_editor(mouse, win_w, win_h);
    update_preferences(mouse, win_w, win_h);
    update_about(mouse, win_w, win_h);
    update_create_popup(mouse, win_w, win_h);
    update_preset_name_popup(mouse, win_w, win_h);
    update_core_update_buttons(mouse);
}

void DialogManager::update_about(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!about.open) return;
    if (!mouse.left_clicked) return;

    OverlayPanelLayout layout = compute_about_layout(win_w, win_h);

    // Click outside panel closes modal
    if (!overlay_contains(layout, mouse.x, mouse.y)) {
        about.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    // Close button
    float btn_w = 80.0f, btn_h = 24.0f;
    float btn_x = layout.px + (layout.pw - btn_w) * 0.5f;
    float btn_y = layout.status_y;
    if (mouse.x >= btn_x && mouse.x <= btn_x + btn_w &&
        mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        about.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Save confirmation dialog interaction
// -----------------------------------------------------------------------
void DialogManager::update_save_confirm(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!save_confirm.open) return;
    if (!mouse.left_clicked) return;

    float dw = 360.0f, dh = 90.0f;
    float dx = (static_cast<float>(win_w) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h) - dh) * 0.5f;

    float btn_w = 80.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float total_btn_w = btn_w * 3 + 12.0f * 2;
    float btn_start_x = dx + (dw - total_btn_w) * 0.5f;
    float cancel_x = btn_start_x;
    float dont_save_x = btn_start_x + btn_w + 12.0f;
    float save_x = btn_start_x + (btn_w + 12.0f) * 2;

    if (mouse.x >= cancel_x && mouse.x <= cancel_x + btn_w &&
        mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        save_confirm.open = false;
        if (on_save_confirm_cancel) on_save_confirm_cancel();
    } else if (mouse.x >= dont_save_x && mouse.x <= dont_save_x + btn_w &&
               mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        save_confirm.open = false;
        if (on_save_confirm_dont_save) on_save_confirm_dont_save();
    } else if (mouse.x >= save_x && mouse.x <= save_x + btn_w &&
               mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        save_confirm.open = false;
        if (on_save_confirm_save) on_save_confirm_save();
    } else if (mouse.x < dx || mouse.x > dx + dw ||
               mouse.y < dy || mouse.y > dy + dh) {
        save_confirm.open = false;
        if (on_save_confirm_cancel) on_save_confirm_cancel();
    }
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Clone confirmation dialog interaction
// -----------------------------------------------------------------------
void DialogManager::update_clone_confirm(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!clone_confirm.open || !mouse.left_clicked) return;

    float dw = 360.0f, dh = 108.0f;
    float dx = (static_cast<float>(win_w) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h) - dh) * 0.5f;

    float btn_w = 70.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float clone_x = dx + dw * 0.5f - btn_w - 6.0f;
    float cancel_x = dx + dw * 0.5f + 6.0f;

    float toggle_y = dy + 38.0f;
    float toggle_h = 24.0f;
    float toggle_x = dx + 12.0f;
    float toggle_w = dw - 24.0f;
    float left_w = toggle_w * 0.5f;

    if (clone_confirm.project_available) {
        if (mouse.x >= toggle_x && mouse.x <= toggle_x + left_w &&
            mouse.y >= toggle_y && mouse.y <= toggle_y + toggle_h) {
            clone_confirm.destination = 0;
        } else if (mouse.x >= toggle_x + left_w && mouse.x <= toggle_x + toggle_w &&
                   mouse.y >= toggle_y && mouse.y <= toggle_y + toggle_h) {
            clone_confirm.destination = 1;
        }
    }

    if (mouse.x >= clone_x && mouse.x <= clone_x + btn_w &&
        mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        const char* destination = (clone_confirm.destination == 0) ? "project" : "core";
        if (!clone_confirm.node_id.empty())
            commands_.clone_and_edit_for_node(clone_confirm.node_id, clone_confirm.type, destination);
        else
            commands_.clone_and_edit(clone_confirm.type, destination);
        clone_confirm.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
    } else if (mouse.x >= cancel_x && mouse.x <= cancel_x + btn_w &&
               mouse.y >= btn_y && mouse.y <= btn_y + btn_h) {
        clone_confirm.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
    } else if (mouse.x < dx || mouse.x > dx + dw ||
               mouse.y < dy || mouse.y > dy + dh) {
        clone_confirm.open = false;
    }
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// MCP setup dialog interaction
// -----------------------------------------------------------------------
void DialogManager::update_mcp_setup(MouseState& mouse, uint32_t /*win_w*/, uint32_t /*win_h*/) {
    if (!mcp_setup.open || !mouse.left_clicked) return;

    for (const auto& btn : mcp_setup.button_rects) {
        if (mouse.x >= btn.x && mouse.x <= btn.x + btn.w &&
            mouse.y >= btn.y && mouse.y <= btn.y + btn.h) {
            if (btn.action == 0) {  // Copy vivid JSON
                std::string mcp_py = mcp_setup.mcp_dir.empty()
                    ? "<path_to_vivid>/mcp/vivid_mcp.py"
                    : mcp_setup.mcp_dir + "/vivid_mcp.py";
                std::string json = "{\"vivid\":{\"command\":\"python\",\"args\":[\"" + mcp_py + "\"],\"type\":\"stdio\"}}";
                glfwSetClipboardString(nullptr, json.c_str());
            } else if (btn.action == 1) {  // Copy opdev JSON
                std::string opdev_py = mcp_setup.mcp_dir.empty()
                    ? "<path_to_vivid>/mcp/vivid_opdev_mcp.py"
                    : mcp_setup.mcp_dir + "/vivid_opdev_mcp.py";
                std::string json = "{\"opdev\":{\"command\":\"python\",\"args\":[\"" + opdev_py + "\"],\"type\":\"stdio\"}}";
                glfwSetClipboardString(nullptr, json.c_str());
            } else if (btn.action == 2 || btn.action == 3) {  // Done or close
                mcp_setup.open = false;
                mcp_setup.project_config.scanned = false;
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
    }
    // Backdrop click closes dialog
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Graph meta editor dialog interaction
// -----------------------------------------------------------------------
void DialogManager::update_graph_meta_editor(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!graph_meta.open) return;
    if (!mouse.left_clicked) return;

    OverlayPanelLayout layout = compute_graph_meta_editor_layout(win_w, win_h);
    float pw = layout.pw;
    float ph = layout.ph;
    float px = layout.px;
    float py = layout.py;

    if (!overlay_contains(layout, mouse.x, mouse.y)) {
        graph_meta.open = false;
        graph_meta.error.clear();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    float cx = px + 16.0f;
    float cy = py + 52.0f;
    float label_w = 160.0f;
    float field_h = 24.0f;
    float field_w = pw - 32.0f - label_w;
    float row_gap = 8.0f;
    const int kFieldCount = 8;
    for (int i = 0; i < kFieldCount; ++i) {
        float fy = cy + i * (field_h + row_gap);
        float fx = cx + label_w;
        if (mouse.x >= fx && mouse.x <= fx + field_w &&
            mouse.y >= fy && mouse.y <= fy + field_h) {
            graph_meta.active_field = i;
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
    }

    float by = py + ph - 42.0f;
    float save_w = 80.0f;
    float cancel_w = 90.0f;
    float save_x = px + pw - 16.0f - save_w - 8.0f - cancel_w;
    float cancel_x = save_x + save_w + 8.0f;

    if (mouse.x >= save_x && mouse.x <= save_x + save_w &&
        mouse.y >= by && mouse.y <= by + 24.0f) {
        if (graph_meta.save_callback) {
            std::string err;
            if (graph_meta.save_callback(graph_meta.data, err)) {
                graph_meta.open = false;
                graph_meta.error.clear();
            } else {
                graph_meta.error = err.empty() ? "Failed to save meta" : err;
            }
        }
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    if (mouse.x >= cancel_x && mouse.x <= cancel_x + cancel_w &&
        mouse.y >= by && mouse.y <= by + 24.0f) {
        graph_meta.open = false;
        graph_meta.error.clear();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Preferences panel click handling
// -----------------------------------------------------------------------
void DialogManager::update_preferences(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!prefs.open || !mouse.left_clicked) return;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    int editor_count = static_cast<int>(prefs.editor_names.size());
    int style_count = static_cast<int>(prefs.styles.size());
    bool show_custom = (prefs.editor_sel >= 0 &&
                        prefs.editor_sel < static_cast<int>(prefs.editor_ids.size()) &&
                        prefs.editor_ids[prefs.editor_sel] == "custom");

    float content_h = kPrefsPadY
        + kPrefsRowH + kPrefsSectionGap
        + kPrefsRowH + editor_count * kPrefsRowH
        + (show_custom ? kPrefsRowH + 4 : 0)
        + kPrefsSectionGap
        + kPrefsRowH + style_count * kPrefsRowH
        + kPrefsRowH + 4                          // "Open Themes Folder" link
        + kPrefsSectionGap
        + kPrefsRowH + 3 * kPrefsRowH             // MOUSE section header + 3 radio items
        + kPrefsSectionGap + kPrefsBtnH + kPrefsPadY;

    float pw = kPrefsW;
    float ph = content_h;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Click outside panel -> close (cancel)
    if (mouse.x < px || mouse.x > px + pw ||
        mouse.y < py || mouse.y > py + ph) {
        // Revert style
        if (style_ptr_ && prefs.saved_style_sel >= 0 &&
            prefs.saved_style_sel < static_cast<int>(prefs.styles.size())) {
            *style_ptr_ = prefs.styles[prefs.saved_style_sel];
            prefs.style_sel = prefs.saved_style_sel;
        }
        prefs.pan_gesture_sel = prefs.saved_pan_gesture_sel;
        prefs.open = false;
        prefs.editing_custom = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    float cx = px + kPrefsPadX;
    float inner_w = pw - 2 * kPrefsPadX;
    float cy = py + kPrefsPadY + kPrefsRowH + kPrefsSectionGap;

    // Skip section header
    cy += kPrefsRowH;

    // Editor radio items
    for (int i = 0; i < editor_count; ++i) {
        if (mouse.x >= cx && mouse.x <= cx + inner_w &&
            mouse.y >= cy && mouse.y <= cy + kPrefsRowH) {
            prefs.editor_sel = i;
            prefs.editing_custom = false;
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        cy += kPrefsRowH;
    }

    // Custom command field click
    if (show_custom) {
        cy += 2;
        if (mouse.x >= cx + 18 && mouse.x <= cx + inner_w &&
            mouse.y >= cy && mouse.y <= cy + kPrefsRowH - 2) {
            prefs.editing_custom = true;
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        cy += kPrefsRowH + 2;
    }

    cy += kPrefsSectionGap;

    // Skip STYLE section header
    cy += kPrefsRowH;

    // Style radio items
    for (int i = 0; i < style_count; ++i) {
        if (mouse.x >= cx && mouse.x <= cx + inner_w &&
            mouse.y >= cy && mouse.y <= cy + kPrefsRowH) {
            prefs.style_sel = i;
            // Live preview: apply style immediately
            if (style_ptr_ && i >= 0 && i < static_cast<int>(prefs.styles.size())) {
                *style_ptr_ = prefs.styles[i];
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        cy += kPrefsRowH;
    }

    // "Open Themes Folder" link
    cy += 4;
    if (mouse.x >= cx + 18 && mouse.x <= cx + inner_w &&
        mouse.y >= cy && mouse.y <= cy + kPrefsRowH) {
        open_themes_folder();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    cy += kPrefsRowH;

    cy += kPrefsSectionGap;

    // Skip MOUSE section header
    cy += kPrefsRowH;

    // Pan gesture radio items
    for (int i = 0; i < 3; ++i) {
        if (mouse.x >= cx && mouse.x <= cx + inner_w &&
            mouse.y >= cy && mouse.y <= cy + kPrefsRowH) {
            prefs.pan_gesture_sel = i;
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        cy += kPrefsRowH;
    }

    cy += kPrefsSectionGap;

    // Buttons
    float btn_total = 2 * kPrefsBtnW + 12;
    float save_x = px + (pw - btn_total) * 0.5f;
    float cancel_x = save_x + kPrefsBtnW + 12;

    if (mouse.x >= save_x && mouse.x <= save_x + kPrefsBtnW &&
        mouse.y >= cy && mouse.y <= cy + kPrefsBtnH) {
        // Save
        std::string editor_id;
        if (prefs.editor_sel >= 0 && prefs.editor_sel < static_cast<int>(prefs.editor_ids.size()))
            editor_id = prefs.editor_ids[prefs.editor_sel];
        commands_.set_editor_preference(editor_id, prefs.custom_command);

        if (prefs.style_sel >= 0 && prefs.style_sel < static_cast<int>(prefs.styles.size())) {
            commands_.set_style_preference(prefs.styles[prefs.style_sel].id);
            prefs.saved_style_sel = prefs.style_sel;
        }

        // Pan gesture
        const char* gestures[] = { "middle", "left", "right" };
        if (pan_gesture_ptr_)
            *pan_gesture_ptr_ = gestures[prefs.pan_gesture_sel];
        commands_.set_pan_gesture_preference(gestures[prefs.pan_gesture_sel]);
        prefs.saved_pan_gesture_sel = prefs.pan_gesture_sel;

        prefs.open = false;
        prefs.editing_custom = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    if (mouse.x >= cancel_x && mouse.x <= cancel_x + kPrefsBtnW &&
        mouse.y >= cy && mouse.y <= cy + kPrefsBtnH) {
        // Cancel: revert style
        if (style_ptr_ && prefs.saved_style_sel >= 0 &&
            prefs.saved_style_sel < static_cast<int>(prefs.styles.size())) {
            *style_ptr_ = prefs.styles[prefs.saved_style_sel];
            prefs.style_sel = prefs.saved_style_sel;
        }
        prefs.pan_gesture_sel = prefs.saved_pan_gesture_sel;
        prefs.open = false;
        prefs.editing_custom = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    // Consume click inside panel
    prefs.editing_custom = false;
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Package browser interaction
// -----------------------------------------------------------------------
void DialogManager::update_package_browser(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!pkg_browser.open) return;

    OverlayPanelLayout layout =
        compute_package_browser_layout(win_w, win_h, pkg_browser.entries.size());
    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    float cx = layout.cx;
    float inner_w = layout.inner_w;

    if (!mouse.left_clicked) return;

    if (!overlay_contains(layout, mouse.x, mouse.y)) {
        pkg_browser.open = false;
        pkg_browser.filter.clear();
        pkg_browser.search_focused = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    // Search field click-to-focus
    float search_cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH;
    if (mouse.x >= cx && mouse.x <= cx + inner_w &&
        mouse.y >= search_cy && mouse.y <= search_cy + kPkgBrowserSearchH) {
        pkg_browser.search_focused = true;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    pkg_browser.search_focused = false;

    // Footer action button
    if (!pkg_browser.action_error.empty()) {
        const auto& btn = pkg_browser.footer_action_btn;
        if (btn.w > 0 && mouse.x >= btn.x && mouse.x <= btn.x + btn.w &&
            mouse.y >= btn.y && mouse.y <= btn.y + btn.h) {
            if (pkg_browser.action_error_console_backed && pkg_browser.callbacks.open_build_console) {
                pkg_browser.callbacks.open_build_console();
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
    }

    // Hit-test "Link Local..." button
    static const float kLinkBtnW = 96.0f;
    float link_btn_x = cx + inner_w - kLinkBtnW;
    float link_btn_y = py + kPkgBrowserPadY + (kPkgBrowserHeaderH - kPkgBrowserBtnH) / 2.0f - 2.0f;
    if (mouse.x >= link_btn_x && mouse.x <= link_btn_x + kLinkBtnW &&
        mouse.y >= link_btn_y && mouse.y <= link_btn_y + kPkgBrowserBtnH) {
        mouse.left_clicked = false;
        mouse.left_released = false;
        std::string path = open_directory_dialog();
        if (!path.empty() && pkg_browser.callbacks.link && !pkg_browser.action_pending) {
            begin_pkg_action(PkgBrowserState::ActionKind::Link, path);
            pkg_browser.callbacks.link(path, pkg_browser.action_error);
        }
        return;
    }

    float cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6;
    static const char* tab_labels[] = { "All", "Audio", "GPU", "Control", "Utility", "Installed" };
    float tab_x = cx;
    float tab_gap = 4.0f;
    for (int i = 0; i < 6; ++i) {
        float tw = pkg_browser.tab_widths[i] > 0 ? pkg_browser.tab_widths[i]
                 : static_cast<float>(std::strlen(tab_labels[i])) * 8.0f + 16.0f;
        if (mouse.x >= tab_x && mouse.x <= tab_x + tw &&
            mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
            pkg_browser.category = i;
            pkg_browser.scroll = 0;
            pkg_browser.sel = 0;
            rebuild_pkg_browser_items();
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        tab_x += tw + tab_gap;
    }

    float list_top = layout.list_top;
    int total = static_cast<int>(pkg_browser.entries.size());
    float pkg_list_area_h = layout.visible_count * kPkgBrowserItemH;
    int pkg_first = std::max(0, static_cast<int>(std::floor(pkg_browser.scroll / kPkgBrowserItemH)));
    float pkg_pix_offset = pkg_browser.scroll - pkg_first * kPkgBrowserItemH;
    int pkg_draw_count = std::min(total - pkg_first, kPkgBrowserMaxVisible + 1);

    for (int vi = 0; vi < pkg_draw_count; ++vi) {
        int i = pkg_first + vi;
        float iy = list_top - pkg_pix_offset + vi * kPkgBrowserItemH;
        if (mouse.y < std::max(iy, list_top) || mouse.y > std::min(iy + kPkgBrowserItemH, list_top + pkg_list_area_h)) continue;
        if (mouse.x < cx || mouse.x > cx + inner_w) continue;

        OverlayRect btn = compute_package_action_button_rect(layout, iy);
        if (overlay_contains(btn, mouse.x, mouse.y)) {
            if (!pkg_browser.action_pending) {
                const auto& entry = pkg_browser.entries[i];
                if (entry.needs_rebuild) {
                    begin_pkg_action(PkgBrowserState::ActionKind::Rebuild, entry.name);
                    if (!pkg_browser.callbacks.rebuild ||
                        !pkg_browser.callbacks.rebuild(entry.name, pkg_browser.action_error)) {
                        set_pkg_action_failure(pkg_browser.action_error.empty()
                            ? "Failed to rebuild " + entry.name
                            : pkg_browser.action_error);
                    }
                } else if (entry.installed) {
                    if (entry.linked) {
                        begin_pkg_action(PkgBrowserState::ActionKind::Unlink, entry.name);
                        if (!pkg_browser.callbacks.unlink ||
                            !pkg_browser.callbacks.unlink(entry.name, pkg_browser.action_error)) {
                            set_pkg_action_failure(pkg_browser.action_error.empty()
                                ? "Failed to unlink " + entry.name
                                : pkg_browser.action_error);
                        }
                    } else {
                        begin_pkg_action(PkgBrowserState::ActionKind::Uninstall, entry.name);
                        if (!pkg_browser.callbacks.uninstall ||
                            !pkg_browser.callbacks.uninstall(entry.name, pkg_browser.action_error)) {
                            set_pkg_action_failure(pkg_browser.action_error.empty()
                                ? "Failed to uninstall " + entry.name
                                : pkg_browser.action_error);
                        }
                    }
                } else {
                    begin_pkg_action(PkgBrowserState::ActionKind::Install, entry.name);
                    if (!pkg_browser.callbacks.install ||
                        !pkg_browser.callbacks.install(entry.name, pkg_browser.action_error)) {
                        set_pkg_action_failure(pkg_browser.action_error.empty()
                            ? "Failed to install " + entry.name
                            : pkg_browser.action_error);
                    }
                }
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }

        pkg_browser.sel = i;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Example browser interaction
// -----------------------------------------------------------------------
void DialogManager::update_example_browser(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!example_browser.open) return;

    OverlayPanelLayout layout =
        compute_example_browser_layout(win_w, win_h, example_browser.entries.size());
    int visible_count = layout.visible_count;
    float px = layout.px;
    float py = layout.py;
    float cx = layout.cx;
    float inner_w = layout.inner_w;
    if (!mouse.left_clicked) return;

    if (!overlay_contains(layout, mouse.x, mouse.y)) {
        example_browser.open = false;
        example_browser.filter.clear();
        example_browser.search_focused = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    // Search field click-to-focus
    float search_cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH;
    if (mouse.x >= cx && mouse.x <= cx + inner_w &&
        mouse.y >= search_cy && mouse.y <= search_cy + kPkgBrowserSearchH) {
        example_browser.search_focused = true;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    example_browser.search_focused = false;

    float cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6;
    static const char* env_tabs[] = { "All", "GPU", "Audio", "Control", "I/O" };
    float tx = cx;
    for (int i = 0; i < 5; ++i) {
        float tw = example_browser.env_tab_widths[i] > 0 ? example_browser.env_tab_widths[i]
                 : static_cast<float>(std::strlen(env_tabs[i])) * 8.0f + 16.0f;
        if (mouse.x >= tx && mouse.x <= tx + tw && mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
            example_browser.env = i;
            example_browser.scroll = 0;
            example_browser.sel = 0;
            rebuild_example_items();
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    static const char* diff_tabs[] = { "All", "Beginner", "Intermediate", "Advanced" };
    tx = cx;
    for (int i = 0; i < 4; ++i) {
        float tw = example_browser.diff_tab_widths[i] > 0 ? example_browser.diff_tab_widths[i]
                 : static_cast<float>(std::strlen(diff_tabs[i])) * 8.0f + 16.0f;
        if (mouse.x >= tx && mouse.x <= tx + tw && mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
            example_browser.difficulty = i;
            example_browser.scroll = 0;
            example_browser.sel = 0;
            rebuild_example_items();
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        tx += tw + 4.0f;
    }

    float right_x = cx + inner_w - 210.0f;
    float toggle_w = 88.0f;
    if (mouse.x >= right_x && mouse.x <= right_x + toggle_w &&
        mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
        example_browser.core_only = !example_browser.core_only;
        if (example_browser.core_only) example_browser.package_only = false;
        rebuild_example_items();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    if (mouse.x >= right_x + toggle_w + 6.0f && mouse.x <= right_x + 2 * toggle_w + 6.0f &&
        mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
        example_browser.package_only = !example_browser.package_only;
        if (example_browser.package_only) example_browser.core_only = false;
        rebuild_example_items();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    cy += kPkgBrowserTabH + 8;

    float sort_x = cx;
    float sort_w0 = example_browser.sort_tab_widths[0] > 0 ? example_browser.sort_tab_widths[0] : 92.0f;
    float sort_w1 = example_browser.sort_tab_widths[1] > 0 ? example_browser.sort_tab_widths[1] : 104.0f;
    if (mouse.x >= sort_x && mouse.x <= sort_x + sort_w0 &&
        mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
        example_browser.sort = 0;
        rebuild_example_items();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    if (mouse.x >= sort_x + sort_w0 + 4.0f && mouse.x <= sort_x + sort_w0 + 4.0f + sort_w1 &&
        mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH) {
        example_browser.sort = 1;
        rebuild_example_items();
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    cy += kPkgBrowserTabH + 8;

    if (!example_browser.entries.empty()) {
        float ex_list_area_h = visible_count * kPkgBrowserItemH;
        int idx = static_cast<int>(std::floor((mouse.y - cy + example_browser.scroll) / kPkgBrowserItemH));
        int ex_first = static_cast<int>(std::floor(example_browser.scroll / kPkgBrowserItemH));
        float ex_offset = example_browser.scroll - ex_first * kPkgBrowserItemH;
        float iy = cy - ex_offset + (idx - ex_first) * kPkgBrowserItemH;
        if (idx >= 0 && idx < static_cast<int>(example_browser.entries.size()) &&
            mouse.y >= cy && mouse.y < cy + ex_list_area_h) {
            example_browser.sel = idx;
            OverlayRect open_btn = compute_example_open_button_rect(layout, iy);
            if (overlay_contains(open_btn, mouse.x, mouse.y) &&
                example_browser.open_callback) {
                const auto& e = example_browser.entries[idx];
                std::string missing;
                bool ok = true;
                if (example_browser.package_checker) {
                    ok = example_browser.package_checker(e.requires_packages, missing);
                }
                if (ok) {
                    example_browser.action_error.clear();
                    example_browser.warn_id.clear();
                    example_browser.open_callback(e.path);
                    example_browser.open = false;
                } else if (example_browser.warn_id == e.id) {
                    example_browser.action_error = "Opening anyway with missing package: " + missing;
                    example_browser.open_callback(e.path);
                    example_browser.open = false;
                } else {
                    example_browser.warn_id = e.id;
                    example_browser.action_error =
                        "Missing package: " + missing + " (click Open again to continue)";
                }
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
    }

    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Create operator popup interaction
// -----------------------------------------------------------------------
void DialogManager::update_create_popup(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!create_popup.open || !mouse.left_clicked) return;

    bool show_composite = (create_popup.env_sel == 0);

    auto layout = compute_create_operator_layout(win_w, win_h, show_composite);

    // Click outside -> close
    if (!overlay_contains(layout, mouse.x, mouse.y)) {
        create_popup.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = layout.py + kCreateModalPadY;

    // Title
    cy += 24.0f;

    // Env buttons
    float btn_gap = 8.0f;
    float total_btn_w = 3 * kCreateEnvBtnW + 2 * btn_gap;
    float bx = layout.px + (layout.pw - total_btn_w) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        float btn_x = bx + i * (kCreateEnvBtnW + btn_gap);
        if (mouse.x >= btn_x && mouse.x <= btn_x + kCreateEnvBtnW &&
            mouse.y >= cy && mouse.y <= cy + kCreateEnvBtnH) {
            if (create_popup.env_sel != i) {
                create_popup.env_sel = i;
                reset_create_env_defaults();
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
    }
    cy += kCreateEnvBtnH + 10.0f;

    // Composite checkbox
    if (show_composite) {
        if (mouse.x >= cx && mouse.x <= cx + 200 &&
            mouse.y >= cy && mouse.y <= cy + 20) {
            create_popup.composite = !create_popup.composite;
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        cy += 24.0f + kCreateModalRowGap;
    }

    // Name field click
    if (mouse.x >= cx && mouse.x <= cx + inner_w &&
        mouse.y >= cy && mouse.y <= cy + 22.0f) {
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    cy += kCreateModalFieldH + kCreateModalRowGap;

    // MCP hint line (no interaction)
    cy += kCreateModalSectionGap;
    cy += 18.0f + kCreateModalRowGap;

    // Destination radio buttons
    cy += kCreateModalSectionGap;
    bool project_available = commands_.has_project_clone_destination();
    float dest_x = cx;
    const char* dest_labels[] = { "Auto", "Project", "Core" };
    for (int i = 0; i < 3; ++i) {
        float dw = 60.0f;  // approximate
        if (mouse.x >= dest_x && mouse.x <= dest_x + dw &&
            mouse.y >= cy && mouse.y <= cy + 22.0f) {
            if (i != 1 || project_available) {
                create_popup.destination = i;
            }
            mouse.left_clicked = false;
            mouse.left_released = false;
            return;
        }
        dest_x += dw + 12.0f;
    }
    cy += 22.0f + kCreateModalRowGap;

    // Error area
    cy += 18.0f + kCreateModalRowGap;

    // Button row
    float btn_y = cy;
    // Create Empty
    if (mouse.x >= cx && mouse.x <= cx + kCreateModalBtnW &&
        mouse.y >= btn_y && mouse.y <= btn_y + kCreateModalBtnH) {
        if (!create_popup.name_buf.empty() && create_popup.error.empty()) {
            submit_create_operator(true);
        }
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    // Cancel
    float cancel_x = cx + inner_w - kCreateModalBtnW;
    if (mouse.x >= cancel_x && mouse.x <= cancel_x + kCreateModalBtnW &&
        mouse.y >= btn_y && mouse.y <= btn_y + kCreateModalBtnH) {
        create_popup.open = false;
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }
    // Create
    float create_x = cancel_x - kCreateModalBtnW - 8.0f;
    if (mouse.x >= create_x && mouse.x <= create_x + kCreateModalBtnW &&
        mouse.y >= btn_y && mouse.y <= btn_y + kCreateModalBtnH) {
        if (!create_popup.name_buf.empty() && create_popup.error.empty()) {
            submit_create_operator(false);
        }
        mouse.left_clicked = false;
        mouse.left_released = false;
        return;
    }

    // Consume click inside modal
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Preset name popup — dismiss on click outside
// -----------------------------------------------------------------------
void DialogManager::update_preset_name_popup(MouseState& mouse, uint32_t win_w, uint32_t win_h) {
    if (!preset_name.open || !mouse.left_clicked) return;

    float pw = 280.0f, ph = 70.0f;
    float px = (static_cast<float>(win_w) - pw) * 0.5f;
    float py = (static_cast<float>(win_h) - ph) * 0.5f;
    if (mouse.x < px || mouse.x > px + pw ||
        mouse.y < py || mouse.y > py + ph) {
        preset_name.open = false;
    }
    mouse.left_clicked = false;
    mouse.left_released = false;
}

// -----------------------------------------------------------------------
// Core update banner button clicks
// -----------------------------------------------------------------------
void DialogManager::update_core_update_buttons(MouseState& mouse) {
    if (!mouse.left_clicked) return;

    for (const auto& btn : core_update.button_rects) {
        if (mouse.x >= btn.x && mouse.x <= btn.x + btn.w &&
            mouse.y >= btn.y && mouse.y <= btn.y + btn.h) {
            if (btn.action == 0) {
                if (core_update.on_install) core_update.on_install();
                clear_core_update_notice();
            } else if (btn.action == 1) {
                if (core_update.on_skip) core_update.on_skip();
                clear_core_update_notice();
            } else if (btn.action == 2) {
                if (core_update.on_later) core_update.on_later();
                clear_core_update_notice();
            }
            mouse.left_clicked = false;
            return;
        }
    }
}

} // namespace vivid::ui
