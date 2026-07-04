#include "ui/clip_editor.h"
#include "midi/note_ops.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

using vivid::session::ClipNote;

static constexpr float kFloatW = 900.f, kFloatH = 560.f;  // floating size
static constexpr float kDockH  = 300.f;                   // docked bottom-strip height
static constexpr float kHeaderH = 30.f;

// Grid/snap presets (beats). Straight + triplet subdivisions.
struct GridPreset { double v; const char* label; };
static const GridPreset kGrids[] = {
    {1.0, "1/4"}, {0.5, "1/8"}, {1.0/3.0, "1/8T"}, {0.25, "1/16"},
    {0.25*2.0/3.0, "1/16T"}, {0.125, "1/32"}, {0.0625, "1/64"},
};
static constexpr int kNumGrids = static_cast<int>(sizeof(kGrids) / sizeof(kGrids[0]));

void ClipEditor::panel(float& x, float& y, float& w, float& h) const {
    if (docked_) { x = 8.f; y = win_h_ - kDockH - 8.f; w = win_w_ - 16.f; h = kDockH; }
    else         { x = px_; y = py_;                   w = kFloatW;       h = kFloatH; }
}
float ClipEditor::gx() const { float x,y,w,h; panel(x,y,w,h); return x + 10.f; }
float ClipEditor::gy() const { float x,y,w,h; panel(x,y,w,h); return y + kHeaderH + 10.f; }
float ClipEditor::gw() const { float x,y,w,h; panel(x,y,w,h); return w - 20.f; }
float ClipEditor::gh() const { float x,y,w,h; panel(x,y,w,h); return h - kHeaderH - 20.f; }

int ClipEditor::pitch_at(double y) const {
    return view_pitch_top_ - static_cast<int>(std::floor((y - gy()) / row_h_));
}
double ClipEditor::snap(double b) const { return std::round(b / cell_) * cell_; }

bool ClipEditor::contains(double x, double y) const {
    if (!open_) return false;
    float px, py, pw, ph; panel(px, py, pw, ph);
    return x >= px && x < px + pw && y >= py && y < py + ph;
}

static bool is_black(int p) { int c = ((p % 12) + 12) % 12; return c==1||c==3||c==6||c==8||c==10; }

int ClipEditor::selected_count() const {
    int n = 0; for (uint8_t s : sel_) n += s ? 1 : 0; return n;
}
void ClipEditor::clear_sel() { std::fill(sel_.begin(), sel_.end(), uint8_t{0}); }

void ClipEditor::push_undo() {
    undo_.push_back(notes_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void ClipEditor::add_note(const vivid::session::ClipNote& n, bool select) {
    notes_.push_back(n);
    sel_.push_back(select ? 1 : 0);
    dirty_ = true;
}

void ClipEditor::delete_selected() {
    if (selected_count() == 0) return;
    push_undo();
    std::vector<vivid::session::ClipNote> keep;
    keep.reserve(notes_.size());
    for (size_t i = 0; i < notes_.size(); ++i) if (!sel_[i]) keep.push_back(notes_[i]);
    notes_.swap(keep);
    sel_.assign(notes_.size(), 0);
    dirty_ = true;
}

void ClipEditor::clamp_view() {
    beat_px_ = std::clamp(beat_px_, 8.f, 600.f);
    row_h_   = std::clamp(row_h_, 5.f, 44.f);
    const double visB = gw() / beat_px_;
    view_beat0_ = std::clamp(view_beat0_, 0.0, std::max(0.0, length_ - visB * 0.15));
    const int rows = std::max(1, static_cast<int>(roll_h() / row_h_));
    view_pitch_top_ = std::clamp(view_pitch_top_, std::min(127, rows - 1), 127);
}

void ClipEditor::fit_view() {
    int lo = 127, hi = 0;
    for (const auto& nn : notes_) { lo = std::min(lo, nn.pitch); hi = std::max(hi, nn.pitch); }
    if (lo > hi) { lo = 54; hi = 78; }                 // empty clip: center on the middle
    lo = std::max(0, lo - 2); hi = std::min(127, hi + 2);
    const int rows = std::max(1, hi - lo + 1);
    row_h_ = std::clamp(roll_h() / static_cast<float>(rows), 5.f, 44.f);
    beat_px_ = std::clamp(gw() / static_cast<float>(length_ > 0 ? length_ : 4.0), 8.f, 600.f);
    view_beat0_ = 0.0;
    view_pitch_top_ = hi;
    clamp_view();
}

void ClipEditor::open(int track, int scene, const std::string& title,
                      const vivid::session::ClipNote* notes, int n, double length) {
    track_ = track; scene_ = scene; title_ = title;
    length_ = length > 0 ? length : 4.0;
    notes_.assign(notes, notes + (n > 0 ? n : 0));
    sel_.assign(notes_.size(), 0);
    undo_.clear(); redo_.clear();
    drag_ = 0; last_idx_ = -1; dirty_ = false; audio_ = false;
    tool_ = Tool::Draw;
    grid_idx_ = std::clamp(grid_idx_, 0, kNumGrids - 1);
    cell_ = kGrids[grid_idx_].v;
    open_ = true;
    fit_view();
}

void ClipEditor::open_audio(int track, int scene, const std::string& title,
                            const float* bins, int n, float t0, float t1) {
    track_ = track; scene_ = scene; title_ = title;
    wave_.assign(bins, bins + (n > 0 ? n : 0));
    t0_ = t0; t1_ = t1;
    drag_ = 0; dirty_ = false;
    audio_ = true;
    open_ = true;
}

int ClipEditor::hit_note(double x, double y, bool& right_edge) const {
    right_edge = false;
    for (int i = static_cast<int>(notes_.size()) - 1; i >= 0; --i) {
        const auto& n = notes_[i];
        const float nx = xb(n.start), ny = yp(n.pitch), nw = float(n.dur) * bw(), nh = rh();
        if (x >= nx && x < nx + nw && y >= ny && y < ny + nh) {
            const float edge = std::min(7.f, nw * 0.4f);
            right_edge = (x >= nx + nw - edge);
            return i;
        }
    }
    return -1;
}

bool ClipEditor::on_down(double x, double y, double now, int mods) {
    if (!open_) return false;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    float px, py, pw, ph; panel(px, py, pw, ph);
    // Header: close [X], dock toggle, tool toggle, or drag-to-move.
    if (y < py + kHeaderH) {
        if (x >= px + pw - 28.f)      { close(); return true; }                         // [X]
        if (x >= px + pw - 64.f)      { docked_ = !docked_; drag_ = 0; return true; }    // dock
        if (x >= px + pw - 124.f)     { tool_ = tool_ == Tool::Select ? Tool::Draw : Tool::Select; return true; }  // tool
        if (!docked_) { drag_ = 3; down_off_x_ = x - px_; down_off_y_ = y - py_; }       // start move
        return true;
    }
    // Content area.
    if (x >= gx() && x < gx() + gw() && y >= gy() && y < gy() + gh()) {
        if (audio_) {  // drag the nearer trim handle
            const float hx0 = gx() + t0_ * gw(), hx1 = gx() + t1_ * gw();
            drag_ = (std::fabs(x - hx0) <= std::fabs(x - hx1)) ? 10 : 11;
            return true;
        }
        // Velocity lane (bottom strip): drag the nearest note's velocity.
        if (y >= gy() + roll_h()) {
            int best = -1; double bestd = 1e18;
            for (size_t i = 0; i < notes_.size(); ++i) {
                double d = std::fabs(x - xb(notes_[i].start));
                if (d < bestd) { bestd = d; best = static_cast<int>(i); }
            }
            if (best >= 0 && bestd < 40.0) {
                push_undo();
                if (!sel_[best]) { clear_sel(); sel_[best] = 1; }
                drag_ = 5; lane_idx_ = best;
            }
            return true;
        }
        bool re; int idx = hit_note(x, y, re);
        if (idx >= 0) {
            if (shift) {                                           // shift-click toggles selection
                sel_[idx] = sel_[idx] ? 0 : 1; last_down_ = now; last_idx_ = idx; return true;
            }
            if (last_idx_ == idx && now - last_down_ < 0.35) {     // double-click -> delete
                push_undo();
                notes_.erase(notes_.begin() + idx);
                sel_.erase(sel_.begin() + idx);
                dirty_ = true; drag_ = 0; last_idx_ = -1; return true;
            }
            last_down_ = now; last_idx_ = idx;
            if (!sel_[idx]) { clear_sel(); sel_[idx] = 1; }        // select (plain click)
            push_undo();
            drag_ = re ? 2 : 1;
            drag_orig_ = notes_;
            down_beat_ = beat_at(x); down_pitch_ = pitch_at(y);
            return true;
        }
        // Empty: Draw tool adds a note; Select tool starts a marquee (shift = additive).
        if (tool_ == Tool::Draw) {
            double b = std::clamp(snap(beat_at(x)), 0.0, std::max(0.0, length_ - cell_));
            int p = std::clamp(pitch_at(y), 0, 127);
            push_undo();
            clear_sel();
            add_note({ p, b, cell_, 0.8f }, /*select*/true);
            drag_ = 1; drag_orig_ = notes_;
            down_beat_ = beat_at(x); down_pitch_ = p;
            last_down_ = now; last_idx_ = static_cast<int>(notes_.size()) - 1;
        } else {
            if (!shift) clear_sel();
            drag_ = 4; marq_add_ = shift;
            down_beat_ = beat_at(x); down_pitch_ = pitch_at(y);
            marq_x_ = x; marq_y_ = y;
        }
        return true;
    }
    return true;  // swallow clicks elsewhere in the panel
}

void ClipEditor::on_move(double x, double y) {
    if (!open_ || drag_ == 0) return;
    if (drag_ == 3) {  // move floating panel
        px_ = std::clamp(static_cast<float>(x - down_off_x_), -kFloatW + 80.f, win_w_ - 80.f);
        py_ = std::clamp(static_cast<float>(y - down_off_y_), 44.f, win_h_ - kHeaderH - 4.f);
        return;
    }
    if (drag_ == 10 || drag_ == 11) {  // audio trim handles
        const float f = std::clamp(static_cast<float>((x - gx()) / gw()), 0.f, 1.f);
        if (drag_ == 10) t0_ = std::min(f, t1_ - 0.02f);
        else             t1_ = std::max(f, t0_ + 0.02f);
        t0_ = std::clamp(t0_, 0.f, 1.f); t1_ = std::clamp(t1_, 0.f, 1.f);
        dirty_ = true;
        return;
    }
    if (drag_ == 4) { marq_x_ = x; marq_y_ = y; return; }   // marquee: finalized on mouse-up
    if (drag_ == 5) {                                        // velocity lane drag
        if (lane_idx_ >= 0 && lane_idx_ < static_cast<int>(notes_.size())) {
            const float top = gy() + roll_h() + 4.f, bot = gy() + gh() - 4.f;
            float v = (bot - static_cast<float>(y)) / std::max(1.f, bot - top);
            vivid::session::set_velocity_selected(notes_, sel_, std::clamp(v, 0.f, 1.f));
            dirty_ = true;
        }
        return;
    }
    if (drag_ != 1 && drag_ != 2) return;
    const double dbeat = beat_at(x) - down_beat_;
    const int    dpitch = pitch_at(y) - down_pitch_;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (!sel_[i] || i >= drag_orig_.size()) continue;
        const auto& o = drag_orig_[i];
        if (drag_ == 1) {                                   // move
            double ns = snap(o.start + dbeat);
            notes_[i].start = std::clamp(ns, 0.0, std::max(0.0, length_ - o.dur));
            notes_[i].pitch = std::clamp(o.pitch + dpitch, 0, 127);
        } else {                                            // resize
            double nd = snap(o.dur + dbeat);
            notes_[i].dur = std::clamp(nd, cell_, length_ - notes_[i].start);
        }
    }
    dirty_ = true;
}

void ClipEditor::on_up(double x, double y) {
    if (drag_ == 4) finish_marquee(x, y);
    drag_ = 0; lane_idx_ = -1;
}

void ClipEditor::finish_marquee(double x, double y) {
    const double b0 = std::min(down_beat_, beat_at(x)), b1 = std::max(down_beat_, beat_at(x));
    const int    p0 = std::min(down_pitch_, pitch_at(y)), p1 = std::max(down_pitch_, pitch_at(y));
    if (!marq_add_) clear_sel();
    for (size_t i = 0; i < notes_.size(); ++i) {
        const auto& n = notes_[i];
        const bool xoverlap = n.start + n.dur >= b0 && n.start <= b1;   // note time-span intersects
        const bool yoverlap = n.pitch >= p0 && n.pitch <= p1;
        if (xoverlap && yoverlap) sel_[i] = 1;
    }
}

void ClipEditor::copy_sel() {
    auto c = vivid::session::copy_selected(notes_, sel_);
    if (!c.empty()) clip_ = std::move(c);
}

void ClipEditor::paste(double at_beat) {
    if (clip_.empty()) return;
    push_undo();
    clear_sel();
    size_t first, last;
    vivid::session::paste_at(notes_, sel_, clip_, at_beat, length_, first, last);
    (void)first; (void)last;
    dirty_ = true;
}

void ClipEditor::duplicate_sel() {
    auto c = vivid::session::copy_selected(notes_, sel_);
    if (c.empty()) return;
    const double span = std::max(vivid::session::notes_span(c), cell_);
    // Anchor at the earliest selected note's absolute start + one span.
    double lo = 1e18;
    for (size_t i = 0; i < notes_.size(); ++i) if (sel_[i]) lo = std::min(lo, notes_[i].start);
    push_undo();
    clear_sel();
    size_t first, last;
    vivid::session::paste_at(notes_, sel_, c, lo + span, length_, first, last);
    (void)first; (void)last;
    dirty_ = true;
}

void ClipEditor::on_scroll(double xoff, double yoff, int mods, double mx, double my) {
    if (!open_ || audio_) { if (open_ && audio_) {} return; }
    const bool cmd = (mods & GLFW_MOD_SUPER) != 0;
    const bool alt = (mods & GLFW_MOD_ALT) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    if (cmd) {                                              // horizontal zoom around cursor
        const double anchor = beat_at(mx);
        beat_px_ *= std::pow(1.15f, static_cast<float>(yoff));
        clamp_view();
        view_beat0_ = anchor - (mx - gx()) / beat_px_;
    } else if (alt) {                                       // vertical zoom around cursor
        const double anchor = view_pitch_top_ - (my - gy()) / row_h_;
        row_h_ *= std::pow(1.15f, static_cast<float>(yoff));
        clamp_view();
        view_pitch_top_ = static_cast<int>(std::lround(anchor + (my - gy()) / row_h_));
    } else if (shift || std::fabs(xoff) > 1e-3) {          // horizontal pan
        const double amt = std::fabs(xoff) > 1e-3 ? xoff : -yoff;
        view_beat0_ += amt * 0.5;
    } else {                                                // vertical pan (pitch)
        view_pitch_top_ += (yoff > 0 ? 2 : -2);
    }
    clamp_view();
}

bool ClipEditor::on_key(int key, int mods) {
    if (!open_ || audio_) return false;
    const bool cmd = (mods & GLFW_MOD_SUPER) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) { delete_selected(); return true; }
    if (cmd && key == GLFW_KEY_A) { std::fill(sel_.begin(), sel_.end(), uint8_t{1}); return true; }
    if (cmd && key == GLFW_KEY_Z) {
        if (shift) {                                        // redo
            if (!redo_.empty()) { undo_.push_back(notes_); notes_ = redo_.back(); redo_.pop_back();
                                  sel_.assign(notes_.size(), 0); dirty_ = true; }
        } else {                                            // undo
            if (!undo_.empty()) { redo_.push_back(notes_); notes_ = undo_.back(); undo_.pop_back();
                                  sel_.assign(notes_.size(), 0); dirty_ = true; }
        }
        return true;
    }
    if (cmd && key == GLFW_KEY_C) { copy_sel(); return true; }
    if (cmd && key == GLFW_KEY_X) { copy_sel(); delete_selected(); return true; }
    if (cmd && key == GLFW_KEY_V) {
        double at = (playhead_ >= 0.0 && length_ > 0.0) ? std::fmod(playhead_, length_) : 0.0;
        paste(std::max(0.0, at)); return true;
    }
    if (cmd && key == GLFW_KEY_D) { duplicate_sel(); return true; }
    if (cmd && key == GLFW_KEY_U) {                          // quantize starts to grid
        if (vivid::session::sel_count(sel_)) { push_undo();
            vivid::session::quantize_selected(notes_, sel_, cell_); dirty_ = true; }
        return true;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
        if (vivid::session::sel_count(sel_)) { push_undo();
            vivid::session::nudge_selected(notes_, sel_, key == GLFW_KEY_RIGHT ? cell_ : -cell_, length_);
            dirty_ = true; }
        return true;
    }
    if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
        if (vivid::session::sel_count(sel_)) { push_undo();
            int step = (key == GLFW_KEY_UP ? 1 : -1) * (shift ? 12 : 1);
            vivid::session::transpose_selected(notes_, sel_, step); dirty_ = true; }
        return true;
    }
    if (key == GLFW_KEY_G) { grid_idx_ = (grid_idx_ + 1) % kNumGrids; cell_ = kGrids[grid_idx_].v; return true; }
    if (key == GLFW_KEY_B) { tool_ = Tool::Draw;   return true; }
    if (key == GLFW_KEY_S) { tool_ = Tool::Select; return true; }
    return false;
}

void ClipEditor::draw(Renderer2D& r) {
    if (!open_) return;
    float px, py, pw, ph; panel(px, py, pw, ph);
    r.draw_rect(px, py, pw, ph, 0.10f, 0.11f, 0.13f, 1.0f);          // panel (non-modal)
    r.draw_rect(px, py, pw, 2.f, 0.31f, 0.55f, 0.80f, 1.0f);         // top accent
    r.draw_rect(px, py, pw, kHeaderH, 0.15f, 0.16f, 0.19f, 1.0f);    // header
    r.draw_text(px + 12.f, py + 9.f, title_.c_str(), 0.88f, 0.91f, 0.95f, 1.0f, 0.95f);
    if (!audio_) {
        const bool draw = tool_ == Tool::Draw;
        r.draw_text(px + pw - 120.f, py + 8.f, draw ? "Draw" : "Select",
                    draw ? 0.55f : 0.6f, draw ? 0.82f : 0.72f, draw ? 0.55f : 0.85f, 1.0f, 0.82f);
    }
    r.draw_text(px + pw - 60.f, py + 8.f, docked_ ? "float" : "dock", 0.6f, 0.72f, 0.78f, 1.0f, 0.8f);
    r.draw_text(px + pw - 22.f, py + 8.f, "X", 0.8f, 0.55f, 0.55f, 1.0f, 1.0f);

    const float GX = gx(), GY = gy(), GW = gw(), GH = gh();
    r.draw_rect(GX, GY, GW, GH, 0.07f, 0.08f, 0.10f, 1.0f);
    r.push_clip_rect(GX, GY, GW, GH);

    if (audio_) {
        const float x0 = GX + t0_ * GW, x1 = GX + t1_ * GW;
        const int n = static_cast<int>(wave_.size());
        const float midY = GY + GH * 0.5f, bw_ = (n > 0) ? std::max(1.f, GW / n) : 1.f;
        for (int i = 0; i < n; ++i) {
            const float wx = GX + static_cast<float>(i) / n * GW;
            const float h = std::min(wave_[i] * GH * 0.46f, GH * 0.49f);
            const bool in = (static_cast<float>(i) / n >= t0_ && static_cast<float>(i) / n < t1_);
            r.draw_rect(wx, midY - h, bw_, h * 2.f,
                        in ? 0.32f : 0.16f, in ? 0.72f : 0.26f, in ? 0.78f : 0.30f, 1.0f);
        }
        r.draw_rect(GX, GY, x0 - GX, GH, 0.0f, 0.0f, 0.0f, 0.45f);          // dim outside the loop
        r.draw_rect(x1, GY, GX + GW - x1, GH, 0.0f, 0.0f, 0.0f, 0.45f);
        r.draw_rect(x0 - 1.f, GY, 2.f, GH, 0.92f, 0.84f, 0.34f, 1.0f);      // trim handles
        r.draw_rect(x1 - 1.f, GY, 2.f, GH, 0.92f, 0.84f, 0.34f, 1.0f);
        r.pop_clip_rect();
        r.draw_text(px + 12.f, py + ph - 18.f,
                    "drag the yellow handles to set the loop region  \xC2\xB7  drag header to move  \xC2\xB7  dock / X",
                    0.45f, 0.48f, 0.53f, 1.0f, 0.78f);
        return;
    }

    const float RH = row_h_;
    const float ROLL = roll_h();
    const int rows = std::max(1, static_cast<int>(ROLL / RH) + 2);
    const int pTop = view_pitch_top_, pBot = view_pitch_top_ - rows;
    for (int p = pBot; p <= pTop; ++p) {
        if (p < 0 || p > 127) continue;
        const float y = yp(p);
        if (y > GY + ROLL) continue;
        if (is_black(p)) r.draw_rect(GX, y, GW, RH, 0.09f, 0.10f, 0.12f, 1.0f);
        if (p % 12 == 0) {
            r.draw_rect(GX, y, GW, 1.f, 0.22f, 0.24f, 0.28f, 1.0f);
            char lbl[8]; std::snprintf(lbl, sizeof lbl, "C%d", p / 12 - 1);
            r.draw_text(GX + 2.f, y + RH * 0.5f - 5.f, lbl, 0.4f, 0.43f, 0.48f, 1.0f, 0.72f);
        }
    }
    // Vertical grid across the visible beat range.
    const double bLeft = view_beat0_, bRight = view_beat0_ + GW / beat_px_;
    const double first = std::floor(bLeft / cell_) * cell_;
    for (double b = first; b <= bRight + 1e-6; b += cell_) {
        if (b < 0) continue;
        const float x = xb(b);
        const bool whole = std::fabs(b - std::round(b)) < 1e-6;
        r.draw_rect(x, GY, 1.f, ROLL, whole ? 0.24f : 0.14f, whole ? 0.26f : 0.15f, whole ? 0.30f : 0.17f, 1.0f);
    }
    // Notes.
    for (size_t i = 0; i < notes_.size(); ++i) {
        const auto& n = notes_[i];
        if (n.pitch < pBot - 1 || n.pitch > pTop + 1) continue;
        const float nx = xb(n.start), ny = yp(n.pitch), nw = std::max(2.f, float(n.dur) * bw());
        if (ny > GY + ROLL) continue;
        const float v = 0.4f + 0.5f * std::clamp(n.vel, 0.f, 1.f);
        if (sel_[i]) {
            r.draw_rect(nx - 1.f, ny, nw + 2.f, RH, 0.98f, 0.86f, 0.42f, 1.0f);   // selection halo
            r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.42f, 0.60f, 0.95f, 1.0f);
        } else {
            r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.30f * v + 0.1f, 0.78f * v, 0.80f * v, 1.0f);
        }
        r.draw_rect(nx, ny + 1.f, 2.f, RH - 2.f, 0.6f, 0.92f, 0.9f, 1.0f);
    }
    // Velocity lane (bottom strip): a bar per note, height = velocity.
    const float laneTop = GY + ROLL, laneBot = GY + GH, laneH = laneBot - laneTop - 6.f;
    r.draw_rect(GX, laneTop, GW, 1.f, 0.20f, 0.22f, 0.26f, 1.0f);
    r.draw_rect(GX, laneTop + 1.f, GW, laneBot - laneTop - 1.f, 0.05f, 0.055f, 0.07f, 1.0f);
    r.draw_text(GX + 2.f, laneTop + 3.f, "vel", 0.4f, 0.43f, 0.48f, 1.0f, 0.66f);
    for (size_t i = 0; i < notes_.size(); ++i) {
        const auto& n = notes_[i];
        const float bx = xb(n.start);
        if (bx < GX - 2.f || bx > GX + GW) continue;
        const float h = std::clamp(n.vel, 0.f, 1.f) * laneH;
        const bool s = sel_[i] != 0;
        r.draw_rect(bx, laneBot - 3.f - h, 2.5f, h,
                    s ? 0.98f : 0.35f, s ? 0.72f : 0.66f, s ? 0.30f : 0.72f, 1.0f);
    }
    // Playhead (spans roll + lane).
    if (playhead_ >= 0.0 && length_ > 0.0) {
        double p = std::fmod(playhead_, length_); if (p < 0) p += length_;
        const float x = xb(p);
        if (x >= GX && x < GX + GW) r.draw_rect(x, GY, 1.5f, GH, 0.95f, 0.35f, 0.35f, 1.0f);
    }
    // Marquee rectangle.
    if (drag_ == 4) {
        const float mx0 = std::min(static_cast<float>(marq_x_), xb(down_beat_));
        const float mx1 = std::max(static_cast<float>(marq_x_), xb(down_beat_));
        const float my0 = std::min(static_cast<float>(marq_y_), yp(down_pitch_));
        const float my1 = std::max(static_cast<float>(marq_y_), yp(down_pitch_));
        r.draw_rect(mx0, my0, mx1 - mx0, my1 - my0, 0.5f, 0.7f, 1.0f, 0.16f);
        r.draw_rect(mx0, my0, mx1 - mx0, 1.f, 0.6f, 0.78f, 1.0f, 0.7f);
        r.draw_rect(mx0, my1, mx1 - mx0, 1.f, 0.6f, 0.78f, 1.0f, 0.7f);
    }
    r.pop_clip_rect();

    char foot[200];
    std::snprintf(foot, sizeof foot,
                  "%s \xC2\xB7 grid %s \xC2\xB7 %d sel \xC2\xB7 B/S tool \xC2\xB7 G cycles grid \xC2\xB7 Cmd C/V/X/D \xC2\xB7 Cmd U quantize \xC2\xB7 arrows move/transpose",
                  tool_ == Tool::Draw ? "Draw" : "Select", kGrids[grid_idx_].label, selected_count());
    r.draw_text(px + 12.f, py + ph - 18.f, foot, 0.45f, 0.48f, 0.53f, 1.0f, 0.76f);
}

}  // namespace vivid::ui
