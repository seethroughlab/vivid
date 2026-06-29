#include "ui/mapping_overview.h"

#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "audio/vst3_host.h"

#include <cstdio>

namespace vivid::ui {

std::string mapping_dest_label(vivid_poc::Session* s, const std::string& dest) {
    if (dest.rfind("node:", 0) == 0) {  // "node:<id>.<name>" -> "node <id> · <name> (visual)"
        const size_t dot = dest.find('.', 5);
        if (dot != std::string::npos)
            return "node " + dest.substr(5, dot - 5) + " \xC2\xB7 " + dest.substr(dot + 1) + "  (visual)";
    }
    if (dest.rfind("uniform.", 0) == 0) return dest.substr(8) + "  (visual)";
    if (dest.rfind("param:", 0) == 0) {
        int T = -1, D = 0, I = 0;
        if (std::sscanf(dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && s) {
            const char* pn = vivid_poc::session_param_name(s, T, D, I);
            char buf[72]; std::snprintf(buf, sizeof buf, "T%d %s: %.20s", T, D == 0 ? "inst" : "fx", pn ? pn : "param");
            return buf;
        }
    }
    return dest;
}

void draw_mapping_overview(Renderer2D& ui, NodeGraph* g, vivid_poc::Session* s, int win_w, int win_h) {
    if (!g) return;
    const auto& maps = g->mappings();
    const int n = static_cast<int>(maps.size());
    const OvGeom o = ov_geom(n, win_w);
    const float px = o.px, py = o.py, w = o.w, rowh = o.rowh, hdr = o.hdr;
    ui.draw_rect(0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f, 0.f, 0.f, 0.f, 0.45f);  // scrim
    ui.draw_rounded_rect(px, py, w, o.h, 6.f, 0.12f, 0.13f, 0.155f, 1.0f);
    ui.draw_rect(px, py, w, 3.f, 0.45f, 0.62f, 0.85f, 1.0f);
    char title[48]; std::snprintf(title, sizeof title, "MAPPINGS  (%d)", n);
    ui.draw_text(px + 16.f, py + 12.f, title, 0.9f, 0.92f, 0.95f, 1.0f, 1.0f);
    ui.draw_text(px + w - 150.f, py + 14.f, "M or Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.78f);
    const OvRow hc = ov_row(px, w, py + 38.f);
    ui.draw_text(px + 16.f, py + 38.f, "SOURCE \xE2\x86\x92 DEST", 0.45f, 0.48f, 0.53f, 1.0f, 0.74f);
    ui.draw_text(hc.inv.x, py + 38.f, "POL", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.amtMinus.x + 2.f, py + 38.f, "AMT", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.curMinus.x - 2.f, py + 38.f, "CURVE", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.loMinus.x + 4.f, py + 38.f, "LO", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    ui.draw_text(hc.hiMinus.x + 4.f, py + 38.f, "HI", 0.45f, 0.48f, 0.53f, 1.0f, 0.72f);
    if (n == 0) {
        ui.draw_text(px + 16.f, py + hdr + 6.f, "No mappings yet \xE2\x80\x94 wire a data node to an op param, or map a device param (m).",
                     0.55f, 0.57f, 0.6f, 1.0f, 0.84f);
        return;
    }
    auto stepper = [&](const Rect& minus, const Rect& plus, float valX, float ry, const char* val) {
        ui.draw_rect(minus.x, minus.y, minus.w, minus.h, 0.18f, 0.19f, 0.22f, 1.0f);
        ui.draw_text(minus.x + 4.f, ry + 3.f, "-", 0.8f, 0.82f, 0.86f, 1.0f, 0.9f);
        ui.draw_rect(plus.x, plus.y, plus.w, plus.h, 0.18f, 0.19f, 0.22f, 1.0f);
        ui.draw_text(plus.x + 3.f, ry + 3.f, "+", 0.8f, 0.82f, 0.86f, 1.0f, 0.9f);
        ui.draw_text(valX, ry + 4.f, val, 0.78f, 0.81f, 0.85f, 1.0f, 0.82f);
    };
    for (int i = 0; i < o.vis; ++i) {
        const auto& m = maps[i];
        const float ry = py + hdr + i * rowh;
        if (i & 1) ui.draw_rect(px + 2.f, ry - 2.f, w - 4.f, rowh, 0.14f, 0.15f, 0.18f, 0.5f);
        const bool toVisual = m.dest.rfind("node:", 0) == 0 || m.dest.rfind("uniform.", 0) == 0;
        const bool fromViz = m.source.rfind("viz.", 0) == 0;
        float cr = 0.5f, cg = 0.7f, cb = 0.85f;                       // audio->audio
        if (toVisual) { cr = 0.31f; cg = 0.80f; cb = 0.75f; }         // audio->visual
        else if (fromViz) { cr = 0.85f; cg = 0.7f; cb = 0.4f; }       // visual->audio
        const OvRow rc = ov_row(px, w, ry);
        char src8[20]; std::snprintf(src8, sizeof src8, "%.18s", m.source.c_str());
        ui.draw_text(px + 16.f, ry + 4.f, src8, 0.85f, 0.88f, 0.92f, 1.0f, 0.82f);
        ui.draw_text(px + 150.f, ry + 3.f, "\xE2\x86\x92", cr, cg, cb, 1.0f, 0.92f);
        char dst22[26]; std::snprintf(dst22, sizeof dst22, "%.24s", mapping_dest_label(s, m.dest).c_str());
        ui.draw_text(px + 168.f, ry + 4.f, dst22, 0.82f, 0.85f, 0.9f, 1.0f, 0.82f);
        // polarity chip
        ui.draw_rect(rc.inv.x, rc.inv.y, rc.inv.w, rc.inv.h, m.invert ? 0.5f : 0.18f, m.invert ? 0.4f : 0.19f, m.invert ? 0.55f : 0.22f, 1.0f);
        ui.draw_text(rc.inv.x + 5.f, ry + 3.f, "inv", m.invert ? 0.95f : 0.6f, 0.9f, 0.95f, 1.0f, 0.74f);
        char amt[10]; std::snprintf(amt, sizeof amt, "%.2f", m.amount);
        stepper(rc.amtMinus, rc.amtPlus, rc.amtValX, ry, amt);
        char cur[10]; std::snprintf(cur, sizeof cur, "%+.2f", m.curve);
        stepper(rc.curMinus, rc.curPlus, rc.curValX, ry, cur);
        char lo[10]; std::snprintf(lo, sizeof lo, "%.2f", m.out_lo);
        stepper(rc.loMinus, rc.loPlus, rc.loValX, ry, lo);
        char hi[10]; std::snprintf(hi, sizeof hi, "%.2f", m.out_hi);
        stepper(rc.hiMinus, rc.hiPlus, rc.hiValX, ry, hi);
        ui.draw_text(rc.clear.x + 2.f, ry + 3.f, "\xC3\x97", 0.75f, 0.45f, 0.45f, 1.0f, 0.95f);
    }
    if (n > o.vis) {
        char more[24]; std::snprintf(more, sizeof more, "+%d more\xE2\x80\xA6", n - o.vis);
        ui.draw_text(px + 16.f, py + hdr + o.vis * rowh + 2.f, more, 0.5f, 0.52f, 0.56f, 1.0f, 0.8f);
    }
}

}  // namespace vivid::ui
