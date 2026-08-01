#pragma once
#include "ui/ui_style.h"        // Style, Rect, style()
#include "app/runtime_health.h" // Severity

namespace vivid { struct App; class Logger; }

namespace vivid::ui {
class Renderer2D;

// ADR-0019 (E2/E3) — the health surface. `severity_color` maps the rollup severity() already
// carries to a palette colour; it drives BOTH the transport-bar status dot (session_view) and this
// panel's header. The panel is pure presentation of the HealthSnapshot the engine already computes
// (one source, two views: this and MCP get_health) — it invents no metric the snapshot doesn't hold.
inline const float* severity_color(Severity sev) {
    const Style& s = style();
    switch (sev) {
        case Severity::Error:   return s.red;
        case Severity::Warning: return s.gold;
        default:                return s.green;
    }
}

// Panel geometry — shared by the draw and the modal hit-test in input.cpp (like shader_view_geom).
struct DiagGeom { float px, py, w, h, rowh, hdr; int scalar_rows, missing_rows; };
inline DiagGeom diag_geom(int missing_count, int win_w) {
    DiagGeom o;
    o.w = 460.f; o.rowh = 22.f; o.hdr = 40.f; o.scalar_rows = 8;
    o.missing_rows = missing_count;
    const int body_rows = o.scalar_rows + (missing_count > 0 ? 1 + missing_count : 0);   // +1 subheader
    o.h = o.hdr + body_rows * o.rowh + 16.f;
    o.px = (win_w - o.w) * 0.5f; o.py = 84.f;
    return o;
}
// The i-th broken-node row (a clickable "select this node" target). The scalar rows come first,
// then a "MISSING OPERATORS" subheader, then one row per broken node.
inline Rect diag_missing_row_rect(const DiagGeom& o, int i) {
    const float y0 = o.py + o.hdr + (o.scalar_rows + 1) * o.rowh;
    return { o.px + 10.f, y0 + i * o.rowh, o.w - 20.f, o.rowh };
}

void draw_diagnostics_panel(Renderer2D& ui, const HealthSnapshot& h, const App& app, int win_w, int win_h);

// ADR-0019 (E4): the in-app log view — the last N leveled entries, coloured by level. The same
// events that go to stderr, now visible in the app (toggle: J).
void draw_log_view(Renderer2D& ui, const Logger& log, int win_w, int win_h);

}  // namespace vivid::ui
