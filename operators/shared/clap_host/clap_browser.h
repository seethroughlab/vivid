#pragma once
// clap_browser.h — Shared CLAP plugin browser editor window draw code.
// Header-only, anonymous namespace. Used by clap_instrument and clap_effect.
// Requires clap_scanner.h to be included first.

#include "operator_api/editor_ui.h"
#include "shared/clap_host/clap_scanner.h"
#include <cctype>
#include <cstring>

namespace {

// Cross-frame state the operator must store per instance.
struct ClapBrowserState {
    char                  search_buf[128]  = {};
    vivid::ui::TextFieldState search_field = {};
    vivid::ui::ScrollState    scroll       = {};
};

struct ClapBrowserResult {
    bool        plugin_selected  = false;
    std::string selected_path;
    std::string selected_id;
    bool        open_gui_clicked = false;
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static bool clap_str_contains_icase(const std::string& haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    std::string h = haystack;
    std::string n(needle);
    for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

// Extract display stem from a plugin path (e.g. "/path/Surge XT.clap" → "Surge XT")
static std::string clap_path_stem(const std::string& path) {
    if (path.empty()) return {};
    size_t slash = path.rfind('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::string ext = ".clap";
    if (name.size() > ext.size() &&
        name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
        name.resize(name.size() - ext.size());
    return name;
}

// ---------------------------------------------------------------------------
// draw_clap_browser — draw the full browser into an editor window.
//
//   ctx:            VividEditorContext from vivid_draw_editor
//   state:          caller-owned cross-frame state (one per operator instance)
//   current_path:   operator's plugin_path param (for header + row highlight)
//   plugin_has_gui: whether current plugin supports CLAP_EXT_GUI (for button enable)
//
// Returns ClapBrowserResult; caller acts on plugin_selected and open_gui_clicked.
// ---------------------------------------------------------------------------
static ClapBrowserResult draw_clap_browser(VividEditorContext*  ctx,
                                            ClapBrowserState&   state,
                                            const std::string&  current_path,
                                            bool                plugin_has_gui) {
    using namespace vivid::ui;
    ClapBrowserResult result;
    if (!ctx) return result;

    auto& d   = ctx->draw;
    void* o   = d.opaque;
    const auto& th = ctx->theme;

    const float W = ctx->surface_width;
    const float H = ctx->surface_height;
    constexpr float kPad       = 8.0f;
    constexpr float kHeaderH   = 44.0f;
    constexpr float kSearchH   = 32.0f;
    constexpr float kColHeadH  = 22.0f;
    constexpr float kSepH      = 1.0f;
    constexpr float kRowH      = 28.0f;

    // --- Background ---
    if (d.draw_rect)
        d.draw_rect(o, 0, 0, W, H, th.bg);

    // --- Header panel ---
    {
        const float hx = kPad;
        const float hy = kPad;
        const float hw = W - 2.0f * kPad;

        // Background panel
        if (d.draw_rounded_rect)
            d.draw_rounded_rect(o, hx, hy, hw, kHeaderH, th.corner_radius, th.dark_bg);

        // Current plugin info (name + vendor)
        if (!current_path.empty()) {
            // Try to find metadata in cache
            const ClapPluginInfo* info = nullptr;
            for (const auto& p : clap_get_plugins()) {
                if (p.path == current_path) { info = &p; break; }
            }
            std::string label, sublabel;
            if (info) {
                label    = info->name;
                sublabel = info->vendor;
            } else {
                label    = clap_path_stem(current_path);
                sublabel = current_path;
            }
            if (d.draw_text) {
                d.draw_text(o, hx + 10.0f, hy + 8.0f,
                    label.c_str(), th.bright_text, 1.0f);
                const float small = 0.85f;
                d.draw_text(o, hx + 10.0f, hy + 26.0f,
                    sublabel.c_str(), th.dim_text, small);
            }
        } else {
            if (d.draw_text)
                d.draw_text(o, hx + 10.0f, hy + 14.0f,
                    "No plugin loaded", th.dim_text, 1.0f);
        }

        // [Open Plugin GUI] button — right side of header
        const float btnW = 130.0f;
        const float btnX = hx + hw - btnW - 4.0f;
        const float btnY = hy + (kHeaderH - 26.0f) * 0.5f;
        Rect btn_rect{ btnX, btnY, btnW, 26.0f };

        const bool can_open = !current_path.empty() && plugin_has_gui;
        VividColor btn_fill_off = can_open
            ? VividColor{0.22f, 0.22f, 0.26f, 1.0f}
            : VividColor{0.15f, 0.15f, 0.17f, 1.0f};
        VividColor btn_fill_on  = th.accent;

        if (can_open) {
            auto br = ui_button(*ctx, btn_rect, "Open Plugin GUI", false, btn_fill_off, btn_fill_on);
            if (br.clicked)
                result.open_gui_clicked = true;
        } else {
            // Disabled — draw but don't respond to clicks
            vivid::draw_ui::draw_button(d, o,
                btn_rect.x, btn_rect.y, btn_rect.w, btn_rect.h,
                "Open Plugin GUI", false, btn_fill_off, btn_fill_off,
                VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f});
        }
    }

    float y = kPad + kHeaderH + 6.0f;

    // --- Separator ---
    if (d.draw_rect)
        d.draw_rect(o, kPad, y, W - 2.0f * kPad, kSepH,
            VividColor{th.separator.r, th.separator.g, th.separator.b, 0.4f});
    y += kSepH + 4.0f;

    // --- Search field ---
    {
        if (d.draw_text)
            d.draw_text(o, kPad, y + 8.0f, "Search", th.dim_text, 0.9f);
        const float labelW = d.text_width
            ? d.text_width(o, "Search  ", 0.9f) : 60.0f;
        Rect field_rect{ kPad + labelW, y, W - kPad * 2.0f - labelW, kSearchH };
        auto tf = ui_text_field(*ctx, field_rect, state.search_buf,
                                sizeof(state.search_buf), &state.search_field,
                                "Filter by name or vendor…");
        if (tf.focused || state.search_field.focused)
            ctx->wants_keyboard = 1;
        if (tf.changed) state.scroll.scroll_y = 0.0f;  // reset scroll on search change
    }
    y += kSearchH + 4.0f;

    // --- Column headers ---
    if (d.draw_text) {
        const float nameW = (W - 2.0f * kPad) * 0.58f;
        d.draw_text(o, kPad + 6.0f, y + 3.0f, "PLUGIN", th.dim_text, 0.8f);
        d.draw_text(o, kPad + 6.0f + nameW, y + 3.0f, "VENDOR", th.dim_text, 0.8f);
    }
    y += kColHeadH;

    if (d.draw_rect)
        d.draw_rect(o, kPad, y, W - 2.0f * kPad, kSepH,
            VividColor{th.separator.r, th.separator.g, th.separator.b, 0.3f});
    y += kSepH;

    // --- Plugin list (scrollable) ---
    {
        const Rect list_bounds{ kPad, y, W - 2.0f * kPad, H - y - kPad };

        // Build filtered list
        const char* q = state.search_buf;
        std::vector<const ClapPluginInfo*> filtered;
        for (const auto& p : clap_get_plugins()) {
            if (clap_str_contains_icase(p.name, q) ||
                clap_str_contains_icase(p.vendor, q))
                filtered.push_back(&p);
        }

        const float content_h = static_cast<float>(filtered.size()) * kRowH;
        const Rect content = ui_scroll_region_begin(*ctx, list_bounds, content_h, &state.scroll);

        const float nameW  = list_bounds.w * 0.58f;
        const float vendW  = list_bounds.w * 0.42f - 6.0f;

        for (size_t i = 0; i < filtered.size(); ++i) {
            const ClapPluginInfo* info = filtered[i];
            const float ry = content.y + static_cast<float>(i) * kRowH;

            // Skip rows outside the visible clip region
            if (ry + kRowH < list_bounds.y) continue;
            if (ry > list_bounds.y + list_bounds.h) break;

            const bool selected = (info->path == current_path);
            Rect row{ content.x, ry, list_bounds.w - 6.0f, kRowH };

            // Draw background for selected/hovered (row bg)
            if (selected && d.draw_rect)
                d.draw_rect(o, row.x, row.y, row.w, row.h,
                    VividColor{th.accent.r, th.accent.g, th.accent.b, 0.22f});
            else if (row.contains(ctx->mouse.x, ctx->mouse.y) && d.draw_rect)
                d.draw_rect(o, row.x, row.y, row.w, row.h,
                    VividColor{th.accent.r, th.accent.g, th.accent.b, 0.10f});

            // Click detection
            if (row.contains(ctx->mouse.x, ctx->mouse.y) && ctx->mouse.left_clicked) {
                result.plugin_selected = true;
                result.selected_path   = info->path;
                result.selected_id     = info->plugin_id;
            }

            // Text
            if (d.draw_text && d.line_height) {
                const float ty = ry + (kRowH - d.line_height(o)) * 0.5f;
                // Plugin name (left column, clip to nameW)
                if (d.push_clip_rect) d.push_clip_rect(o, content.x, ry, nameW - 6.0f, kRowH);
                d.draw_text(o, content.x + 6.0f, ty, info->name.c_str(),
                    selected ? th.bright_text : th.dim_text, 1.0f);
                if (d.pop_clip_rect) d.pop_clip_rect(o);
                // Vendor name (right column)
                if (d.push_clip_rect) d.push_clip_rect(o, content.x + nameW, ry, vendW, kRowH);
                d.draw_text(o, content.x + nameW + 4.0f, ty, info->vendor.c_str(),
                    th.dim_text, 0.9f);
                if (d.pop_clip_rect) d.pop_clip_rect(o);
            }
        }

        // Empty state message
        if (filtered.empty() && d.draw_text) {
            const char* msg = clap_get_plugins().empty()
                ? "No plugins found. Check /Library/Audio/Plug-Ins/CLAP"
                : "No matches";
            d.draw_text(o, list_bounds.x + 12.0f,
                list_bounds.y + 14.0f, msg, th.dim_text, 0.9f);
        }

        ui_scroll_region_end(*ctx, list_bounds, content_h, &state.scroll);
    }

    return result;
}

} // namespace
