#include <nlohmann/json.hpp>
#include "ui/dialog_manager.h"
#include "ui/node_graph.h"
#include "ui/renderer_2d.h"
#include "ui/overlay_layouts.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/i18n.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace vivid::ui {

static constexpr uint64_t kMcpStaleMs = 30000;

void DialogManager::draw(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                         float popup_opacity, uint32_t win_w, uint32_t win_h,
                         const TextEditState& text_edit, bool cursor_blink) {
    draw_save_confirm(tr, mouse, style, popup_opacity, win_w, win_h);
    draw_clone_confirm(tr, mouse, style, popup_opacity, win_w, win_h);
    draw_mcp_setup(tr, mouse, style, popup_opacity, win_w, win_h);
    draw_graph_meta_editor(tr, mouse, style, popup_opacity, win_w, win_h, text_edit, cursor_blink);
    draw_preferences(tr, mouse, style, popup_opacity, win_w, win_h, text_edit, cursor_blink);
    draw_package_browser(tr, mouse, style, popup_opacity, win_w, win_h);
    draw_example_browser(tr, mouse, style, popup_opacity, win_w, win_h);
    draw_create_popup(tr, mouse, style, popup_opacity, win_w, win_h, text_edit, cursor_blink);
    draw_preset_name_popup(tr, mouse, style, popup_opacity, win_w, win_h, text_edit, cursor_blink);
    draw_about(tr, mouse, style, popup_opacity, win_w, win_h);
}

void DialogManager::draw_about(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                               float popup_opacity, uint32_t win_w, uint32_t win_h) {
    if (!about.open) return;

    OverlayPanelLayout layout = compute_about_layout(win_w, win_h);
    float wf = layout.wf, hf = layout.hf;
    float pw = layout.pw, ph = layout.ph;
    float px = layout.px, py = layout.py;

    tr.draw_rect(0, 0, wf, hf, style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);
    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style.accent[0], style.accent[1], style.accent[2]);

    // Fixed header
    float hx = px + 20.0f;
    float hy = py + 17.0f;
    tr.draw_text(hx, hy, "Vivid",
                 style.bright_text[0], style.bright_text[1], style.bright_text[2], 1.0f, 1.4f);
    hy += 22.0f;
    tr.draw_text(hx, hy, "Version " VIVID_CORE_VERSION,
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);
    hy += 16.0f;
    tr.draw_text(hx, hy, "\xC2\xA9 2024-present See-Through Lab LLC",
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);
    hy += 19.0f;
    tr.draw_rect(px + 8.0f, hy, pw - 16.0f, 1.0f,
                 style.separator[0], style.separator[1], style.separator[2], 0.5f);

    float list_top = layout.list_top;
    float list_h = layout.list_h;

    struct LibNotice { const char* name; const char* text; };
    static const LibNotice kNotices[] = {
        { "GLFW",
          "Copyright (c) 2002-2006 Marcus Geelnard\n"
          "Copyright (c) 2006-2019 Camilla Loewy\n"
          "License: zlib/libpng\n"
          "This software is provided 'as-is', without any express or implied warranty.\n"
          "Permission is granted to use, alter and redistribute it freely, subject to:\n"
          "1. The origin must not be misrepresented.\n"
          "2. Altered versions must be plainly marked as such.\n"
          "3. This notice may not be removed or altered from any source distribution."
        },
        { "glfw3webgpu",
          "Copyright (c) 2022-2024 Elie Michel and the wgpu-native authors\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "oscpack",
          "Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "nlohmann/json",
          "Copyright (c) 2013-2022 Niels Lohmann <https://nlohmann.me>\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "miniaudio",
          "Copyright 2025 David Reid\n"
          "License: Choice of Public Domain (Unlicense) or MIT-0\n"
          "This software is dedicated to the public domain. Alternatively available\n"
          "under MIT-0 (MIT with no attribution requirement).\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "RtMidi",
          "Copyright (c) 2003-2023 Gary P. Scavone\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "stb_truetype, stb_image, stb_image_write",
          "Copyright (c) 2017 Sean Barrett\n"
          "License: MIT or Public Domain\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "Alternatively released into the public domain (www.unlicense.org).\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "Syphon",
          "Copyright 2010 bangnoise (Tom Butterworth) & vade (Anton Marini). All rights reserved.\n"
          "License: BSD 2-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice\n"
          "and disclaimer; (2) binary distributions reproduce the copyright notice in docs.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED."
        },
        { "IXWebSocket",
          "Copyright (c) 2018 Machine Zone, Inc. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) neither the name of the\n"
          "copyright holder nor contributor names may be used to endorse derived products.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "CLI11",
          "CLI11 2.6 Copyright (c) 2017-2025 University of Cincinnati,\n"
          "developed by Henry Schreiner under NSF AWARD 1414736. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) the name of the copyright\n"
          "holder and contributors may not be used without specific prior written permission.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "WebGPU-distribution",
          "Copyright (c) 2022-2024 Elie Michel\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "wgpu-native",
          "Copyright (c) 2021 The gfx-rs developers\n"
          "License: MIT or Apache 2.0\n"
          "MIT: Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "Apache 2.0: See THIRD_PARTY_NOTICES.txt for full Apache License 2.0 text.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "Sparkle",
          "Copyright (c) 2006-2016 Andy Matuschak and Sparkle Project contributors\n"
          "License: MIT  (loaded at runtime on macOS via Sparkle.framework)\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "NanoSVG",
          "Copyright (c) 2013-14 Mikko Mononen\n"
          "License: zlib/libpng\n"
          "This software is provided 'as-is', without any express or implied warranty.\n"
          "Permission is granted to use, alter and redistribute it freely, subject to:\n"
          "1. The origin must not be misrepresented.\n"
          "2. Altered versions must be plainly marked as such.\n"
          "3. This notice may not be removed or altered from any source distribution."
        },
        { "Snappy",
          "Copyright 2011, Google Inc. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) neither the name of\n"
          "Google Inc. nor contributor names may be used to endorse derived products.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
    };

    constexpr int kNoticeCount = static_cast<int>(sizeof(kNotices) / sizeof(kNotices[0]));
    constexpr float kLineHHeader = 16.0f;
    constexpr float kLineHText   = 13.0f;
    constexpr float kSectionGap  = 10.0f;

    // Compute total scrollable content height
    float total_h = 0.0f;
    for (int i = 0; i < kNoticeCount; ++i) {
        total_h += kLineHHeader + 4.0f;
        int line_count = 1;
        for (const char* p = kNotices[i].text; *p; ++p)
            if (*p == '\n') ++line_count;
        total_h += line_count * kLineHText + 4.0f;
        total_h += kSectionGap;
    }
    about.max_scroll = std::max(0.0f, total_h - list_h);
    about.scroll     = std::max(0.0f, std::min(about.scroll, about.max_scroll));

    // Scrollable notices area
    tr.push_clip_rect(px, list_top, pw, list_h);
    float cy = list_top - about.scroll;
    float text_x = px + 20.0f;

    for (int i = 0; i < kNoticeCount; ++i) {
        if (cy + kLineHHeader > list_top && cy < list_top + list_h)
            tr.draw_text(text_x, cy, kNotices[i].name,
                         style.accent[0], style.accent[1], style.accent[2]);
        cy += kLineHHeader + 4.0f;

        const char* p = kNotices[i].text;
        const char* line_start = p;
        auto emit_line = [&]() {
            if (cy + kLineHText > list_top && cy < list_top + list_h) {
                std::string line(line_start, p - line_start);
                tr.draw_text(text_x + 4.0f, cy, line.c_str(),
                             style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.85f, 0.75f);
            }
            cy += kLineHText;
        };
        while (*p) {
            if (*p == '\n') { emit_line(); line_start = p + 1; }
            ++p;
        }
        if (p > line_start) emit_line();
        cy += kSectionGap;
    }
    tr.pop_clip_rect();

    // Close button
    float btn_w = 80.0f, btn_h = 24.0f;
    float btn_x = px + (pw - btn_w) * 0.5f;
    float btn_y = layout.status_y;
    bool btn_hovered = mouse.x >= btn_x && mouse.x <= btn_x + btn_w &&
                       mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    tr.draw_rect(btn_x, btn_y, btn_w, btn_h,
                 btn_hovered ? style.button_hover[0] : style.button_bg[0],
                 btn_hovered ? style.button_hover[1] : style.button_bg[1],
                 btn_hovered ? style.button_hover[2] : style.button_bg[2], 0.85f);
    tr.draw_text(btn_x + 22.0f, btn_y + 4.0f, T("close", "Close"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    // Scrollbar indicator
    if (about.max_scroll > 0.0f) {
        float sb_x = px + pw - 7.0f;
        float thumb_h = list_h * list_h / (list_h + about.max_scroll);
        float thumb_y = list_top + (about.scroll / about.max_scroll) * (list_h - thumb_h);
        tr.draw_rect(sb_x, list_top, 3.0f, list_h,
                     style.separator[0], style.separator[1], style.separator[2], 0.25f);
        tr.draw_rect(sb_x, thumb_y, 3.0f, thumb_h,
                     style.accent[0], style.accent[1], style.accent[2], 0.6f);
    }
}

// -----------------------------------------------------------------------
// Save confirmation dialog (unsaved changes before New / New Project)
// -----------------------------------------------------------------------
void DialogManager::draw_save_confirm(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                      float popup_opacity, uint32_t win_w, uint32_t win_h) {
    if (!save_confirm.open) return;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    // Scrim over entire window
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    // Dialog panel (centered)
    float dw = 360.0f, dh = 90.0f;
    float dx = (wf - dw) * 0.5f;
    float dy = (hf - dh) * 0.5f;

    // Shadow + Background
    draw_shadow(tr, dx, dy, dw, dh, style.corner_radius);
    tr.draw_rounded_rect(dx, dy, dw, dh, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(dx, dy, dw, 2, style.accent[0], style.accent[1], style.accent[2]);

    // Label text
    tr.draw_text(dx + 12, dy + 12, T("save_changes_prompt", "Save changes before closing?"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    // Three buttons: Cancel | Don't Save | Save
    float btn_w = 80.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float total_btn_w = btn_w * 3 + 12.0f * 2;
    float btn_start_x = dx + (dw - total_btn_w) * 0.5f;
    float cancel_x = btn_start_x;
    float dont_save_x = btn_start_x + btn_w + 12.0f;
    float save_x = btn_start_x + (btn_w + 12.0f) * 2;

    // Cancel button
    bool cancel_hover = mouse.x >= cancel_x && mouse.x <= cancel_x + btn_w &&
                        mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    tr.draw_rect(cancel_x, btn_y, btn_w, btn_h,
                 cancel_hover ? style.button_hover[0] : style.button_bg[0],
                 cancel_hover ? style.button_hover[1] : style.button_bg[1],
                 cancel_hover ? style.button_hover[2] : style.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 16, btn_y + 3, T("cancel", "Cancel"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);

    // Don't Save button
    bool dont_hover = mouse.x >= dont_save_x && mouse.x <= dont_save_x + btn_w &&
                      mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    tr.draw_rect(dont_save_x, btn_y, btn_w, btn_h,
                 dont_hover ? style.button_hover[0] : style.button_bg[0],
                 dont_hover ? style.button_hover[1] : style.button_bg[1],
                 dont_hover ? style.button_hover[2] : style.button_bg[2], 0.9f);
    tr.draw_text(dont_save_x + 4, btn_y + 3, T("dont_save", "Don't Save"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);

    // Save button (accent)
    bool save_hover = mouse.x >= save_x && mouse.x <= save_x + btn_w &&
                      mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    if (save_hover)
        tr.draw_rect(save_x, btn_y, btn_w, btn_h,
                     style.accent[0], style.accent[1], style.accent[2], 1.0f);
    else
        tr.draw_rect(save_x, btn_y, btn_w, btn_h,
                     style.accent[0], style.accent[1], style.accent[2], 0.85f);
    tr.draw_text(save_x + 24, btn_y + 3, T("save", "Save"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
}

// -----------------------------------------------------------------------
// Clone confirmation dialog
// -----------------------------------------------------------------------
void DialogManager::draw_clone_confirm(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                       float popup_opacity, uint32_t win_w, uint32_t win_h) {
    if (!clone_confirm.open) return;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    // Scrim over entire window
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    // Dialog panel (centered)
    float dw = 360.0f, dh = 108.0f;
    float dx = (wf - dw) * 0.5f;
    float dy = (hf - dh) * 0.5f;

    // Shadow + Background
    draw_shadow(tr, dx, dy, dw, dh, style.corner_radius);
    tr.draw_rounded_rect(dx, dy, dw, dh, style.corner_radius, style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(dx, dy, dw, 2, style.accent[0], style.accent[1], style.accent[2]);

    // Label text
    std::string label = "Clone " + clone_confirm.type + " for editing";
    tr.draw_text(dx + 12, dy + 10, label.c_str(), style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    float toggle_x = dx + 12.0f;
    float toggle_y = dy + 38.0f;
    float toggle_w = dw - 24.0f;
    float toggle_h = 24.0f;
    float left_w = toggle_w * 0.5f;

    float border = 0.6f;
    tr.draw_rect(toggle_x, toggle_y, toggle_w, toggle_h,
                 style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.8f);
    tr.draw_rect(toggle_x, toggle_y, toggle_w, 1.0f,
                 style.separator[0], style.separator[1], style.separator[2], border);
    tr.draw_rect(toggle_x, toggle_y + toggle_h - 1.0f, toggle_w, 1.0f,
                 style.separator[0], style.separator[1], style.separator[2], border);
    tr.draw_rect(toggle_x, toggle_y, 1.0f, toggle_h,
                 style.separator[0], style.separator[1], style.separator[2], border);
    tr.draw_rect(toggle_x + toggle_w - 1.0f, toggle_y, 1.0f, toggle_h,
                 style.separator[0], style.separator[1], style.separator[2], border);
    tr.draw_rect(toggle_x + left_w - 0.5f, toggle_y, 1.0f, toggle_h,
                 style.separator[0], style.separator[1], style.separator[2], border);

    if (clone_confirm.project_available && clone_confirm.destination == 0) {
        tr.draw_rect(toggle_x + 1.0f, toggle_y + 1.0f, left_w - 2.0f, toggle_h - 2.0f,
                     style.accent[0], style.accent[1], style.accent[2], 0.85f);
    }
    if (clone_confirm.destination == 1) {
        tr.draw_rect(toggle_x + left_w + 1.0f, toggle_y + 1.0f, left_w - 2.0f, toggle_h - 2.0f,
                     style.accent[0], style.accent[1], style.accent[2], 0.85f);
    }

    tr.push_clip_rect(toggle_x, toggle_y, left_w, toggle_h);
    if (clone_confirm.project_available) {
        tr.draw_text(toggle_x + 12.0f, toggle_y + 4.0f, T("project_package", "Project Package"),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    } else {
        tr.draw_text(toggle_x + 12.0f, toggle_y + 4.0f, T("project_package_unavailable", "Project Package (unavailable)"),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2]);
    }
    tr.pop_clip_rect();
    tr.push_clip_rect(toggle_x + left_w, toggle_y, left_w, toggle_h);
    tr.draw_text(toggle_x + left_w + 12.0f, toggle_y + 4.0f, T("core", "Core"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    tr.pop_clip_rect();

    // Buttons
    float btn_w = 70.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float clone_x = dx + dw * 0.5f - btn_w - 6.0f;
    float cancel_x = dx + dw * 0.5f + 6.0f;

    // Clone button
    bool clone_hover = mouse.x >= clone_x && mouse.x <= clone_x + btn_w &&
                       mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    if (clone_hover)
        tr.draw_rect(clone_x, btn_y, btn_w, btn_h, style.accent[0], style.accent[1], style.accent[2], 0.9f);
    else
        tr.draw_rect(clone_x, btn_y, btn_w, btn_h, style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    tr.draw_text(clone_x + 16, btn_y + 3, T("clone", "Clone"), style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    // Cancel button
    bool cancel_hover = mouse.x >= cancel_x && mouse.x <= cancel_x + btn_w &&
                        mouse.y >= btn_y && mouse.y <= btn_y + btn_h;
    if (cancel_hover)
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style.button_hover[0], style.button_hover[1], style.button_hover[2], 0.9f);
    else
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 13, btn_y + 3, T("cancel", "Cancel"), style.dim_text[0], style.dim_text[1], style.dim_text[2]);
}

// -----------------------------------------------------------------------
// Graph meta editor dialog
// -----------------------------------------------------------------------
void DialogManager::draw_graph_meta_editor(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                           float popup_opacity, uint32_t win_w, uint32_t win_h,
                                           const TextEditState& text_edit, bool cursor_blink) {
    if (!graph_meta.open) return;

    OverlayPanelLayout layout = compute_graph_meta_editor_layout(win_w, win_h);
    float wf = layout.wf;
    float hf = layout.hf;
    float pw = layout.pw;
    float ph = layout.ph;
    float px = layout.px;
    float py = layout.py;

    tr.draw_rect(0, 0, wf, hf, style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);
    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style.accent[0], style.accent[1], style.accent[2]);
    tr.draw_text(px + 16.0f, py + 16.0f, T("edit_meta", "Edit Meta"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    if (!graph_meta.data.path.empty()) {
        std::string p = graph_meta.data.path;
        if (p.size() > 70) p = "..." + p.substr(p.size() - 67);
        tr.draw_text(px + 16.0f, py + 32.0f, p.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    }

    static const char* labels[] = {
        "id", "title", "description", "tags (csv)", "difficulty",
        "envs (csv)", "requires_packages (csv)", "featured_rank"
    };
    const std::string values[] = {
        graph_meta.data.id,
        graph_meta.data.title,
        graph_meta.data.description,
        graph_meta.data.tags_csv,
        graph_meta.data.difficulty,
        graph_meta.data.envs_csv,
        graph_meta.data.requires_packages_csv,
        graph_meta.data.featured_rank
    };

    float cx = px + 16.0f;
    float cy = py + 52.0f;
    float label_w = 160.0f;
    float field_h = 24.0f;
    float field_w = pw - 32.0f - label_w;
    float row_gap = 8.0f;
    for (int i = 0; i < 8; ++i) {
        float fy = cy + i * (field_h + row_gap);
        float fx = cx + label_w;
        tr.draw_text(cx, fy + 4, labels[i],
                     style.dim_text[0], style.dim_text[1], style.dim_text[2]);
        bool active = (i == graph_meta.active_field);
        tr.draw_rect(fx, fy, field_w, field_h,
                     active ? style.node_sel_bg[0] : style.input_field_bg[0],
                     active ? style.node_sel_bg[1] : style.input_field_bg[1],
                     active ? style.node_sel_bg[2] : style.input_field_bg[2],
                     active ? 0.95f : 0.85f);
        std::string txt = values[i];
        if (txt.size() > 94) txt = txt.substr(0, 91) + "...";
        tr.draw_text(fx + 6.0f, fy + 4.0f, txt.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        if (active && cursor_blink) {
            int cpos = std::max(0, std::min(text_edit.cursor, static_cast<int>(values[i].size())));
            float cur_x = fx + 6.0f + tr.text_width(values[i].substr(0, cpos).c_str());
            tr.draw_rect(cur_x, fy + 1.0f, 1.0f, field_h - 2.0f,
                         style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        }
    }

    if (!graph_meta.error.empty()) {
        tr.draw_text(px + 16.0f, py + ph - 66.0f, graph_meta.error.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.95f);
    }

    float by = py + ph - 42.0f;
    float save_w = 80.0f;
    float cancel_w = 90.0f;
    float save_x = px + pw - 16.0f - save_w - 8.0f - cancel_w;
    float cancel_x = save_x + save_w + 8.0f;
    tr.draw_rect(save_x, by, save_w, 24.0f, style.accent[0], style.accent[1], style.accent[2], 0.9f);
    tr.draw_rect(cancel_x, by, cancel_w, 24.0f,
                 style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.85f);
    tr.draw_text(save_x + 20.0f, by + 4.0f, T("save", "Save"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    tr.draw_text(cancel_x + 18.0f, by + 4.0f, T("cancel", "Cancel"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
}

// -----------------------------------------------------------------------
// MCP project config detection
// -----------------------------------------------------------------------
void DialogManager::scan_mcp_project_config() {
    const std::string& gpath = mcp_setup.graph_path;
    if (mcp_setup.project_config.scanned && mcp_setup.project_config.scanned_for_path == gpath)
        return;

    mcp_setup.project_config.scanned = true;
    mcp_setup.project_config.scanned_for_path = gpath;
    mcp_setup.project_config.vivid_configured = false;
    mcp_setup.project_config.opdev_configured = false;
    mcp_setup.project_config.mcp_json_dir.clear();

    if (gpath.empty()) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::path(gpath).parent_path();

    for (int i = 0; i < 5; ++i) {
        fs::path candidate = dir / ".mcp.json";
        if (fs::exists(candidate, ec)) {
            try {
                std::ifstream ifs(candidate);
                auto j = nlohmann::json::parse(ifs);
                if (j.is_object() && j.contains("mcpServers")) {
                    auto& servers = j["mcpServers"];
                    if (servers.contains("vivid"))
                        mcp_setup.project_config.vivid_configured = true;
                    if (servers.contains("opdev"))
                        mcp_setup.project_config.opdev_configured = true;
                }
                mcp_setup.project_config.mcp_json_dir = dir.string();
            } catch (...) {}
            break;  // stop at first .mcp.json found
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;  // reached root
        dir = parent;
    }
}

// -----------------------------------------------------------------------
// MCP setup dialog
// -----------------------------------------------------------------------
void DialogManager::draw_mcp_setup(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                   float popup_opacity, uint32_t win_w, uint32_t win_h) {
    if (!mcp_setup.open) return;

    scan_mcp_project_config();

    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    constexpr float kDlgW      = 480.0f;
    constexpr float kDlgPadX   = 20.0f;
    constexpr float kDlgPadY   = 16.0f;
    constexpr float kRowH      = 20.0f;
    constexpr float kSep       = 10.0f;
    constexpr float kBtnH      = 24.0f;
    constexpr float kBtnW      = 72.0f;
    constexpr float kCopyBtnW  = 48.0f;
    constexpr float kCodePad   = 6.0f;

    float inner_w = kDlgW - 2 * kDlgPadX;
    float code_text_w = inner_w - kCopyBtnW - 6.0f - 2 * kCodePad;

    // Build JSON snippets
    std::string mcp_py_path = mcp_setup.mcp_dir.empty() ? "<path_to_vivid>/mcp/vivid_mcp.py"
                                                         : mcp_setup.mcp_dir + "/vivid_mcp.py";
    std::string opdev_py_path = mcp_setup.mcp_dir.empty() ? "<path_to_vivid>/mcp/vivid_opdev_mcp.py"
                                                           : mcp_setup.mcp_dir + "/vivid_opdev_mcp.py";

    const std::string vivid_json =
        "{\"vivid\":{\"command\":\"python\",\"args\":[\"" + mcp_py_path + "\"],\"type\":\"stdio\"}}";
    const std::string opdev_json =
        "{\"opdev\":{\"command\":\"python\",\"args\":[\"" + opdev_py_path + "\"],\"type\":\"stdio\"}}";

    struct ServerDef {
        const char*  label;
        const char*  desc;
        const std::string* json_snippet;
        uint64_t     last_ping_ms;
        int          copy_action;  // 0=copy_vivid, 1=copy_opdev
        bool         configured;
    };
    bool vivid_connected = (mcp_setup.mcp_main_last_ping_ms  > 0 && now_ms - mcp_setup.mcp_main_last_ping_ms  < kMcpStaleMs);
    bool opdev_connected = (mcp_setup.mcp_opdev_last_ping_ms > 0 && now_ms - mcp_setup.mcp_opdev_last_ping_ms < kMcpStaleMs);

    ServerDef servers[2] = {
        { "Graph Server",   "Controls the Vivid node graph via AI.",  &vivid_json, mcp_setup.mcp_main_last_ping_ms,  0, mcp_setup.project_config.vivid_configured },
        { "Operator Dev",   "Helps build custom operators.",           &opdev_json, mcp_setup.mcp_opdev_last_ping_ms, 1, mcp_setup.project_config.opdev_configured },
    };
    bool connected[2] = { vivid_connected, opdev_connected };

    // Pre-pass: compute wrapped code block heights
    float code_h[2];
    for (int i = 0; i < 2; ++i) {
        auto lines = tr.wrap_text(servers[i].json_snippet->c_str(), code_text_w);
        code_h[i] = lines.size() * tr.line_height() + 2 * kCodePad;
    }

    // Panel height: title + 2 server sections + Done button
    float section_h_0 = kRowH + kRowH + code_h[0] + kSep;
    float section_h_1 = kRowH + kRowH + code_h[1] + kSep;
    float dlg_h = kDlgPadY + kRowH + kSep            // title
                + section_h_0 + section_h_1           // two servers
                + kBtnH + kDlgPadY;                   // Done + padding
    float dlg_x = (wf - kDlgW) * 0.5f;
    float dlg_y = (hf - dlg_h) * 0.5f;

    draw_shadow(tr, dlg_x, dlg_y, kDlgW, dlg_h, style.corner_radius);
    tr.draw_rounded_rect(dlg_x, dlg_y, kDlgW, dlg_h, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(dlg_x, dlg_y, kDlgW, 2,
                 style.accent[0], style.accent[1], style.accent[2]);

    float cx = dlg_x + kDlgPadX;
    float cy = dlg_y + kDlgPadY;

    // Title + close [X]
    tr.draw_text(cx, cy, T("mcp_servers", "MCP Servers"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    const char* close_x = "\xe2\x9c\x95";
    float close_tw = tr.text_width(close_x);
    float close_bx = dlg_x + kDlgW - kDlgPadX - close_tw;
    mcp_setup.button_rects.clear();
    bool close_hov = mouse.x >= close_bx - 4 && mouse.x <= close_bx + close_tw + 4 &&
                     mouse.y >= cy - 2 && mouse.y <= cy + kRowH + 2;
    tr.draw_text(close_bx, cy,
                 close_x,
                 close_hov ? style.bright_text[0] : style.dim_text[0],
                 close_hov ? style.bright_text[1] : style.dim_text[1],
                 close_hov ? style.bright_text[2] : style.dim_text[2]);
    mcp_setup.button_rects.push_back({close_bx - 4, cy - 2, close_tw + 8, kRowH + 4, 3 /*close*/});
    cy += kRowH + kSep;

    for (int i = 0; i < 2; ++i) {
        const auto& s = servers[i];
        bool conn = connected[i];

        // Status dot + server name + status text
        float dot_diam = 7.0f;
        float dot_cx = cx + dot_diam * 0.5f;
        float dot_cy = cy + kRowH * 0.5f;
        float dot_r = conn ? 0.30f : 0.40f;
        float dot_g = conn ? 0.85f : 0.40f;
        float dot_b = conn ? 0.40f : 0.45f;
        tr.draw_rounded_rect(dot_cx - dot_diam * 0.5f, dot_cy - dot_diam * 0.5f,
                             dot_diam, dot_diam, dot_diam * 0.5f, dot_r, dot_g, dot_b, 0.9f);
        tr.draw_text(cx + dot_diam + 5.0f, cy,
                     s.label,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        const char* status_str = conn ? "connected" : "not connected";
        float status_x = dlg_x + kDlgW - kDlgPadX - tr.text_width(status_str);

        // "configured" badge (muted blue, drawn left of connection status)
        if (s.configured) {
            const char* cfg_str = "configured";
            float cfg_tw = tr.text_width(cfg_str);
            float cfg_x = status_x - cfg_tw - 10.0f;
            tr.draw_text(cfg_x, cy, cfg_str, 0.45f, 0.65f, 0.90f);
        }

        tr.draw_text(status_x, cy, status_str,
                     conn ? dot_r : style.dim_text[0],
                     conn ? dot_g : style.dim_text[1],
                     conn ? dot_b : style.dim_text[2]);
        cy += kRowH;

        // Description
        tr.draw_text(cx, cy, s.desc,
                     style.dim_text[0], style.dim_text[1], style.dim_text[2]);
        cy += kRowH;

        // Code block with dynamic height
        float code_x = cx;
        float code_w = inner_w - kCopyBtnW - 6.0f;
        tr.draw_rect(code_x, cy, code_w, code_h[i], 0.06f, 0.07f, 0.08f, 0.95f);

        const std::string& jsnip = *s.json_snippet;
        float code_tx = code_x + kCodePad;
        float code_ty = cy + kCodePad;
        tr.draw_text_wrapped(code_tx, code_ty, jsnip.c_str(), code_text_w,
                             0.75f, 0.85f, 0.95f);

        // Copy button (vertically centered in code block)
        float copy_bx = code_x + code_w + 6.0f;
        float copy_by = cy + (code_h[i] - kBtnH) * 0.5f;
        bool copy_hov = mouse.x >= copy_bx && mouse.x <= copy_bx + kCopyBtnW &&
                        mouse.y >= copy_by && mouse.y <= copy_by + kBtnH;
        tr.draw_rounded_rect(copy_bx, copy_by, kCopyBtnW, kBtnH, style.corner_radius,
                             copy_hov ? style.button_hover[0] : style.button_bg[0],
                             copy_hov ? style.button_hover[1] : style.button_bg[1],
                             copy_hov ? style.button_hover[2] : style.button_bg[2], 0.9f);
        const char* copy_lbl = "Copy";
        float copy_lbl_w = tr.text_width(copy_lbl);
        tr.draw_text(copy_bx + (kCopyBtnW - copy_lbl_w) * 0.5f,
                     copy_by + (kBtnH - tr.line_height()) * 0.5f,
                     copy_lbl,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        mcp_setup.button_rects.push_back({copy_bx, copy_by, kCopyBtnW, kBtnH, s.copy_action});
        cy += code_h[i] + kSep;
    }

    // Done button
    float done_x = dlg_x + kDlgW - kDlgPadX - kBtnW;
    bool done_hov = mouse.x >= done_x && mouse.x <= done_x + kBtnW &&
                    mouse.y >= cy     && mouse.y <= cy + kBtnH;
    tr.draw_rounded_rect(done_x, cy, kBtnW, kBtnH, style.corner_radius,
                         done_hov ? style.button_hover[0] : style.button_bg[0],
                         done_hov ? style.button_hover[1] : style.button_bg[1],
                         done_hov ? style.button_hover[2] : style.button_bg[2], 0.9f);
    float done_tw = tr.text_width(T("done", "Done"));
    tr.draw_text(done_x + (kBtnW - done_tw) * 0.5f, cy + (kBtnH - tr.line_height()) * 0.5f, T("done", "Done"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    mcp_setup.button_rects.push_back({done_x, cy, kBtnW, kBtnH, 2 /*done*/});
}

// -----------------------------------------------------------------------
// Preferences panel
// -----------------------------------------------------------------------
void DialogManager::draw_preferences(Renderer2D& tr, const MouseState& mouse, const UIStyle& style,
                                     float popup_opacity, uint32_t win_w, uint32_t win_h,
                                     const TextEditState& text_edit, bool cursor_blink) {
    if (!prefs.open) return;

    static constexpr float kW = 360.0f;
    static constexpr float kPadX = 20.0f;
    static constexpr float kPadY = 16.0f;
    static constexpr float kRowH = 22.0f;
    static constexpr float kSectionGap = 12.0f;
    static constexpr float kBtnW = 70.0f;
    static constexpr float kBtnH = 24.0f;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    // Compute panel height dynamically
    int editor_count = static_cast<int>(prefs.editor_names.size());
    int style_count = static_cast<int>(prefs.styles.size());
    bool show_custom = (prefs.editor_sel >= 0 &&
                        prefs.editor_sel < static_cast<int>(prefs.editor_ids.size()) &&
                        prefs.editor_ids[prefs.editor_sel] == "custom");

    float content_h = kPadY
        + kRowH                              // "Preferences" title
        + kSectionGap
        + kRowH                              // "TEXT EDITOR" section header
        + editor_count * kRowH               // radio items
        + (show_custom ? kRowH + 4 : 0)      // custom command field
        + kSectionGap
        + kRowH                              // "STYLE" section header
        + style_count * kRowH                // radio items
        + kRowH + 4                          // "Open Themes Folder" button
        + kSectionGap
        + kRowH                              // "MOUSE" section header
        + 3 * kRowH                          // pan gesture radio items
        + kSectionGap
        + kBtnH                              // buttons
        + kPadY;

    float pw = kW;
    float ph = content_h;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Shadow + Panel background
    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    // Accent bar
    tr.draw_rect(px, py, pw, 2, style.accent[0], style.accent[1], style.accent[2]);

    float cx = px + kPadX;
    float cy = py + kPadY;
    float inner_w = pw - 2 * kPadX;

    // Title
    tr.draw_text(cx, cy, T("preferences", "Preferences"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    cy += kRowH + kSectionGap;

    // --- TEXT EDITOR section ---
    tr.draw_text(cx, cy, T("text_editor", "TEXT EDITOR"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    cy += kRowH;

    for (int i = 0; i < editor_count; ++i) {
        bool sel = (i == prefs.editor_sel);
        // Radio button circle
        float radio_x = cx + 2;
        float radio_y = cy + kRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style.separator[0], style.separator[1], style.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style.accent[0], style.accent[1], style.accent[2]);
        }
        // Label
        tr.draw_text(cx + 18, cy + 1, prefs.editor_names[i].c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        cy += kRowH;
    }

    // Custom command text field
    if (show_custom) {
        cy += 2;
        tr.draw_rect(cx + 18, cy, inner_w - 18, kRowH - 2,
                     style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
        if (prefs.editing_custom) {
            tr.draw_rect(cx + 18, cy, inner_w - 18, 1,
                         style.accent[0], style.accent[1], style.accent[2]);
        }
        std::string display = prefs.custom_command;
        if (display.empty() && !prefs.editing_custom) display = "/usr/local/bin/code {file}";
        float text_alpha = prefs.custom_command.empty() && !prefs.editing_custom ? 0.4f : 1.0f;
        tr.draw_text(cx + 22, cy + 2, display.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2], text_alpha);
        if (prefs.editing_custom && cursor_blink) {
            int cpos = std::max(0, std::min(text_edit.cursor, static_cast<int>(prefs.custom_command.size())));
            float cur_x = cx + 22 + tr.text_width(prefs.custom_command.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, cy + 1, 1.0f, kRowH - 4,
                         style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        }
        cy += kRowH + 2;
    }

    cy += kSectionGap;

    // --- STYLE section ---
    tr.draw_text(cx, cy, T("style", "STYLE"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    cy += kRowH;

    for (int i = 0; i < style_count; ++i) {
        bool sel = (i == prefs.style_sel);
        float radio_x = cx + 2;
        float radio_y = cy + kRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style.separator[0], style.separator[1], style.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style.accent[0], style.accent[1], style.accent[2]);
        }
        tr.draw_text(cx + 18, cy + 1, prefs.styles[i].name.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        cy += kRowH;
    }

    // "Open Themes Folder" link
    cy += 4;
    {
        const char* label = "Open Themes Folder...";
        bool link_hover = mouse.x >= cx + 18 && mouse.x <= cx + 18 + tr.text_width(label) &&
                          mouse.y >= cy && mouse.y <= cy + kRowH;
        float alpha = link_hover ? 1.0f : 0.7f;
        tr.draw_text(cx + 18, cy + 1, label,
                     style.accent[0], style.accent[1], style.accent[2], alpha);
    }
    cy += kRowH;

    cy += kSectionGap;

    // --- MOUSE section ---
    tr.draw_text(cx, cy, T("mouse", "MOUSE"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    cy += kRowH;

    const char* pan_labels[] = { "Middle drag", "Left drag (empty canvas)", "Right drag" };
    for (int i = 0; i < 3; ++i) {
        bool sel = (i == prefs.pan_gesture_sel);
        float radio_x = cx + 2;
        float radio_y = cy + kRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style.separator[0], style.separator[1], style.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style.accent[0], style.accent[1], style.accent[2]);
        }
        tr.draw_text(cx + 18, cy + 1, pan_labels[i],
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        cy += kRowH;
    }

    cy += kSectionGap;

    // --- Buttons ---
    float btn_total = 2 * kBtnW + 12;
    float save_x = px + (pw - btn_total) * 0.5f;
    float cancel_x = save_x + kBtnW + 12;

    bool save_hover = mouse.x >= save_x && mouse.x <= save_x + kBtnW &&
                      mouse.y >= cy && mouse.y <= cy + kBtnH;
    bool cancel_hover = mouse.x >= cancel_x && mouse.x <= cancel_x + kBtnW &&
                        mouse.y >= cy && mouse.y <= cy + kBtnH;

    if (save_hover)
        tr.draw_rounded_rect(save_x, cy, kBtnW, kBtnH, style.corner_radius,
                             style.accent[0], style.accent[1], style.accent[2], 0.9f);
    else
        tr.draw_rounded_rect(save_x, cy, kBtnW, kBtnH, style.corner_radius,
                             style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    float save_tw = tr.text_width(T("save", "Save"));
    tr.draw_text(save_x + (kBtnW - save_tw) * 0.5f, cy + 4, T("save", "Save"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    if (cancel_hover)
        tr.draw_rounded_rect(cancel_x, cy, kBtnW, kBtnH, style.corner_radius,
                             style.button_hover[0], style.button_hover[1], style.button_hover[2], 0.9f);
    else
        tr.draw_rounded_rect(cancel_x, cy, kBtnW, kBtnH, style.corner_radius,
                             style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    float cancel_tw = tr.text_width(T("cancel", "Cancel"));
    tr.draw_text(cancel_x + (kBtnW - cancel_tw) * 0.5f, cy + 4, T("cancel", "Cancel"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);
}

// -----------------------------------------------------------------------
// Package browser
// -----------------------------------------------------------------------

namespace {
std::string fit_text_to_width(Renderer2D& tr, const std::string& text, float max_w) {
    if (max_w <= 0.0f) return {};
    if (tr.text_width(text.c_str()) <= max_w) return text;
    static const char* kEllipsis = "...";
    const float ell_w = tr.text_width(kEllipsis);
    if (ell_w >= max_w) return {};
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        std::string candidate = out + kEllipsis;
        if (tr.text_width(candidate.c_str()) <= max_w) return candidate;
    }
    return {};
}
} // namespace

void DialogManager::draw_package_browser(Renderer2D& tr, const MouseState& mouse,
                                         const UIStyle& style, float popup_opacity,
                                         uint32_t win_w, uint32_t win_h) {
    if (!pkg_browser.open) return;

    refresh_package_browser_snapshot_if_ready();

    OverlayPanelLayout layout =
        compute_package_browser_layout(win_w, win_h, pkg_browser.entries.size());
    float wf = layout.wf;
    float hf = layout.hf;

    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2,
                 style.accent[0], style.accent[1], style.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = py + kPkgBrowserPadY;

    tr.draw_text(cx, cy + 6, T("packages", "Packages"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    static const float kLinkBtnW = 96.0f;
    float link_btn_x = cx + inner_w - kLinkBtnW;
    float link_btn_y = cy + (kPkgBrowserHeaderH - kPkgBrowserBtnH) / 2.0f - 2.0f;
    bool link_btn_hovered = mouse.x >= link_btn_x && mouse.x <= link_btn_x + kLinkBtnW &&
                            mouse.y >= link_btn_y && mouse.y <= link_btn_y + kPkgBrowserBtnH;
    tr.draw_rect(link_btn_x, link_btn_y, kLinkBtnW, kPkgBrowserBtnH,
                 link_btn_hovered ? style.accent[0] : style.button_bg[0],
                 link_btn_hovered ? style.accent[1] : style.button_bg[1],
                 link_btn_hovered ? style.accent[2] : style.button_bg[2],
                 link_btn_hovered ? 0.9f : 0.8f);
    float link_lbl_x = link_btn_x + (kLinkBtnW - tr.text_width(T("link_local", "Link Local..."))) * 0.5f;
    tr.draw_text(link_lbl_x, link_btn_y + 3, T("link_local", "Link Local..."),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    cy += kPkgBrowserHeaderH;

    if (pkg_browser.search_focused) {
        tr.draw_rect(cx - 1, cy - 1, inner_w + 2, kPkgBrowserSearchH + 2,
                     style.accent[0], style.accent[1], style.accent[2]);
        tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                     style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
        std::string search_display = pkg_browser.filter;
        search_display += (static_cast<int>(frame_counter_ / 30) % 2 == 0) ? "_" : " ";
        tr.draw_text(cx + 4, cy + 5, search_display.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    } else {
        tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                     style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
        tr.draw_rect(cx, cy, inner_w, 1,
                     style.accent[0], style.accent[1], style.accent[2]);
        if (pkg_browser.filter.empty()) {
            tr.draw_text(cx + 4, cy + 5, T("search_packages", "Search packages..."),
                         style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
        } else {
            tr.draw_text(cx + 4, cy + 5, pkg_browser.filter.c_str(),
                         style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        }
    }
    cy += kPkgBrowserSearchH + 6;

    static const char* tab_labels[] = { "All", "Audio", "GPU", "Control", "Utility", "Installed" };
    static const int tab_count = 6;
    float tab_x = cx;
    float tab_gap = 4.0f;
    for (int i = 0; i < tab_count; ++i) {
        bool selected = (i == pkg_browser.category);
        float approx_tw = tr.text_width(tab_labels[i]) + 16.0f;
        bool hovered = mouse.x >= tab_x && mouse.x <= tab_x + approx_tw &&
                       mouse.y >= cy && mouse.y <= cy + kPkgBrowserTabH;
        float tw = draw_tab_button(tr, style, tab_x, cy, kPkgBrowserTabH, tab_labels[i], selected, hovered);
        pkg_browser.tab_widths[i] = tw;
        tab_x += tw + tab_gap;
    }
    cy += kPkgBrowserTabH + 8;

    float list_top = layout.list_top;
    int total = static_cast<int>(pkg_browser.entries.size());
    float list_area_h = visible_count * kPkgBrowserItemH;
    int first = std::max(0, static_cast<int>(std::floor(pkg_browser.scroll / kPkgBrowserItemH)));
    float offset = pkg_browser.scroll - first * kPkgBrowserItemH;
    int draw_count = std::min(total - first, kPkgBrowserMaxVisible + 1);

    tr.push_clip_rect(cx, list_top, inner_w, list_area_h);
    for (int vi = 0; vi < draw_count; ++vi) {
        int i = first + vi;
        const auto& entry = pkg_browser.entries[i];
        float iy = list_top - offset + vi * kPkgBrowserItemH;
        bool hovered = mouse.x >= cx && mouse.x <= cx + inner_w &&
                       mouse.y >= std::max(iy, list_top) && mouse.y <= std::min(iy + kPkgBrowserItemH, list_top + list_area_h);
        if (hovered || i == pkg_browser.sel) {
            tr.draw_rect(cx, iy, inner_w, kPkgBrowserItemH,
                         style.node_sel_bg[0], style.node_sel_bg[1], style.node_sel_bg[2],
                         hovered ? 0.5f : 0.3f);
        }
        if (vi > 0 || offset > 0.0f) {
            tr.draw_rect(cx + 4, iy, inner_w - 8, 1,
                         style.slider_track[0], style.slider_track[1], style.slider_track[2], 0.3f);
        }

        OverlayRect btn = compute_package_action_button_rect(layout, iy);
        const float text_left = cx + 8.0f;
        const float text_right = btn.x - 10.0f;
        const float text_w = std::max(0.0f, text_right - text_left);
        tr.push_clip_rect(text_left, iy + 2.0f, text_w, kPkgBrowserItemH - 4.0f);

        std::string ver_str = "v" + entry.version;
        const float ver_w = tr.text_width(ver_str.c_str());
        const char* state = entry.needs_rebuild ? T("needs_rebuild", "Try Rebuild")
                          : entry.linked        ? T("linked", "Linked")
                          :                       T("installed", "Installed");
        const float chip_w = tr.text_width(state) + 16.0f;
        float available_name_w = text_w - 8.0f - ver_w;
        if (entry.installed) available_name_w -= (10.0f + chip_w);
        std::string display_name = fit_text_to_width(tr, entry.name, available_name_w);
        tr.draw_text(text_left, iy + 6, display_name.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        float name_w = tr.text_width(display_name.c_str());
        tr.draw_text(text_left + name_w + 8, iy + 6, ver_str.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
        if (entry.installed) {
            float state_x = text_left + name_w + 8 + ver_w + 10.0f;
            bool error_chip = entry.needs_rebuild;
            tr.draw_rect(state_x, iy + 4, chip_w, 16.0f,
                         error_chip   ? kErrorAccent[0] : entry.linked ? style.accent[0] : style.button_bg[0],
                         error_chip   ? kErrorAccent[1] : entry.linked ? style.accent[1] : style.button_bg[1],
                         error_chip   ? kErrorAccent[2] : entry.linked ? style.accent[2] : style.button_bg[2],
                         error_chip   ? 0.85f : entry.linked ? 0.85f : 0.75f);
            tr.draw_text(state_x + 6.0f, iy + 6, state,
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style.bright_text[0],
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style.bright_text[1],
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style.bright_text[2],
                         0.9f);
        }

        std::string desc = fit_text_to_width(tr, entry.description, text_w);
        tr.draw_text(text_left, iy + 22, desc.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2]);

        std::string meta;
        if (!entry.category.empty()) meta = entry.category;
        if (!entry.author.empty()) {
            if (!meta.empty()) meta += " · ";
            meta += entry.author;
        }
        if (!meta.empty()) {
            meta = fit_text_to_width(tr, meta, text_w);
            tr.draw_text(text_left, iy + 37, meta.c_str(),
                         style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
        }
        tr.pop_clip_rect();

        float btn_x = btn.x;
        float btn_y = btn.y;
        const char* btn_label = entry.needs_rebuild ? T("rebuild", "Rebuild")
                              : entry.installed     ? (entry.linked ? T("unlink", "Unlink") : T("remove", "Remove"))
                              :                       T("install", "Install");
        bool this_pending = pkg_browser.action_pending && (entry.name == pkg_browser.action_name);
        bool any_pending  = pkg_browser.action_pending;
        bool btn_hover = !any_pending && overlay_contains(btn, mouse.x, mouse.y);

        if (this_pending) {
            // Draw spinner arc instead of button text
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.8f);
            float arc_cx = btn_x + kPkgBrowserBtnW * 0.5f;
            float arc_cy = btn_y + kPkgBrowserBtnH * 0.5f;
            float arc_r  = kPkgBrowserBtnH * 0.30f;
            float a0 = static_cast<float>(frame_counter_) * 0.08f;
            tr.draw_arc(arc_cx, arc_cy, arc_r, a0, a0 + 4.5f, 1.5f, 12,
                        style.accent[0], style.accent[1], style.accent[2], 0.9f);
        } else if (any_pending) {
            // Grey out — no hover, no interaction
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.35f);
            float label_x = btn_x + (btn.w - tr.text_width(btn_label)) * 0.5f;
            tr.draw_text(label_x, btn_y + 3, btn_label,
                         style.bright_text[0], style.bright_text[1], style.bright_text[2], 0.35f);
        } else {
        if (entry.needs_rebuild) {
            // Rebuild button — accent color to draw attention
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? style.accent[0] : kErrorAccent[0],
                         btn_hover ? style.accent[1] : kErrorAccent[1],
                         btn_hover ? style.accent[2] : kErrorAccent[2],
                         btn_hover ? 0.9f : 0.8f);
        } else if (entry.installed) {
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? kErrorAccent[0] : style.button_bg[0],
                         btn_hover ? kErrorAccent[1] : style.button_bg[1],
                         btn_hover ? kErrorAccent[2] : style.button_bg[2],
                         0.8f);
        } else {
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? style.accent[0] : style.button_bg[0],
                         btn_hover ? style.accent[1] : style.button_bg[1],
                         btn_hover ? style.accent[2] : style.button_bg[2],
                         btn_hover ? 0.9f : 0.8f);
        }

        float label_x = btn_x + (btn.w - tr.text_width(btn_label)) * 0.5f;
        tr.draw_text(label_x, btn_y + 3, btn_label,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        }
    }
    tr.pop_clip_rect();

    if (total > kPkgBrowserMaxVisible) {
        float sb_x = cx + inner_w - 4;
        float sb_h = visible_count * kPkgBrowserItemH;
        float thumb_h = std::max(20.0f, sb_h * kPkgBrowserMaxVisible / static_cast<float>(total));
        float max_scroll_px = std::max(1.0f, (total - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
        float thumb_y = list_top + (sb_h - thumb_h) * pkg_browser.scroll / max_scroll_px;
        tr.draw_rect(sb_x, list_top, 4, sb_h,
                     style.slider_track[0], style.slider_track[1], style.slider_track[2], 0.3f);
        tr.draw_rect(sb_x, thumb_y, 4, thumb_h,
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
    }

    std::string status;
    if (pkg_browser.callbacks.fetch_state) {
        auto fstate = pkg_browser.callbacks.fetch_state();
        if (fstate == PackageBrowserFetchState::Fetching) {
            status = T("fetching_catalog", "Fetching catalog...");
        } else if (fstate == PackageBrowserFetchState::Error) {
            if (pkg_browser.callbacks.fetch_error) status = pkg_browser.callbacks.fetch_error();
        } else {
            status = std::to_string(pkg_browser.entries.size()) + " package" +
                     (pkg_browser.entries.size() != 1 ? "s" : "");
            PackageBrowserUpdateSummary summary{};
            if (pkg_browser.callbacks.update_summary) summary = pkg_browser.callbacks.update_summary();
            if (summary.updates_available > 0) {
                status += " • " + std::to_string(summary.updates_available) + " update";
                if (summary.updates_available != 1) status += "s";
                if (summary.incompatible_updates > 0) {
                    status += " (" + std::to_string(summary.incompatible_updates) + " incompatible)";
                }
                status += " • run `vivid package-check-updates`";
            }
        }
    }
    if (!pkg_browser.action_error.empty()) {
        static constexpr float kCopyBtnW = 48.0f;
        static constexpr float kCopyBtnH = 20.0f;
        static constexpr float kCopyGap = 6.0f;
        float err_text_w = inner_w - kCopyBtnW - kCopyGap;
        tr.draw_text_wrapped(cx, layout.status_y, pkg_browser.action_error.c_str(),
                             err_text_w, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
        // "Copy" button at top-right of error area
        float copy_bx = cx + inner_w - kCopyBtnW;
        float copy_by = layout.status_y;
        bool copy_hov = mouse.x >= copy_bx && mouse.x <= copy_bx + kCopyBtnW &&
                        mouse.y >= copy_by && mouse.y <= copy_by + kCopyBtnH;
        tr.draw_rounded_rect(copy_bx, copy_by, kCopyBtnW, kCopyBtnH, style.corner_radius,
                             copy_hov ? style.button_hover[0] : style.button_bg[0],
                             copy_hov ? style.button_hover[1] : style.button_bg[1],
                             copy_hov ? style.button_hover[2] : style.button_bg[2], 0.9f);
        const char* copy_lbl = "Copy";
        float copy_lbl_w = tr.text_width(copy_lbl);
        tr.draw_text(copy_bx + (kCopyBtnW - copy_lbl_w) * 0.5f,
                     copy_by + (kCopyBtnH - tr.line_height()) * 0.5f,
                     copy_lbl,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        pkg_browser.error_copy_btn = {copy_bx, copy_by, kCopyBtnW, kCopyBtnH};
    } else if (!status.empty()) {
        tr.draw_text(cx, layout.status_y, status.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    }
}

// -----------------------------------------------------------------------
// Example browser
// -----------------------------------------------------------------------

void DialogManager::draw_example_browser(Renderer2D& tr, const MouseState& mouse,
                                         const UIStyle& style, float popup_opacity,
                                         uint32_t win_w, uint32_t win_h) {
    if (!example_browser.open) return;

    OverlayPanelLayout layout =
        compute_example_browser_layout(win_w, win_h, example_browser.entries.size());
    float wf = layout.wf;
    float hf = layout.hf;
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style.accent[0], style.accent[1], style.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = py + kPkgBrowserPadY;

    tr.draw_text(cx, cy + 6, T("open_example", "Open Example"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    cy += kPkgBrowserHeaderH;

    if (example_browser.search_focused) {
        tr.draw_rect(cx - 1, cy - 1, inner_w + 2, kPkgBrowserSearchH + 2,
                     style.accent[0], style.accent[1], style.accent[2]);
        tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                     style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
        std::string s = example_browser.filter;
        s += (static_cast<int>(frame_counter_ / 30) % 2 == 0) ? "_" : " ";
        tr.draw_text(cx + 4, cy + 5, s.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    } else {
        tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                     style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
        tr.draw_rect(cx, cy, inner_w, 1, style.accent[0], style.accent[1], style.accent[2]);
        if (example_browser.filter.empty()) {
            tr.draw_text(cx + 4, cy + 5, T("search_examples", "Search by title, tags, id, path..."),
                         style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.55f);
        } else {
            tr.draw_text(cx + 4, cy + 5, example_browser.filter.c_str(),
                         style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        }
    }
    cy += kPkgBrowserSearchH + 6;

    static const char* env_tabs[] = { "All", "GPU", "Audio", "Control", "I/O" };
    float tx = cx;
    for (int i = 0; i < 5; ++i) {
        bool sel = (i == example_browser.env);
        float tw = draw_tab_button(tr, style, tx, cy, kPkgBrowserTabH, env_tabs[i], sel, false);
        example_browser.env_tab_widths[i] = tw;
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    static const char* diff_tabs[] = { "All", "Beginner", "Intermediate", "Advanced" };
    tx = cx;
    for (int i = 0; i < 4; ++i) {
        bool sel = (i == example_browser.difficulty);
        float tw = draw_tab_button(tr, style, tx, cy, kPkgBrowserTabH, diff_tabs[i], sel, false);
        example_browser.diff_tab_widths[i] = tw;
        tx += tw + 4.0f;
    }

    float toggle_w = 88.0f;
    float toggles_x = cx + inner_w - 210.0f;
    tr.draw_rect(toggles_x, cy, toggle_w, kPkgBrowserTabH,
                 example_browser.core_only ? style.accent[0] : style.button_bg[0],
                 example_browser.core_only ? style.accent[1] : style.button_bg[1],
                 example_browser.core_only ? style.accent[2] : style.button_bg[2],
                 example_browser.core_only ? 0.9f : 0.65f);
    tr.draw_text(toggles_x + 8, cy + 3, T("core_only", "Core only"),
                 example_browser.core_only ? 0.0f : style.dim_text[0],
                 example_browser.core_only ? 0.0f : style.dim_text[1],
                 example_browser.core_only ? 0.0f : style.dim_text[2]);
    tr.draw_rect(toggles_x + toggle_w + 6.0f, cy, toggle_w, kPkgBrowserTabH,
                 example_browser.package_only ? style.accent[0] : style.button_bg[0],
                 example_browser.package_only ? style.accent[1] : style.button_bg[1],
                 example_browser.package_only ? style.accent[2] : style.button_bg[2],
                 example_browser.package_only ? 0.9f : 0.65f);
    tr.draw_text(toggles_x + toggle_w + 14.0f, cy + 3, T("package", "Package"),
                 example_browser.package_only ? 0.0f : style.dim_text[0],
                 example_browser.package_only ? 0.0f : style.dim_text[1],
                 example_browser.package_only ? 0.0f : style.dim_text[2]);
    cy += kPkgBrowserTabH + 8;

    static const char* sort_tabs[] = { "Featured", "A-Z" };
    tx = cx;
    for (int i = 0; i < 2; ++i) {
        bool sel = (i == example_browser.sort);
        float tw = draw_tab_button(tr, style, tx, cy, kPkgBrowserTabH, sort_tabs[i], sel, false);
        example_browser.sort_tab_widths[i] = tw;
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    int total = static_cast<int>(example_browser.entries.size());
    float ex_list_area_h = layout.visible_count * kPkgBrowserItemH;
    int ex_first = std::max(0, static_cast<int>(std::floor(example_browser.scroll / kPkgBrowserItemH)));
    float ex_offset = example_browser.scroll - ex_first * kPkgBrowserItemH;
    int ex_draw_count = std::min(total - ex_first, kPkgBrowserMaxVisible + 1);

    tr.push_clip_rect(cx, cy, inner_w, ex_list_area_h);
    for (int vi = 0; vi < ex_draw_count; ++vi) {
        int i = ex_first + vi;
        const auto& e = example_browser.entries[i];
        float iy = cy - ex_offset + vi * kPkgBrowserItemH;
        bool hovered = mouse.x >= cx && mouse.x <= cx + inner_w &&
                       mouse.y >= std::max(iy, cy) && mouse.y <= std::min(iy + kPkgBrowserItemH, cy + ex_list_area_h);
        if (hovered || i == example_browser.sel) {
            tr.draw_rect(cx, iy, inner_w, kPkgBrowserItemH,
                         style.node_sel_bg[0], style.node_sel_bg[1], style.node_sel_bg[2],
                         hovered ? 0.5f : 0.3f);
        }
        if (vi > 0 || ex_offset > 0.0f) {
            tr.draw_rect(cx + 4, iy, inner_w - 8, 1,
                         style.slider_track[0], style.slider_track[1], style.slider_track[2], 0.3f);
        }

        OverlayRect open_btn = compute_example_open_button_rect(layout, iy);
        float bx = open_btn.x;
        float by = open_btn.y;
        const bool has_required_packages = !e.requires_packages.empty();
        bool has_missing_packages = false;
        if (has_required_packages && example_browser.package_checker) {
            std::string missing_pkg_name;
            has_missing_packages = !example_browser.package_checker(e.requires_packages, missing_pkg_name);
        }

        const float badge_gap = 8.0f;
        const float badge_w = 128.0f;
        const float text_left = cx + 8.0f;
        float text_right = bx - 10.0f;
        if (has_missing_packages) text_right -= (badge_w + badge_gap);
        const float text_w = std::max(0.0f, text_right - text_left);

        tr.push_clip_rect(text_left, iy + 2.0f, text_w, kPkgBrowserItemH - 4.0f);
        const std::string title = fit_text_to_width(tr, e.title, text_w);
        tr.draw_text(text_left, iy + 6, title.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
        std::string emeta = e.id + " · " + e.path;
        emeta = fit_text_to_width(tr, emeta, text_w);
        tr.draw_text(text_left, iy + 22, emeta.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
        std::string summary = fit_text_to_width(tr, e.summary, text_w);
        tr.draw_text(text_left, iy + 37, summary.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
        tr.pop_clip_rect();

        tr.draw_rect(bx, by, open_btn.w, open_btn.h,
                     style.accent[0], style.accent[1], style.accent[2], 0.85f);
        float tw = tr.text_width(T("open", "Open"));
        tr.draw_text(bx + (open_btn.w - tw) * 0.5f, by + 3, T("open", "Open"),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);

        if (has_missing_packages) {
            const float badge_x = bx - badge_gap - badge_w;
            tr.draw_rect(badge_x, by, badge_w, open_btn.h,
                         0.42f, 0.30f, 0.12f, 0.9f);
            tr.draw_text(badge_x + 8.0f, by + 3.0f, T("needs_packages", "needs package(s)"),
                         0.95f, 0.78f, 0.32f, 0.95f);
        }
    }
    tr.pop_clip_rect();

    if (total > kPkgBrowserMaxVisible) {
        float sb_x = cx + inner_w - 4;
        float sb_h = layout.visible_count * kPkgBrowserItemH;
        float thumb_h = std::max(20.0f, sb_h * kPkgBrowserMaxVisible / static_cast<float>(total));
        float max_scroll_px = std::max(1.0f, (total - kPkgBrowserMaxVisible) * kPkgBrowserItemH);
        float thumb_y = cy + (sb_h - thumb_h) * example_browser.scroll / max_scroll_px;
        tr.draw_rect(sb_x, cy, 4, sb_h,
                     style.slider_track[0], style.slider_track[1], style.slider_track[2], 0.3f);
        tr.draw_rect(sb_x, thumb_y, 4, thumb_h,
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
    }

    if (!example_browser.action_error.empty()) {
        tr.draw_text(cx, layout.status_y, example_browser.action_error.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    } else {
        std::string status = std::to_string(example_browser.entries.size()) + " graph";
        if (example_browser.entries.size() != 1) status += "s";
        status += " · Enter opens selection";
        tr.draw_text(cx, layout.status_y, status.c_str(),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.7f);
    }
}

// -----------------------------------------------------------------------
// Create operator modal
// -----------------------------------------------------------------------
void DialogManager::draw_create_popup(Renderer2D& tr, const MouseState& mouse,
                                      const UIStyle& style, float popup_opacity,
                                      uint32_t win_w, uint32_t win_h,
                                      const TextEditState& text_edit, bool cursor_blink) {
    if (!create_popup.open) return;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);
    bool blink_on = (static_cast<int>(frame_counter_ / 30) % 2 == 0);
    bool show_composite = (create_popup.env_sel == 0);

    auto layout = compute_create_operator_layout(win_w, win_h, show_composite);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    // Shadow + Panel
    draw_shadow(tr, layout.px, layout.py, layout.pw, layout.ph, style.corner_radius);
    tr.draw_rounded_rect(layout.px, layout.py, layout.pw, layout.ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(layout.px, layout.py, layout.pw, 2,
                 style.accent[0], style.accent[1], style.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = layout.py + kCreateModalPadY;

    // 1. Title
    tr.draw_text(cx, cy, T("new_operator", "New Starter Operator"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    cy += 24.0f;

    // 2. Env selector buttons
    const char* env_labels[] = { "control", "audio", "gpu" };
    const std::array<float, 3>* env_colors[] = { &kControlAccent, &kAudioAccent, &kGpuAccent };
    float btn_gap = 8.0f;
    float total_btn_w = 3 * kCreateEnvBtnW + 2 * btn_gap;
    float bx = layout.px + (layout.pw - total_btn_w) * 0.5f;

    for (int i = 0; i < 3; ++i) {
        float btn_x = bx + i * (kCreateEnvBtnW + btn_gap);
        const auto& dc = *env_colors[i];
        if (i == create_popup.env_sel) {
            tr.draw_rect(btn_x, cy, kCreateEnvBtnW, kCreateEnvBtnH,
                         dc[0], dc[1], dc[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, env_labels[i], 0.0f, 0.0f, 0.0f);
        } else {
            tr.draw_rect(btn_x, cy, kCreateEnvBtnW, kCreateEnvBtnH,
                         style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, env_labels[i], dc[0], dc[1], dc[2]);
        }
    }
    cy += kCreateEnvBtnH + 10.0f;

    // 3. Composite checkbox (control env only)
    if (show_composite) {
        draw_checkbox(tr, style, cx, cy + 2, 16.0f, create_popup.composite);
        tr.draw_text(cx + 22, cy + 2, T("composite_template", "Composite (ChildOp template)"),
                     style.dim_text[0], style.dim_text[1], style.dim_text[2]);
        cy += 24.0f + kCreateModalRowGap;
    }

    // 4. Name field
    draw_editing_text_field(tr, style, cx, cy, inner_w, 22.0f,
                           create_popup.name_buf, text_edit, blink_on);
    if (create_popup.name_buf.empty()) {
        tr.draw_text(cx + 4, cy + 2, "operator_name",
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
    }
    cy += kCreateModalFieldH + kCreateModalRowGap;

    // 5. MCP hint
    cy += kCreateModalSectionGap;
    tr.draw_text(cx, cy, "Use MCP opdev tools for custom ports and parameters",
                 style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.6f);
    cy += 18.0f + kCreateModalRowGap;

    // 6. Destination radio buttons
    cy += kCreateModalSectionGap;
    const char* dest_labels[] = { "Auto", "Project", "Core" };
    float dest_x = cx;
    bool project_available = commands_.has_project_clone_destination();
    for (int i = 0; i < 3; ++i) {
        bool selected = (create_popup.destination == i);
        bool disabled = (i == 1 && !project_available);
        float dw = tr.text_width(dest_labels[i]) + 24.0f;
        // Radio circle
        float circle_x = dest_x + 2;
        float circle_y = cy + 4;
        tr.draw_rect(circle_x, circle_y, 12.0f, 12.0f,
                     style.slider_track[0], style.slider_track[1], style.slider_track[2]);
        if (selected) {
            tr.draw_rect(circle_x + 3, circle_y + 3, 6.0f, 6.0f,
                         style.accent[0], style.accent[1], style.accent[2]);
        }
        float alpha = disabled ? 0.3f : 1.0f;
        tr.draw_text(dest_x + 18, cy + 2, dest_labels[i],
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], alpha);
        dest_x += dw + 12.0f;
    }
    cy += 22.0f + kCreateModalRowGap;

    // 7. Error area
    if (!create_popup.error.empty()) {
        tr.draw_text(cx, cy, create_popup.error.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    }
    cy += 18.0f + kCreateModalRowGap;

    // 8. Button row: [Create Empty] left, [Create] [Cancel] right
    float btn_y = cy;
    // Create Empty (left-aligned)
    tr.draw_rect(cx, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    tr.draw_text(cx + 8, btn_y + 5, T("create_empty", "Create Empty"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);

    // Cancel (right-aligned)
    float cancel_x = cx + inner_w - kCreateModalBtnW;
    tr.draw_rect(cancel_x, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 28, btn_y + 5, T("cancel", "Cancel"),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);

    // Create (to the left of Cancel)
    float create_x = cancel_x - kCreateModalBtnW - 8.0f;
    tr.draw_rect(create_x, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style.accent[0], style.accent[1], style.accent[2], 0.9f);
    tr.draw_text(create_x + 28, btn_y + 5, T("create", "Create"), 0.0f, 0.0f, 0.0f);
}

// -----------------------------------------------------------------------
// Preset name popup (Save with no active preset)
// -----------------------------------------------------------------------
void DialogManager::draw_preset_name_popup(Renderer2D& tr, const MouseState& /*mouse*/,
                                           const UIStyle& style, float popup_opacity,
                                           uint32_t win_w, uint32_t win_h,
                                           const TextEditState& text_edit, bool cursor_blink) {
    if (!preset_name.open) return;

    float wf = static_cast<float>(win_w);
    float hf = static_cast<float>(win_h);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style.scrim[0], style.scrim[1], style.scrim[2], style.scrim[3] * popup_opacity);

    float pw = 280.0f, ph = 70.0f;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    draw_shadow(tr, px, py, pw, ph, style.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style.corner_radius,
                         style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2,
                 style.accent[0], style.accent[1], style.accent[2]);

    float cx = px + 16.0f;
    float cy = py + 12.0f;

    tr.draw_text(cx, cy, T("save_preset", "Save Preset"),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    cy += 20.0f;

    // Text field
    float field_w = pw - 32.0f;
    tr.draw_rect(cx, cy, field_w, 22.0f,
                 style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
    tr.draw_rect(cx, cy, field_w, 1,
                 style.accent[0], style.accent[1], style.accent[2]);

    if (preset_name.buffer.empty()) {
        tr.draw_text(cx + 4, cy + 3, "Folder/Name",
                     style.dim_text[0], style.dim_text[1], style.dim_text[2], 0.5f);
    } else {
        tr.draw_text(cx + 4, cy + 3, preset_name.buffer.c_str(),
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    }
    if (cursor_blink) {
        int cpos = std::max(0, std::min(text_edit.cursor, static_cast<int>(preset_name.buffer.size())));
        float cur_x = cx + 4 + tr.text_width(preset_name.buffer.substr(0, cpos).c_str());
        tr.draw_rect(cur_x, cy + 1, 1.0f, 20.0f,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    }
}

// -----------------------------------------------------------------------
// Core update banner (non-modal, drawn inline in perf bar area)
// -----------------------------------------------------------------------
void DialogManager::draw_core_update_banner(Renderer2D& tr, const UIStyle& /*style*/,
                                            float banner_y, float max_w) {
    core_update.button_rects.clear();
    if (!core_update.open) return;

    float h = 28.0f;
    float w = max_w;

    tr.draw_rect(0.0f, banner_y, w, h, 0.14f, 0.20f, 0.26f, 0.95f);
    tr.draw_rect(0.0f, banner_y, w, 1.0f, 0.28f, 0.46f, 0.58f, 0.8f);

    std::string label = "Core update available: v" + core_update.version;
    if (!core_update.summary.empty()) label += " - " + core_update.summary;
    tr.draw_text(10.0f, banner_y + 6.0f, label.c_str(), 0.86f, 0.92f, 0.98f);

    float bx = w - 12.0f;
    auto draw_btn = [&](const char* text, int action, float r, float g, float b) {
        float bw = tr.text_width(text) + 14.0f;
        bx -= bw;
        tr.draw_rounded_rect(bx, banner_y + 4.0f, bw, 20.0f, 3.0f, r, g, b, 0.85f);
        tr.draw_text(bx + 7.0f, banner_y + 6.0f, text, 0.95f, 0.97f, 1.0f);
        core_update.button_rects.push_back({bx, banner_y + 4.0f, bw, 20.0f, action});
        bx -= 6.0f;
    };

    draw_btn("Later", 2, 0.26f, 0.30f, 0.34f);
    draw_btn("Skip", 1, 0.33f, 0.25f, 0.23f);
    draw_btn("Install", 0, 0.22f, 0.42f, 0.28f);
}

} // namespace vivid::ui
