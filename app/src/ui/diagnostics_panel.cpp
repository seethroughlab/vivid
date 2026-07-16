#include "ui/diagnostics_panel.h"

#include "ui/renderer_2d.h"
#include "app/app.h"
#include "app/log.h"
#include "gpu/visual_graph.h"

#include <cstdio>
#include <string>

namespace vivid::ui {

void draw_diagnostics_panel(Renderer2D& ui, const HealthSnapshot& h, const App& app, int win_w, int win_h) {
    const std::vector<int> missing = app.vgraph ? app.vgraph->missing_op_node_indices() : std::vector<int>{};
    const DiagGeom o = diag_geom(static_cast<int>(missing.size()), win_w);
    const Style& sty = style();
    const Severity sev = severity(h);

    overlay_panel(ui, { o.px, o.py, o.w, o.h }, nullptr, severity_color(sev), true,
                  { 0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f });
    // Header: a severity dot + title + close hint.
    const float* sc = severity_color(sev);
    ui.draw_rounded_rect(o.px + 14.f, o.py + 12.f, 10.f, 10.f, 2.f, sc[0], sc[1], sc[2], 1.0f);
    char title[64]; std::snprintf(title, sizeof title, "DIAGNOSTICS  \xE2\x80\x94  %s", severity_str(sev));
    ui.draw_text(o.px + 32.f, o.py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 0.94f);
    ui.draw_text(o.px + o.w - 96.f, o.py + 14.f, "Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.76f);

    int row = 0;
    auto line = [&](const char* label, const std::string& value, const float* vc) {
        const float ry = o.py + o.hdr + row * o.rowh;
        ui.draw_text(o.px + 14.f, ry + 4.f, label, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.80f);
        char v[80]; std::snprintf(v, sizeof v, "%.72s", value.c_str());
        ui.draw_text(o.px + 150.f, ry + 4.f, v, vc[0], vc[1], vc[2], 1.0f, 0.82f);
        ++row;
    };

    char buf[80];
    line("Severity", severity_str(sev), sc);
    std::snprintf(buf, sizeof buf, h.gpu_ok ? "device ok, %u error(s)" : "DEVICE LOST, %u error(s)", h.gpu_errors);
    line("GPU", buf, h.gpu_ok ? (h.gpu_errors ? sty.gold : sty.green) : sty.red);
    line("GPU last error", h.gpu_last_error.empty() ? "\xE2\x80\x94" : h.gpu_last_error,
         h.gpu_last_error.empty() ? sty.body : sty.gold);
    std::snprintf(buf, sizeof buf, "%d nodes, %d op types", h.op_nodes, h.op_types);
    line("Graph", buf, sty.body);
    std::snprintf(buf, sizeof buf, "%d", h.missing_ops);
    line("Missing operators", buf, h.missing_ops ? sty.red : sty.green);
    std::snprintf(buf, sizeof buf, "%d dylib(s)", h.packages_loaded);
    line("Packages loaded", buf, sty.body);
    std::snprintf(buf, sizeof buf, "%s   \xC2\xB7   v%s", h.control_running ? "control server up" : "control server DOWN",
                  h.app_version.c_str());
    line("Runtime", buf, h.control_running ? sty.body : sty.gold);

    // Missing-operator node rows — clickable: click one to select that node in the graph.
    if (!missing.empty()) {
        const float ry = o.py + o.hdr + row * o.rowh;
        ui.draw_text(o.px + 14.f, ry + 4.f, "MISSING OPERATORS  (click to select)",
                     sty.red[0], sty.red[1], sty.red[2], 1.0f, 0.74f);
        for (int i = 0; i < static_cast<int>(missing.size()); ++i) {
            const Rect rr = diag_missing_row_rect(o, i);
            const bool hov = false;   // (hover feedback optional; the modal handler owns clicks)
            (void)hov;
            const int idx = missing[i];
            const std::string& type = app.vgraph->nodes()[static_cast<size_t>(idx)].op_type;
            item_box(ui, rr, nullptr, false, false, AccentEdge::None);
            char lbl[64]; std::snprintf(lbl, sizeof lbl, "node %d  \xE2\x80\x94  '%.40s' not registered", idx, type.c_str());
            ui.draw_text(rr.x + 8.f, rr.y + 5.f, lbl, 0.86f, 0.6f, 0.58f, 1.0f, 0.80f);
        }
    }
}

void draw_log_view(Renderer2D& ui, const Logger& log, int win_w, int win_h) {
    const Style& sty = style();
    const auto& entries = log.entries();
    const int n = static_cast<int>(entries.size());
    const float w = 720.f, rowh = 18.f, hdr = 40.f;
    const int vis = std::max(1, std::min(n, 22));
    const float h = hdr + vis * rowh + 12.f;
    const float px = (win_w - w) * 0.5f, py = 72.f;

    overlay_panel(ui, { px, py, w, h }, nullptr, sty.control, true,
                  { 0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f });
    char title[48]; std::snprintf(title, sizeof title, "LOG  (%d)", n);
    ui.draw_text(px + 16.f, py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 0.94f);
    ui.draw_text(px + w - 96.f, py + 14.f, "Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.76f);

    if (n == 0) {
        ui.draw_text(px + 16.f, py + hdr + 6.f, "No log entries yet.", 0.55f, 0.57f, 0.6f, 1.0f, 0.82f);
        return;
    }
    // Newest at the bottom of the list: show the last `vis` entries in chronological order.
    for (int i = 0; i < vis; ++i) {
        const LogEntry& e = entries[static_cast<size_t>(n - vis + i)];
        const float ry = py + hdr + i * rowh;
        const float* lc = e.level == LogLevel::Error ? sty.red
                        : (e.level == LogLevel::Warning ? sty.gold
                        : (e.level == LogLevel::Debug ? sty.dim : sty.body));
        char t[12]; std::snprintf(t, sizeof t, "%7.2fs", e.t);
        ui.draw_text(px + 14.f, ry + 3.f, t, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.72f);
        char lv[8]; std::snprintf(lv, sizeof lv, "%-5s", log_level_str(e.level));
        ui.draw_text(px + 72.f, ry + 3.f, lv, lc[0], lc[1], lc[2], 1.0f, 0.72f);
        char msg[120]; std::snprintf(msg, sizeof msg, "%.116s", e.msg);
        ui.draw_text(px + 120.f, ry + 3.f, msg, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.76f);
    }
}

}  // namespace vivid::ui
