#include "ui/shader_library_view.h"

#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "gpu/shader_library.h"

#include <cstdio>
#include <filesystem>

namespace vivid::ui {

void draw_shader_library_view(Renderer2D& ui, const ShaderLibrary& lib, int win_w, int win_h) {
    const auto& entries = lib.entries();
    const int n = static_cast<int>(entries.size());
    const ShaderViewGeom o = shader_view_geom(n, win_w);
    const float px = o.px, py = o.py, w = o.w, rowh = o.rowh, hdr = o.hdr;
    const Style& sty = style();

    overlay_panel(ui, { px, py, w, o.h }, nullptr, sty.gpu, true,
                  { 0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f });
    char title[48]; std::snprintf(title, sizeof title, "SHADER LIBRARY  (%d)", n);
    ui.draw_text(px + 16.f, py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 1.0f);
    ui.draw_text(px + w - 130.f, py + 14.f, "Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.78f);
    ui.draw_text(px + 16.f, py + 38.f, "NAME", 0.45f, 0.48f, 0.53f, 1.0f, 0.74f);
    ui.draw_text(px + 210.f, py + 38.f, "TIER", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(px + 274.f, py + 38.f, "SUMMARY / ERROR", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);

    if (n == 0) {
        ui.draw_text(px + 16.f, py + hdr + 6.f,
                     "No shaders found. Drop a .wgsl into ~/Library/Application Support/Vivid/shaders.",
                     0.55f, 0.57f, 0.6f, 1.0f, 0.84f);
        return;
    }

    for (int i = 0; i < o.vis; ++i) {
        const auto& e = entries[i];
        const float ry = py + hdr + i * rowh;
        if (i & 1) ui.draw_rect(px + 2.f, ry - 2.f, w - 4.f, rowh, 0.14f, 0.15f, 0.18f, 0.5f);

        // A broken/shadowed row reads in a muted red; a registered one in normal text.
        const bool broken = !e.registered;
        const float nr = broken ? 0.86f : 0.85f, ng = broken ? 0.55f : 0.88f, nb = broken ? 0.52f : 0.92f;

        // Name — the op type, or the file's basename when the header did not even parse.
        std::string label = !e.name.empty() ? e.name
                                             : "(" + std::filesystem::path(e.path).filename().string() + ")";
        char nm[30]; std::snprintf(nm, sizeof nm, "%.28s", label.c_str());
        ui.draw_text(px + 16.f, ry + 6.f, nm, nr, ng, nb, 1.0f, 0.86f);

        char tier[10]; std::snprintf(tier, sizeof tier, "%.8s", e.tier.c_str());
        ui.draw_text(px + 210.f, ry + 6.f, tier, 0.6f, 0.63f, 0.68f, 1.0f, 0.78f);

        // Summary for a good row; the parse/shadow error for a bad one (mirrors disabled_note).
        const std::string& detail = broken ? e.error : e.summary;
        char dt[40]; std::snprintf(dt, sizeof dt, "%.38s", detail.c_str());
        ui.draw_text(px + 274.f, ry + 6.f, dt,
                     broken ? 0.80f : 0.70f, broken ? 0.45f : 0.72f, broken ? 0.42f : 0.75f, 1.0f, 0.80f);

        const ShaderViewRow rc = shader_view_row(px, w, ry);
        // Open works for any row (you open a broken file to fix it). Fork only a registered shader.
        item_box(ui, rc.open, nullptr, false, false, AccentEdge::None);
        ui.draw_text(rc.open.x + 6.f, ry + 9.f, "open", 0.78f, 0.81f, 0.85f, 1.0f, 0.78f);
        item_box(ui, rc.fork, nullptr, false, false, AccentEdge::None);
        ui.draw_text(rc.fork.x + 6.f, ry + 9.f, "fork",
                     e.registered ? 0.78f : 0.4f, e.registered ? 0.81f : 0.42f, e.registered ? 0.85f : 0.45f, 1.0f, 0.78f);
    }
    if (n > o.vis) {
        char more[24]; std::snprintf(more, sizeof more, "+%d more\xE2\x80\xA6", n - o.vis);
        ui.draw_text(px + 16.f, py + hdr + o.vis * rowh + 2.f, more, 0.5f, 0.52f, 0.56f, 1.0f, 0.8f);
    }
}

}  // namespace vivid::ui
