#include "ui/diagnostics_panel.h"

#include "ui/renderer_2d.h"
#include "app/app.h"
#include "app/log.h"
#include "app/quarantine.h"       // Ph4 F2: surface auto-disabled (quarantined) operators
#include "app/crash_recovery.h"   // App::crash_recovery->crash_dir()
#include "gpu/visual_graph.h"

#include <cstdio>
#include <string>
#include <vector>

namespace vivid::ui {

void draw_diagnostics_panel(Renderer2D& ui, const HealthSnapshot& h, const App& app, int win_w, int win_h) {
    const std::vector<int> missing = app.vgraph ? app.vgraph->missing_op_node_indices() : std::vector<int>{};
    // Ph4 F2: quarantined operators (repeat-crashers auto-disabled this launch) — scanned only while
    // the panel is open. Was MCP-only; now the user can see WHY an op stopped working.
    const std::vector<QuarantineEntry> quar =
        app.crash_recovery ? scan_quarantine(app.crash_recovery->crash_dir()) : std::vector<QuarantineEntry>{};
    const DiagGeom o = diag_geom(static_cast<int>(missing.size()), win_w, static_cast<int>(quar.size()));
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
    // Blank-vs-empty (P2-03): an unfed Output is empty-by-design (benign, neutral colour), not a fault.
    line("Output", h.output_fed ? "feeding Output" : "no feed \xE2\x80\x94 empty by design",
         h.output_fed ? sty.body : sty.dim);
    std::snprintf(buf, sizeof buf, "%d dylib(s)", h.packages_loaded);
    line("Packages loaded", buf, sty.body);
    std::snprintf(buf, sizeof buf, "%s   \xC2\xB7   v%s", h.control_running ? "control server up" : "control server DOWN",
                  h.app_version.c_str());
    line("Runtime", buf, h.control_running ? sty.body : sty.gold);
    // ADR-0032 Phase A: the active audio OUTPUT device — name · rate · buffer. Gold when a saved device
    // was gone and the default was substituted; dim when audio is unavailable (headless).
    if (!h.audio_device_open) {
        line("Audio device", "unavailable", sty.dim);
    } else {
        const char* dn = h.audio_device_name.empty() ? "System Default" : h.audio_device_name.c_str();
        const double lat_ms = h.audio_device_sr
            ? h.audio_device_latency_frames * 1000.0 / h.audio_device_sr : 0.0;
        // ADR-0032 Phase B: append plugin-reported latency only when relevant (keeps idle sessions clean).
        char fx[40] = "";
        if (h.audio_plugin_latency_unknown)
            std::snprintf(fx, sizeof fx, "  \xC2\xB7  plugins: unknown");
        else if (h.audio_max_plugin_latency_samples > 0 && h.audio_device_sr)
            std::snprintf(fx, sizeof fx, "  \xC2\xB7  +%.0f ms fx",
                          h.audio_max_plugin_latency_samples * 1000.0 / h.audio_device_sr);
        std::snprintf(buf, sizeof buf, "%.28s  \xC2\xB7  %u Hz  \xC2\xB7  %u buf  \xC2\xB7  ~%.0f ms out%s%s", dn,
                      h.audio_device_sr, h.audio_device_period, lat_ms, fx,
                      h.audio_device_fallback ? "  (fb)" : "");
        line("Audio device", buf, h.audio_device_fallback ? sty.gold : sty.body);
    }
    // ADR-0032 Phase D1: the hardware INPUT (capture) — shown only when a duplex device is open, so a
    // playback-only session (the default) stays clean. name · latency · live level meter.
    if (h.audio_input_open) {
        const char* in_name = h.audio_input_name.empty() ? "System Default" : h.audio_input_name.c_str();
        const double in_lat = h.audio_device_sr
            ? h.audio_input_latency_frames * 1000.0 / h.audio_device_sr : 0.0;
        std::snprintf(buf, sizeof buf, "%.28s  \xC2\xB7  ~%.0f ms in  \xC2\xB7  level %.2f",
                      in_name, in_lat, h.audio_input_level);
        line("Audio input", buf, sty.body);
    }
    // ADR-0032 E1: plugin-delay compensation — shown only when ON (opt-in), so a default session stays
    // clean. Reports the added latency + how many tracks are aligned vs left live; gold if clamped.
    if (h.pdc_enabled) {
        const double pdc_ms = h.audio_device_sr
            ? h.pdc_applied_delay_samples * 1000.0 / h.audio_device_sr : 0.0;
        std::snprintf(buf, sizeof buf, "on  \xC2\xB7  +%.0f ms  \xC2\xB7  %d compensated  \xC2\xB7  %d live%s",
                      pdc_ms, h.pdc_tracks_compensated, h.pdc_tracks_live,
                      h.pdc_clamped ? "  \xC2\xB7  clamped" : "");
        line("PDC", buf, h.pdc_clamped ? sty.gold : sty.body);
    }
    // ADR-0031 §4: realtime audio health — recent bail/over-budget/skip deltas + callback-µs gauges.
    std::snprintf(buf, sizeof buf, "%llu bail, %llu over-budget, %llu skips  \xC2\xB7  %uus (max %u)",
                  static_cast<unsigned long long>(h.audio_render_bailouts),
                  static_cast<unsigned long long>(h.audio_over_budget),
                  static_cast<unsigned long long>(h.audio_handoff_skips),
                  h.audio_last_callback_us, h.audio_max_callback_us);
    const bool audio_err = h.audio_bailout_error_threshold > 0 &&
                           h.audio_render_bailouts >= h.audio_bailout_error_threshold;
    line("Audio RT", buf, audio_err ? sty.red
                        : (h.audio_over_budget || h.audio_handoff_skips) ? sty.gold : sty.green);

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

    // Quarantined-operator rows (Ph4 F2) — sit after the missing-op section. Informational: an
    // unquarantine needs a restart, so these are not clickable.
    if (!quar.empty()) {
        const int base_row = o.scalar_rows + (missing.empty() ? 0 : 1 + static_cast<int>(missing.size()));
        const float ry = o.py + o.hdr + base_row * o.rowh;
        ui.draw_text(o.px + 14.f, ry + 4.f, "QUARANTINED OPERATORS  (auto-disabled; restart to re-enable)",
                     sty.gold[0], sty.gold[1], sty.gold[2], 1.0f, 0.72f);
        for (int i = 0; i < static_cast<int>(quar.size()); ++i) {
            const Rect rr = { o.px + 10.f, ry + (1 + i) * o.rowh, o.w - 20.f, o.rowh };
            item_box(ui, rr, nullptr, false, false, AccentEdge::None);
            char lbl[80]; std::snprintf(lbl, sizeof lbl, "'%.40s'  \xE2\x80\x94  %d crash(es)",
                                        quar[i].type_name.c_str(), quar[i].crash_count);
            ui.draw_text(rr.x + 8.f, rr.y + 5.f, lbl, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f, 0.80f);
        }
    }
}

// UX Ph4 F3: the keyboard-shortcut cheat-sheet (toggle: ?). A static list so the single-key
// shortcuts are discoverable in-app rather than only in the docs.
void draw_shortcuts_overlay(Renderer2D& ui, int win_w, int win_h, bool clip_editor, bool audio_mode) {
    const Style& sty = style();
    struct Row { const char* key; const char* desc; };   // key == nullptr => a section heading
    static const Row rows[] = {
        {"Space",                    "Play / Stop"},
        {"R",                        "Record (arm)"},
        {"Tab",                      "Add a node (audio or visual, by cursor)"},
        {"[ / ]",                    "Select prev / next node (focused graph)"},
        {"Arrows",                   "Select the nearest node in that direction"},
        {"\\",                       "Switch keyboard focus: visual \xE2\x86\x94 audio graph"},
        {"\xE2\x8C\xAB / Del",       "Delete the selected node"},
        {"W",                        "Wire: press on a node, then W on a target"},
        {"\xE2\x87\xA7\xE2\x8C\xAB", "Disconnect the selected node's input"},
        {"`",                        "Musical typing (play the armed track)"},
        {"1 \xE2\x80\x93 9",         "Launch scene 1\xE2\x80\x93" "9"},
        {"M",                        "Mappings (audio\xE2\x86\x94visual)"},
        {"H",                        "Diagnostics"},
        {"J",                        "Log"},
        {"L",                        "Shader library"},
        {"\xE2\x8C\x98Z / \xE2\x87\xA7\xE2\x8C\x98Z", "Undo / Redo"},
        {"\xE2\x8C\x98N / O / S",     "New / Open / Save project"},
        {"?",                        "This shortcut list"},
        {"Esc",                      "Close an overlay / chooser"},
    };
    // ADR-0048 step 4: the clip editor's power keys belong here, not in an always-visible footer crawl.
    // Appended only while the editor is open, so the global list stays short the rest of the time.
    static const Row midi_rows[] = {
        {nullptr,                    "MIDI CLIP EDITOR"},
        {"B / S",                    "Draw tool / Select tool"},
        {"G",                        "Cycle the grid (1/4 \xE2\x80\xA6 1/64, triplets)"},
        {"K / \xE2\x87\xA7K",        "Scale root / scale type"},
        {"E",                        "Cycle the lane: velocity \xE2\x86\x92 bend \xE2\x86\x92 pressure \xE2\x86\x92 timbre"},
        {"\xE2\x8C\xA5 (drag)",      "Bypass grid snap (fine positioning)"},
        {"\xE2\x8C\x98U",            "Quantize the selection"},
        {"I / R",                    "Invert / Retrograde"},
        {"H / T",                    "Humanize / Strum"},
        {"Y",                        "Quantize pitches to the scale"},
        {"'",                        "Glide (bend each note in from the last)"},
        {"< / >",                    "Velocity softer / louder"},
        {"\xE2\x8C\x98" "C / V / D", "Copy / Paste / Duplicate the selection"},
    };
    static const Row audio_rows[] = {
        {nullptr,                    "AUDIO CLIP EDITOR"},
        {"scroll",                   "Pan \xC2\xB7 \xE2\x8C\x98 zoom \xC2\xB7 \xE2\x8C\xA5 amplitude"},
        {"drag brace",               "Trim the loop window"},
        {"drag marker",              "Move a warp marker"},
        {"\xE2\x87\xA7" "click",     "Add / delete a warp marker"},
    };
    const int base_n = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    const Row* extra = nullptr; int extra_n = 0;
    if (clip_editor) {
        extra   = audio_mode ? audio_rows : midi_rows;
        extra_n = audio_mode ? static_cast<int>(sizeof(audio_rows) / sizeof(audio_rows[0]))
                             : static_cast<int>(sizeof(midi_rows) / sizeof(midi_rows[0]));
    }
    const int n = base_n + extra_n;
    const float w = 460.f, rowh = 22.f, hdr = 40.f;
    const float h = hdr + n * rowh + 16.f;
    const float px = (win_w - w) * 0.5f, py = 84.f;
    overlay_panel(ui, { px, py, w, h }, nullptr, sty.control, true,
                  { 0.f, 40.f, static_cast<float>(win_w), static_cast<float>(win_h) - 40.f });
    ui.draw_text(px + 16.f, py + 12.f, "KEYBOARD SHORTCUTS", 0.9f, 0.92f, 0.95f, 1.0f, 0.94f);
    ui.draw_text(px + w - 96.f, py + 14.f, "Esc to close", 0.5f, 0.52f, 0.56f, 1.0f, 0.76f);
    for (int i = 0; i < n; ++i) {
        const Row& row = (i < base_n) ? rows[i] : extra[i - base_n];
        const float ry = py + hdr + i * rowh;
        if (!row.key) {   // a section heading: an amber kicker with a rule above it
            ui.draw_rect(px + 18.f, ry + 2.f, w - 36.f, 1.f,
                         sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
            ui.draw_text(px + 18.f, ry + 7.f, row.desc, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f, sty.fs_kicker);
            continue;
        }
        ui.draw_text(px + 18.f, ry + 4.f, row.key, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.82f);
        ui.draw_text(px + 168.f, ry + 4.f, row.desc, sty.body[0], sty.body[1], sty.body[2], 1.0f, 0.82f);
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
