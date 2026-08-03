#include "ui/chooser.h"
#include "ui/text_match.h"
#include "ui/ui_style.h"
#include "ui/chooser_rank.h"   // role_chip_label (ADR-0046 role chip)

#include <algorithm>
#include <utility>

namespace vivid::ui {

void Chooser::set_entries(std::vector<ChooserEntry> entries) {
    entries_ = std::move(entries);
    for (ChooserEntry& e : entries_) e.hay = lower_str(e.hay + " " + e.label + " " + e.summary);
    rebuild();
}

void Chooser::show(double sx, double sy, float bx0, float by0, float bx1, float by1) {
    open_ = true;
    filter_.clear();
    sel_ = 0;
    sx_ = static_cast<float>(sx); sy_ = static_cast<float>(sy);   // the spawn anchor
    bx0_ = bx0; by0_ = by0; bx1_ = bx1; by1_ = by1;
    rebuild();
}

void Chooser::rebuild() {
    hits_.clear();
    const std::string f = lower_str(filter_);
    std::vector<std::pair<int, int>> scored;   // (score, entry index)
    scored.reserve(entries_.size());
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const int sc = score_match(lower_str(entries_[i].label), entries_[i].hay, f);
        if (sc >= 0) scored.push_back({ sc, i });
    }
    // Best match first; ties keep catalog order (which the owner has already grouped sensibly).
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& s : scored) hits_.push_back(s.second);
    sel_ = std::clamp(sel_, 0, std::max(0, static_cast<int>(hits_.size()) - 1));
}

void Chooser::move(int dir) {
    if (hits_.empty()) return;
    const int n = static_cast<int>(hits_.size());
    sel_ = (sel_ + dir % n + n) % n;
}
void Chooser::backspace() { if (!filter_.empty()) { filter_.pop_back(); rebuild(); } }
void Chooser::type(unsigned int cp) { if (cp >= 32 && cp < 127) { filter_.push_back(static_cast<char>(cp)); rebuild(); } }

const ChooserEntry* Chooser::confirm() {
    if (sel_ < 0 || sel_ >= static_cast<int>(hits_.size())) { hide(); return nullptr; }
    const ChooserEntry& e = entries_[static_cast<size_t>(hits_[static_cast<size_t>(sel_)])];
    if (!e.enabled) return nullptr;   // a listed-but-unhostable entry: stay open, say nothing
    hide();
    return &e;
}

const ChooserEntry* Chooser::click(double x, double y, bool& dismissed) {
    dismissed = false;
    float px, py, w, h; int vis, first;
    geom(px, py, w, h, vis, first);
    if (x < px || x >= px + w || y < py + kHdrH || y >= py + kHdrH + vis * kRowH) {
        dismissed = true;   // clicked away
        hide();
        return nullptr;
    }
    const int hi = first + static_cast<int>((y - (py + kHdrH)) / kRowH);
    if (hi < 0 || hi >= static_cast<int>(hits_.size())) return nullptr;
    sel_ = hi;
    return confirm();
}

// Anchored at the cursor (the node spawns there too, so the palette appears where the work is),
// nudged back inside the graph region when it would overhang.
void Chooser::geom(float& px, float& py, float& w, float& h, int& vis, int& first) const {
    w = kW;
    const int total = static_cast<int>(hits_.size());
    vis   = std::max(1, std::min(total, kMaxRows));
    first = sel_ >= vis ? sel_ - vis + 1 : 0;
    h  = kHdrH + vis * kRowH + 6.f;
    px = std::clamp(sx_ + 8.f, bx0_, std::max(bx0_, bx1_ - w));
    py = std::clamp(sy_ + 8.f, by0_, std::max(by0_, by1_ - h));
}

void Chooser::draw(Renderer2D& r) const {
    if (!open_) return;
    const Style& sty = style();
    float px, py, w, h; int vis, first;
    geom(px, py, w, h, vis, first);
    const int total = static_cast<int>(hits_.size());
    overlay_panel(r, { px, py, w, h }, nullptr, sty.sel);

    const bool empty = filter_.empty();
    const std::string f = empty ? std::string("type to filter\xE2\x80\xA6") : (filter_ + "_");
    r.draw_text(px + 10.f, py + 7.f, f.c_str(), empty ? sty.dim[0] : sty.text[0],
                empty ? sty.dim[1] : sty.text[1], empty ? sty.dim[2] : sty.text[2], 1.0f, sty.fs_body);
    if (total == 0) {
        r.draw_text(px + 10.f, py + kHdrH + 4.f, "no match", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        return;
    }
    for (int vi = 0; vi < vis; ++vi) {
        const int hi = first + vi;
        if (hi >= total) break;
        const ChooserEntry& e = entries_[static_cast<size_t>(hits_[static_cast<size_t>(hi)])];
        const float iy = py + kHdrH + vi * kRowH;
        if (hi == sel_) r.draw_rect(px + 2.f, iy, w - 4.f, kRowH, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 0.9f);
        const float* acc = e.accent ? e.accent : sty.dim;
        // ADR-0050: when the owner supplies a preview painter, a 24x24 swatch replaces the accent dot
        // in the left gutter and the text column shifts right past it. Otherwise a 5x5 accent dot.
        float tx = px + 24.f;                // left text column (label + summary)
        if (preview_fn_) {
            const float sw = 24.f, sxp = px + 6.f, syp = iy + 3.f;
            preview_fn_(r, e, sxp, syp, sw, sw);
            tx = sxp + sw + 6.f;
        } else {
            r.draw_rect(px + 10.f, iy + 9.f, 5.f, 5.f, acc[0], acc[1], acc[2], e.enabled ? 1.0f : 0.4f);
        }
        // A disabled row stays VISIBLE (the catalog must not lie about what you own) but reads as
        // unavailable, and says why in place of its summary.
        const float* nc = e.enabled ? sty.text : sty.dim;
        const float rx = px + w - 12.f;      // common right margin (badge on top, role chip below)
        const float gap = 10.f;

        // Top line: badge right-aligned; label clamped so it can't run under the badge.
        float label_max = rx - tx;
        if (!e.badge.empty()) {
            draw_text_r(r, rx, iy + 3.f, e.badge.c_str(), acc, e.enabled ? 0.9f : 0.4f, sty.fs_kicker);
            label_max -= r.text_width(e.badge.c_str(), sty.fs_kicker) + gap;
        }
        r.draw_text(tx, iy + 3.f, fit_text(r, e.label, label_max, sty.fs_label).c_str(),
                    nc[0], nc[1], nc[2], e.enabled ? 1.0f : 0.6f, sty.fs_label);

        // ADR-0046: bottom line: a right-aligned role chip (SOURCE/TRANSFORM/…/RECIPE) — the "labeled"
        // half of the ADR decision. Dim (NOT a zone accent) so it reads as a quiet classification, not a
        // second identity color. The summary is clamped so it never runs under the chip or the panel edge.
        const char* chip = e.enabled ? role_chip_label(e.role) : nullptr;
        float sub_max = rx - tx;
        if (chip) {
            draw_text_r(r, rx, iy + 16.f, chip, sty.dim, e.enabled ? 0.8f : 0.4f, sty.fs_kicker);
            sub_max -= r.text_width(chip, sty.fs_kicker) + gap;
        }
        const std::string& sub = e.enabled ? e.summary : e.disabled_note;
        if (!sub.empty())
            r.draw_text(tx, iy + 16.f, fit_text(r, sub, sub_max, sty.fs_kicker).c_str(),
                        sty.dim[0], sty.dim[1], sty.dim[2], e.enabled ? 1.0f : 0.7f, sty.fs_kicker);
    }
}

}  // namespace vivid::ui
