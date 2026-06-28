#include "ui/clip_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

// Modal panel geometry (window is 1280x800).
static constexpr float kPX = 300.f, kPY = 110.f, kPW = 900.f, kPH = 580.f;
static constexpr float kHeaderH = 30.f;
static constexpr int   kNumRows = 37;   // ~3 octaves visible

float ClipEditor::gx() const { return kPX + 10.f; }
float ClipEditor::gy() const { return kPY + kHeaderH + 10.f; }
float ClipEditor::gw() const { return kPW - 20.f; }
float ClipEditor::gh() const { return kPH - kHeaderH - 20.f; }
float ClipEditor::rh() const { return gh() / float(kNumRows); }
float ClipEditor::yp(int p) const { return gy() + float(pitch_lo_ + kNumRows - 1 - p) * rh(); }
int   ClipEditor::pitch_at(double y) const {
    return pitch_lo_ + kNumRows - 1 - static_cast<int>((y - gy()) / rh());
}
double ClipEditor::snap(double b) const { return std::round(b / cell_) * cell_; }

static bool is_black(int p) { int c = ((p % 12) + 12) % 12; return c==1||c==3||c==6||c==8||c==10; }

void ClipEditor::open(int track, int scene, const std::string& title,
                      const vivid_poc::ClipNote* notes, int n, double length) {
    track_ = track; scene_ = scene; title_ = title;
    length_ = length > 0 ? length : 4.0;
    notes_.assign(notes, notes + (n > 0 ? n : 0));
    // Frame the pitch window around the clip's notes.
    int lo = 127, hi = 0;
    for (const auto& nn : notes_) { lo = std::min(lo, nn.pitch); hi = std::max(hi, nn.pitch); }
    if (lo > hi) { pitch_lo_ = 48; }                       // empty clip
    else {
        int center = (lo + hi) / 2;
        pitch_lo_ = std::clamp(center - kNumRows / 2, 0, 127 - kNumRows);
    }
    drag_ = 0; drag_idx_ = -1; last_idx_ = -1; dirty_ = false;
    open_ = true;
}

int ClipEditor::hit_note(double x, double y, bool& right_edge) const {
    right_edge = false;
    for (int i = static_cast<int>(notes_.size()) - 1; i >= 0; --i) {  // topmost first
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

bool ClipEditor::on_down(double x, double y, double now) {
    if (!open_) return false;
    // Click outside the panel closes the editor.
    if (x < kPX || x >= kPX + kPW || y < kPY || y >= kPY + kPH) { close(); return true; }
    // Close button [X] in the header.
    if (x >= kPX + kPW - 30.f && y < kPY + kHeaderH) { close(); return true; }
    // Header (drag-less for now) consumes clicks.
    if (y < kPY + kHeaderH) return true;
    // Grid.
    if (x >= gx() && x < gx() + gw() && y >= gy() && y < gy() + gh()) {
        bool re; int idx = hit_note(x, y, re);
        if (idx >= 0) {
            if (last_idx_ == idx && now - last_down_ < 0.35) {     // double-click -> delete
                notes_.erase(notes_.begin() + idx);
                dirty_ = true; drag_ = 0; last_idx_ = -1; return true;
            }
            last_down_ = now; last_idx_ = idx;
            drag_ = re ? 2 : 1; drag_idx_ = idx;
            orig_start_ = notes_[idx].start; orig_dur_ = notes_[idx].dur; orig_pitch_ = notes_[idx].pitch;
            down_beat_ = beat_at(x); down_pitch_ = pitch_at(y);
            return true;
        }
        // Empty cell -> add a note and start moving it.
        double b = std::clamp(snap(beat_at(x)), 0.0, std::max(0.0, length_ - cell_));
        int p = std::clamp(pitch_at(y), 0, 127);
        notes_.push_back({ p, b, cell_, 0.8f });
        dirty_ = true;
        drag_ = 1; drag_idx_ = static_cast<int>(notes_.size()) - 1;
        orig_start_ = b; orig_dur_ = cell_; orig_pitch_ = p;
        down_beat_ = beat_at(x); down_pitch_ = p;
        last_down_ = now; last_idx_ = drag_idx_;
        return true;
    }
    return true;  // modal: consume everything inside the panel
}

void ClipEditor::on_move(double x, double y) {
    if (!open_ || drag_ == 0 || drag_idx_ < 0 || drag_idx_ >= static_cast<int>(notes_.size())) return;
    auto& n = notes_[drag_idx_];
    if (drag_ == 1) {  // move
        double ns = snap(orig_start_ + (beat_at(x) - down_beat_));
        n.start = std::clamp(ns, 0.0, std::max(0.0, length_ - n.dur));
        n.pitch = std::clamp(orig_pitch_ + (pitch_at(y) - down_pitch_), 0, 127);
    } else {           // resize
        double nd = snap(orig_dur_ + (beat_at(x) - down_beat_));
        n.dur = std::clamp(nd, cell_, length_ - n.start);
    }
    dirty_ = true;
}

void ClipEditor::on_up(double, double) { drag_ = 0; drag_idx_ = -1; }

void ClipEditor::scroll(double dy) {
    if (!open_) return;
    pitch_lo_ = std::clamp(pitch_lo_ + (dy > 0 ? 1 : -1), 0, 127 - kNumRows);
}

void ClipEditor::draw(Renderer2D& r) {
    if (!open_) return;
    r.draw_rect(0, 0, 1280, 800, 0.0f, 0.0f, 0.0f, 0.55f);            // dim backdrop
    r.draw_rect(kPX, kPY, kPW, kPH, 0.10f, 0.11f, 0.13f, 1.0f);       // panel
    r.draw_rect(kPX, kPY, kPW, kHeaderH, 0.15f, 0.16f, 0.19f, 1.0f);  // header
    r.draw_text(kPX + 12.f, kPY + 9.f, title_.c_str(), 0.88f, 0.91f, 0.95f, 1.0f, 0.95f);
    r.draw_text(kPX + kPW - 24.f, kPY + 8.f, "X", 0.8f, 0.55f, 0.55f, 1.0f, 1.0f);

    const float GX = gx(), GY = gy(), GW = gw(), GH = gh(), RH = rh();
    r.draw_rect(GX, GY, GW, GH, 0.07f, 0.08f, 0.10f, 1.0f);
    // pitch rows (black-key shading + octave lines)
    for (int p = pitch_lo_; p < pitch_lo_ + kNumRows; ++p) {
        const float y = yp(p);
        if (is_black(p)) r.draw_rect(GX, y, GW, RH, 0.09f, 0.10f, 0.12f, 1.0f);
        if (p % 12 == 0) {  // C
            r.draw_rect(GX, y, GW, 1.f, 0.22f, 0.24f, 0.28f, 1.0f);
            char lbl[8]; std::snprintf(lbl, sizeof lbl, "C%d", p / 12 - 1);
            r.draw_text(GX + 2.f, y + RH * 0.5f - 5.f, lbl, 0.4f, 0.43f, 0.48f, 1.0f, 0.72f);
        }
    }
    // beat / grid lines
    for (double b = 0; b <= length_ + 1e-6; b += cell_) {
        const float x = xb(b);
        const bool whole = std::fabs(b - std::round(b)) < 1e-6;
        r.draw_rect(x, GY, 1.f, GH, whole ? 0.24f : 0.14f, whole ? 0.26f : 0.15f, whole ? 0.30f : 0.17f, 1.0f);
    }
    // notes
    for (const auto& n : notes_) {
        if (n.pitch < pitch_lo_ || n.pitch >= pitch_lo_ + kNumRows) continue;
        const float nx = xb(n.start), ny = yp(n.pitch), nw = std::max(2.f, float(n.dur) * bw());
        const float v = 0.4f + 0.5f * std::clamp(n.vel, 0.f, 1.f);
        r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.30f * v + 0.1f, 0.78f * v, 0.80f * v, 1.0f);
        r.draw_rect(nx, ny + 1.f, 2.f, RH - 2.f, 0.6f, 0.92f, 0.9f, 1.0f);  // left accent
    }
    r.draw_text(kPX + 12.f, kPY + kPH - 20.f,
                "click empty = add  \xC2\xB7  drag = move  \xC2\xB7  drag right edge = resize  \xC2\xB7  double-click = delete  \xC2\xB7  scroll = pitch  \xC2\xB7  X / click outside = close",
                0.45f, 0.48f, 0.53f, 1.0f, 0.8f);
}

}  // namespace vivid::ui
