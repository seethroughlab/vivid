#include "ui/ui_style.h"

namespace vivid::ui {

std::vector<UIStyle> builtin_styles() {
    std::vector<UIStyle> styles;

    // --- Dark Steel: current look (sharp corners) ---
    {
        UIStyle s;
        s.name = "Dark Steel";
        s.id = "dark_steel";
        s.corner_radius = 0.0f;

        s.node_bg       = { 0.12f, 0.13f, 0.15f };
        s.node_sel_bg   = { 0.18f, 0.22f, 0.30f };
        s.accent        = { 0.35f, 0.55f, 0.85f };
        s.slider_fill   = { 0.25f, 0.42f, 0.68f };
        s.inspector_bg  = { 0.10f, 0.11f, 0.13f };
        s.dim_text      = { 0.55f, 0.58f, 0.62f };
        s.bright_text   = { 0.90f, 0.92f, 0.95f };

        s.popup_bg      = { 0.14f, 0.15f, 0.18f, 0.97f };
        s.input_field_bg = { 0.08f, 0.09f, 0.11f };
        s.separator     = { 0.22f, 0.24f, 0.28f };
        s.scrollbar_track = { 0.12f, 0.13f, 0.15f };
        s.scrollbar_thumb = { 0.30f, 0.32f, 0.36f };
        s.button_bg     = { 0.22f, 0.24f, 0.28f };
        s.button_hover  = { 0.28f, 0.30f, 0.35f };
        s.scrim         = { 0.0f, 0.0f, 0.0f, 0.55f };

        s.wire_color    = { 0.5f, 0.6f, 0.65f, 0.7f };
        s.wire_sel_color = { 0.6f, 0.75f, 0.85f, 0.9f };

        s.slider_track  = { 0.18f, 0.19f, 0.22f };
        s.dark_bg       = { 0.07f, 0.08f, 0.09f };

        styles.push_back(std::move(s));
    }

    // --- Midnight: deeper blues/purples, slightly rounded ---
    {
        UIStyle s;
        s.name = "Midnight";
        s.id = "midnight";
        s.corner_radius = 4.0f;

        s.node_bg       = { 0.09f, 0.10f, 0.16f };
        s.node_sel_bg   = { 0.14f, 0.18f, 0.32f };
        s.accent        = { 0.40f, 0.50f, 0.90f };
        s.slider_fill   = { 0.30f, 0.38f, 0.75f };
        s.inspector_bg  = { 0.07f, 0.08f, 0.14f };
        s.dim_text      = { 0.50f, 0.53f, 0.65f };
        s.bright_text   = { 0.85f, 0.88f, 0.95f };

        s.popup_bg      = { 0.10f, 0.11f, 0.19f, 0.97f };
        s.input_field_bg = { 0.06f, 0.07f, 0.12f };
        s.separator     = { 0.18f, 0.20f, 0.30f };
        s.scrollbar_track = { 0.09f, 0.10f, 0.16f };
        s.scrollbar_thumb = { 0.25f, 0.28f, 0.42f };
        s.button_bg     = { 0.18f, 0.20f, 0.30f };
        s.button_hover  = { 0.24f, 0.26f, 0.38f };
        s.scrim         = { 0.0f, 0.0f, 0.02f, 0.60f };

        s.wire_color    = { 0.45f, 0.55f, 0.72f, 0.7f };
        s.wire_sel_color = { 0.55f, 0.68f, 0.92f, 0.9f };

        s.slider_track  = { 0.14f, 0.15f, 0.24f };
        s.dark_bg       = { 0.05f, 0.06f, 0.10f };

        styles.push_back(std::move(s));
    }

    // --- Slate: warmer grays, more rounded ---
    {
        UIStyle s;
        s.name = "Slate";
        s.id = "slate";
        s.corner_radius = 6.0f;

        s.node_bg       = { 0.15f, 0.14f, 0.13f };
        s.node_sel_bg   = { 0.24f, 0.22f, 0.20f };
        s.accent        = { 0.72f, 0.50f, 0.30f };
        s.slider_fill   = { 0.60f, 0.42f, 0.25f };
        s.inspector_bg  = { 0.12f, 0.11f, 0.10f };
        s.dim_text      = { 0.58f, 0.55f, 0.52f };
        s.bright_text   = { 0.92f, 0.90f, 0.87f };

        s.popup_bg      = { 0.17f, 0.16f, 0.15f, 0.97f };
        s.input_field_bg = { 0.10f, 0.09f, 0.08f };
        s.separator     = { 0.26f, 0.24f, 0.22f };
        s.scrollbar_track = { 0.15f, 0.14f, 0.13f };
        s.scrollbar_thumb = { 0.34f, 0.32f, 0.30f };
        s.button_bg     = { 0.26f, 0.24f, 0.22f };
        s.button_hover  = { 0.32f, 0.30f, 0.28f };
        s.scrim         = { 0.02f, 0.01f, 0.0f, 0.55f };

        s.wire_color    = { 0.58f, 0.55f, 0.50f, 0.7f };
        s.wire_sel_color = { 0.75f, 0.68f, 0.58f, 0.9f };

        s.slider_track  = { 0.20f, 0.19f, 0.17f };
        s.dark_bg       = { 0.08f, 0.07f, 0.06f };

        styles.push_back(std::move(s));
    }

    return styles;
}

const UIStyle* find_style(const std::vector<UIStyle>& styles, const std::string& id) {
    for (const auto& s : styles) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

} // namespace vivid::ui
