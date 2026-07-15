#include "ui/preset_popover.h"

#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "ui/node_graph.h"
#include "app/app.h"
#include "app/node_presets.h"

#include <cstdio>

namespace vivid::ui {

void draw_preset_popover(Renderer2D& ui, App& app, int node_idx, int win_w, int win_h) {
    if (!app.graph || node_idx < 0) return;
    const std::string op_type = app.graph->op_type_at(node_idx);
    const auto presets = node_presets::list(op_type);
    const int n = static_cast<int>(presets.size());
    const PresetGeom o = preset_geom(n, win_w);
    const Style& sty = style();

    overlay_panel(ui, { o.px, o.py, o.w, o.h }, nullptr, sty.gpu, true,
                  { 0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f });
    char title[64]; std::snprintf(title, sizeof title, "PRESETS \xC2\xB7 %.24s", op_type.c_str());
    ui.draw_text(o.px + 14.f, o.py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 0.98f);
    ui.draw_text(o.px + o.w - 92.f, o.py + 14.f, "Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.76f);

    // "Save current" action row.
    const Rect sv = preset_save_row(o.px, o.py, o.w, o.hdr, o.rowh);
    item_box(ui, sv, sty.gpu, false, false, AccentEdge::Left);
    ui.draw_text(sv.x + 8.f, sv.y + 6.f, "+ Save current params", sty.gpu[0], sty.gpu[1], sty.gpu[2], 1.0f, 0.84f);

    if (n == 0) {
        ui.draw_text(o.px + 14.f, o.py + o.hdr + o.rowh + 6.f, "No presets yet \xE2\x80\x94 save the current look above.",
                     0.55f, 0.57f, 0.6f, 1.0f, 0.82f);
        return;
    }
    for (int i = 0; i < o.vis; ++i) {
        const auto& p = presets[i];
        const Rect row = preset_list_row(o.px, o.py, o.w, o.hdr, o.rowh, i);
        if (i & 1) ui.draw_rect(row.x - 4.f, row.y - 1.f, row.w + 8.f, row.h, 0.14f, 0.15f, 0.18f, 0.5f);
        ui.draw_text(row.x + 6.f, row.y + 6.f, p.name.c_str(), 0.85f, 0.88f, 0.92f, 1.0f, 0.84f);
        if (p.factory)
            ui.draw_text(row.x + row.w - 96.f, row.y + 6.f, "factory", 0.55f, 0.58f, 0.62f, 1.0f, 0.72f);
        else {   // a user preset can be deleted
            const Rect del = preset_del_rect(row);
            ui.draw_text(del.x + 4.f, del.y + 5.f, "\xC3\x97", 0.75f, 0.45f, 0.45f, 1.0f, 0.92f);
        }
    }
    if (n > o.vis) {
        char more[24]; std::snprintf(more, sizeof more, "+%d more\xE2\x80\xA6", n - o.vis);
        ui.draw_text(o.px + 14.f, o.py + o.hdr + (o.vis + 1) * o.rowh + 2.f, more, 0.5f, 0.52f, 0.56f, 1.0f, 0.78f);
    }
}

}  // namespace vivid::ui
