#include "ui/clip_editor.h"
#include "midi/note_ops.h"
#include "midi/note_tools.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

using vivid::session::ClipNote;
using vivid::session::ExprCurve;

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

static constexpr float kBendRange = 12.f;   // painted bend lane spans ±12 semitones
static const char* kAxisNames[3] = { "bend", "pressure", "timbre" };

// Scale highlighting: pitch-class membership sets (mirrors mcp/theory.py's scales).
struct ScaleDef { const char* label; uint16_t mask; };   // bit c set = pitch-class c in scale
static constexpr uint16_t SM(int a,int b,int c,int d,int e,int f,int g) {
    return uint16_t((1<<a)|(1<<b)|(1<<c)|(1<<d)|(1<<e)|(1<<f)|(1<<g));
}
static const ScaleDef kScales[] = {
    {"maj",  SM(0,2,4,5,7,9,11)},
    {"min",  SM(0,2,3,5,7,8,10)},
    {"harm", SM(0,2,3,5,7,8,11)},
    {"dor",  SM(0,2,3,5,7,9,10)},
    {"mix",  SM(0,2,4,5,7,9,10)},
    {"phr",  SM(0,1,3,5,7,8,10)},
    {"lyd",  SM(0,2,4,6,7,9,11)},
    {"penM", uint16_t((1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9))},
    {"penm", uint16_t((1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10))},
};
static constexpr int kNumScales = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
static const char* kPitchNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

void ClipEditor::set_playhead(double abs_beats) {
    playhead_ = abs_beats;
    if (!follow_ || audio_ || length_ <= 0.0 || beat_px_ <= 0.f) return;
    double p = std::fmod(abs_beats, length_); if (p < 0) p += length_;
    const double visB = gw() / beat_px_;
    if (p < view_beat0_ || p > view_beat0_ + visB * 0.92) {
        view_beat0_ = p - visB * 0.1;
        clamp_view();
    }
}

void ClipEditor::panel(float& x, float& y, float& w, float& h) const {
    // Docked = the shared bottom inspector/editor dock (matches Window::dock_h). Starts just
    // below the dock resize strip (dock_top-3..+4) so that handle stays draggable.
    if (docked_) { x = 8.f; y = win_h_ - dock_h_ + 5.f; w = win_w_ - 16.f; h = dock_h_ - 9.f; }
    else         { x = px_; y = py_;                    w = kFloatW;       h = kFloatH; }
}
float ClipEditor::gx() const { float x,y,w,h; panel(x,y,w,h); return x + 10.f; }
float ClipEditor::gy() const { float x,y,w,h; panel(x,y,w,h); return y + kHeaderH + 10.f; }
float ClipEditor::gw() const { float x,y,w,h; panel(x,y,w,h); return w - 20.f; }
float ClipEditor::gh() const { float x,y,w,h; panel(x,y,w,h); return h - kHeaderH - 20.f; }

int ClipEditor::pitch_of_row(int r) const {
    if (fold_) {
        if (fold_rows_.empty()) return 60;
        return fold_rows_[std::clamp(r, 0, static_cast<int>(fold_rows_.size()) - 1)];
    }
    return std::clamp(127 - r, 0, 127);
}
int ClipEditor::row_of_pitch(int p) const {
    if (fold_) {
        for (size_t i = 0; i < fold_rows_.size(); ++i) if (fold_rows_[i] == p) return static_cast<int>(i);
        // pitch not occupied: nearest row (fold_rows_ is descending)
        for (size_t i = 0; i < fold_rows_.size(); ++i) if (fold_rows_[i] < p) return static_cast<int>(i);
        return static_cast<int>(fold_rows_.size());
    }
    return 127 - p;
}
bool ClipEditor::vscroll_geom(float& tx, float& ty, float& tw, float& th, float& thumb0, float& thumbLen) const {
    const float top = roll_top(), bot = lane_top();
    const float trackH = bot - top;
    const float visRows = trackH / row_h_;
    const int   nr = nrows();
    if (visRows >= nr) return false;                 // everything fits
    tx = gx() + gw() - 7.f; ty = top; tw = 6.f; th = trackH;
    thumbLen = std::max(14.f, trackH * visRows / nr);
    thumb0   = ty + (trackH - thumbLen) * std::clamp(view_row_top_ / static_cast<float>(std::max(1, nr - static_cast<int>(visRows))), 0.f, 1.f);
    return true;
}
bool ClipEditor::hscroll_geom(float& tx, float& ty, float& tw, float& th, float& thumb0, float& thumbLen) const {
    const double visBeats = gw() / beat_px_;
    if (visBeats >= length_ || length_ <= 0) return false;
    tx = roll_x0(); ty = lane_top() - 7.f; tw = roll_w() - 8.f; th = 6.f;
    thumbLen = std::max(20.f, static_cast<float>(tw * visBeats / length_));
    const double maxScroll = std::max(1e-6, length_ - visBeats);
    thumb0   = tx + (tw - thumbLen) * static_cast<float>(std::clamp(view_beat0_ / maxScroll, 0.0, 1.0));
    return true;
}
void ClipEditor::rebuild_fold() {
    fold_rows_.clear();
    if (!fold_) return;
    std::vector<uint8_t> seen(128, 0);
    for (const auto& n : notes_) if (n.pitch >= 0 && n.pitch < 128) seen[n.pitch] = 1;
    for (int p = 127; p >= 0; --p) if (seen[p]) fold_rows_.push_back(p);   // descending = top-down
    if (fold_rows_.empty()) fold_rows_.push_back(60);   // empty clip: show at least one row
}
int ClipEditor::pitch_at(double y) const {
    return pitch_of_row(view_row_top_ + static_cast<int>(std::floor((y - roll_top()) / row_h_)));
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

void ClipEditor::step_note_on(int pitch, float vel) {
    if (!open_ || audio_) return;
    const double step = cell_ > 0 ? cell_ : 0.25;
    if (step_held_ == 0) push_undo();                     // one undo entry per chord/step
    const double at = std::fmod(step_cursor_, std::max(step, length_));   // wrap within the clip
    add_note({ pitch, at, step, std::clamp(vel, 0.05f, 1.0f) }, /*select*/false);
    ++step_held_;
}
void ClipEditor::step_note_off() {
    if (!open_ || audio_) return;
    if (step_held_ > 0 && --step_held_ == 0) {            // chord released -> advance one cell
        const double step = cell_ > 0 ? cell_ : 0.25;
        step_cursor_ += step;
    }
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
    const double visB = roll_w() / beat_px_;
    view_beat0_ = std::clamp(view_beat0_, 0.0, std::max(0.0, length_ - visB * 0.15));
    const int vis = std::max(1, static_cast<int>(roll_h() / row_h_));
    view_row_top_ = std::clamp(view_row_top_, 0, std::max(0, nrows() - vis));
}

void ClipEditor::fit_view() {
    if (fold_) {                                       // folded: frame all occupied rows
        rebuild_fold();
        row_h_ = std::clamp(roll_h() / static_cast<float>(nrows()), 5.f, 44.f);
        view_row_top_ = 0;
    } else {
        int lo = 127, hi = 0;
        for (const auto& nn : notes_) { lo = std::min(lo, nn.pitch); hi = std::max(hi, nn.pitch); }
        if (lo > hi) { lo = 54; hi = 78; }             // empty clip: center on the middle
        lo = std::max(0, lo - 2); hi = std::min(127, hi + 2);
        const int rows = std::max(1, hi - lo + 1);
        row_h_ = std::clamp(roll_h() / static_cast<float>(rows), 5.f, 44.f);
        view_row_top_ = row_of_pitch(hi);              // top row = highest note
    }
    beat_px_ = std::clamp(roll_w() / static_cast<float>(length_ > 0 ? length_ : 4.0), 8.f, 600.f);
    view_beat0_ = 0.0;
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
    step_cursor_ = 0.0; step_held_ = 0;   // reset step input for the new clip
    ghost_notes_.clear();                 // caller repopulates via set_ghost_notes
    tool_ = Tool::Draw;
    grid_idx_ = std::clamp(grid_idx_, 0, kNumGrids - 1);
    cell_ = kGrids[grid_idx_].v;
    docked_ = true;             // open in the shared bottom inspector dock (float via header toggle)
    open_ = true;
    fit_view();
}

void ClipEditor::open_audio(int track, int scene, const std::string& title,
                            const float* bins, int n, float t0, float t1, double loop_beats) {
    track_ = track; scene_ = scene; title_ = title;
    wave_.assign(bins, bins + (n > 0 ? n : 0));
    t0_ = t0; t1_ = t1;
    aud_loop_ = loop_beats > 0 ? loop_beats : 4.0;
    wav_x0_ = 0.0; wav_px_ = gw() > 0 ? gw() : 600.f; wav_amp_ = 1.f;   // fit the whole clip
    warp_norm_.clear(); warp_b_.clear(); trans_norm_.clear(); slice_norm_.clear();   // loaded via setters
    slice_mode_ = 0; marker_drag_ = -1; aud_req_ = 0;
    drag_ = 0; dirty_ = false;
    audio_ = true;
    docked_ = true;
    open_ = true;
}

// Keep the audio waveform view in range: zoom bounded, and the visible window inside [0,1].
void ClipEditor::clamp_wav_view() {
    const float fit = gw() > 0 ? gw() : 600.f;
    wav_px_ = std::clamp(wav_px_, fit, fit * 400.f);   // never zoom out past the whole clip
    wav_amp_ = std::clamp(wav_amp_, 0.25f, 16.f);
    const double vis = gw() / wav_px_;                 // visible fraction of the clip
    wav_x0_ = std::clamp(wav_x0_, 0.0, std::max(0.0, 1.0 - vis));
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
    // Header: close [X], dock toggle, per-mode controls, or drag-to-move.
    if (y < py + kHeaderH) {
        if (x >= px + pw - 28.f) { close(); return true; }                              // [X]
        if (x >= px + pw - 64.f) { docked_ = !docked_; drag_ = 0; return true; }         // dock
        if (audio_) {
            if (x >= px + pw - 358.f && x < px + pw - 274.f) {   // slice: cycle off->tran->grid
                slice_mode_ = slice_mode_ == 0 ? 1 : slice_mode_ == 1 ? 3 : 0; aud_req_ |= 8; return true;
            }
            if (x >= px + pw - 268.f && x < px + pw - 186.f) {   // warp: cycle off->cplx->beat->rept
                aud_warp_mode_ = aud_warp_mode_ >= 2 ? -1 : aud_warp_mode_ + 1; aud_req_ |= 1; return true;
            }
            if (x >= px + pw - 186.f && x < px + pw - 140.f) { aud_req_ |= 2; return true; }   // auto-warp
            if (x >= px + pw - 140.f && x < px + pw - 70.f) {    // pitch -/+ (left half / right half)
                aud_pitch_ = std::clamp(aud_pitch_ + (x < px + pw - 105.f ? -1.f : 1.f), -24.f, 24.f);
                if (aud_warp_mode_ < 0) aud_warp_mode_ = 0;      // pitch implies warp on (Complex)
                aud_req_ |= 1; return true;
            }
        } else {
            if (x >= px + pw - 124.f) { tool_ = tool_ == Tool::Select ? Tool::Draw : Tool::Select; return true; }
            if (x >= px + pw - 214.f) {   // scale (shift = scale type)
                if (shift) scale_type_ = (scale_type_ + 1) % kNumScales;
                else       scale_root_ = (scale_root_ + 2) % 13 - 1;
                return true;
            }
            if (x >= px + pw - 296.f) {   // step-input toggle (resets the cursor to the top)
                step_mode_ = !step_mode_; step_cursor_ = 0.0; step_held_ = 0; return true;
            }
            if (x >= px + pw - 366.f) {   // fold: show only occupied pitch rows
                fold_ = !fold_; rebuild_fold(); fit_view(); return true;
            }
            if (x >= px + pw - 436.f) { ghost_ = !ghost_; return true; }   // ghost reference notes
        }
        if (!docked_) { drag_ = 3; down_off_x_ = x - px_; down_off_y_ = y - py_; }       // start move
        return true;
    }
    // Content area.
    if (x >= gx() && x < gx() + gw() && y >= gy() && y < gy() + gh()) {
        if (!audio_) {   // scrollbars take priority over roll interaction
            float tx, ty, tw, th, t0, tl;
            if (vscroll_geom(tx, ty, tw, th, t0, tl) && x >= tx - 2.f && y >= ty && y < ty + th) {
                drag_ = 20; on_move(x, y); return true;
            }
            if (hscroll_geom(tx, ty, tw, th, t0, tl) && y >= ty - 2.f && y < ty + th + 2.f && x >= tx && x < tx + tw) {
                drag_ = 21; on_move(x, y); return true;
            }
        }
        if (audio_) {
            // Warp markers: shift-click a marker deletes it; plain click drags it; shift-click
            // empty adds a marker (beat interpolated so it lands on the existing warp line).
            int hit = -1; float bestd = 7.f;
            for (size_t i = 0; i < warp_norm_.size(); ++i) {
                const float d = std::fabs(x - wxn(warp_norm_[i]));
                if (d < bestd) { bestd = d; hit = static_cast<int>(i); }
            }
            if (hit >= 0) {
                if (shift) { warp_norm_.erase(warp_norm_.begin() + hit); warp_b_.erase(warp_b_.begin() + hit); aud_req_ |= 4; }
                else { marker_drag_ = hit; drag_ = 13; }
                return true;
            }
            const float hx0 = wxn(t0_), hx1 = wxn(t1_);
            if (std::fabs(x - hx0) <= 8.0) { drag_ = 10; return true; }
            if (std::fabs(x - hx1) <= 8.0) { drag_ = 11; return true; }
            if (shift) {   // add a marker at the click, beat interpolated between neighbors
                const double pn = std::clamp(wnorm_at(x), 0.0, 1.0);
                double beat = 0.0;
                if (warp_norm_.size() >= 2) {
                    size_t j = 0; while (j < warp_norm_.size() && warp_norm_[j] < pn) ++j;
                    if (j == 0) beat = warp_b_.front();
                    else if (j >= warp_norm_.size()) beat = warp_b_.back();
                    else { const double f = (pn - warp_norm_[j-1]) / std::max(1e-6, static_cast<double>(warp_norm_[j] - warp_norm_[j-1]));
                           beat = warp_b_[j-1] + f * (warp_b_[j] - warp_b_[j-1]); }
                }
                warp_norm_.push_back(static_cast<float>(pn)); warp_b_.push_back(beat); aud_req_ |= 4;
                return true;
            }
            drag_ = 12; down_beat_ = wnorm_at(x);   // pan the view
            return true;
        }
        // Piano-keyboard sidebar: press a key to audition the edited track's instrument.
        if (x < roll_x0() && y >= roll_top() && y < lane_top()) {
            const int p = pitch_at(y);
            if (p >= 0 && p <= 127 && audition_cb_) { audition_pitch_ = p; drag_ = 22; audition_cb_(track_, p, 0.85f, true); }
            return true;
        }
        // Bottom lane: velocity bars (lane_axis_ < 0) or a painted expression curve.
        if (y >= lane_top()) {
            if (lane_axis_ < 0) {                             // velocity: drag the nearest note's bar
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
            } else {                                          // expression: paint into the note under x
                paint_note_ = -1;
                for (size_t i = 0; i < notes_.size(); ++i) {
                    const float nx0 = xb(notes_[i].start), nx1 = xb(notes_[i].start + notes_[i].dur);
                    if (x >= nx0 - 2.f && x <= nx1 + 2.f) { paint_note_ = static_cast<int>(i); break; }
                }
                if (paint_note_ >= 0) {
                    push_undo();
                    if (!sel_[paint_note_]) { clear_sel(); sel_[paint_note_] = 1; }
                    paint_.clear();
                    paint_.push_back({ lane_t_at(x), lane_value_at(y) });
                    drag_ = 6;
                }
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
    if (drag_ == 20) {  // vertical scrollbar: map the cursor to a top row
        const float top = roll_top(), trackH = lane_top() - top;
        const float visRows = trackH / row_h_;
        const int   scrollRows = std::max(1, nrows() - static_cast<int>(visRows));
        const float f = std::clamp((static_cast<float>(y) - top) / std::max(1.f, trackH), 0.f, 1.f);
        view_row_top_ = static_cast<int>(std::lround(f * scrollRows));
        clamp_view();
        return;
    }
    if (drag_ == 21) {  // horizontal scrollbar
        const double visBeats = roll_w() / beat_px_;
        const float f = std::clamp((static_cast<float>(x) - roll_x0()) / std::max(1.f, roll_w()), 0.f, 1.f);
        view_beat0_ = f * std::max(0.0, length_ - visBeats);
        clamp_view();
        return;
    }
    if (drag_ == 3) {  // move floating panel
        px_ = std::clamp(static_cast<float>(x - down_off_x_), -kFloatW + 80.f, win_w_ - 80.f);
        py_ = std::clamp(static_cast<float>(y - down_off_y_), 44.f, win_h_ - kHeaderH - 4.f);
        return;
    }
    if (drag_ == 10 || drag_ == 11) {  // audio trim handles (in the zoomed view)
        const float f = std::clamp(static_cast<float>(wnorm_at(x)), 0.f, 1.f);
        if (drag_ == 10) t0_ = std::min(f, t1_ - 0.02f);
        else             t1_ = std::max(f, t0_ + 0.02f);
        t0_ = std::clamp(t0_, 0.f, 1.f); t1_ = std::clamp(t1_, 0.f, 1.f);
        dirty_ = true;
        return;
    }
    if (drag_ == 12) {  // pan the waveform view so the grabbed position stays under the cursor
        wav_x0_ = down_beat_ - (x - gx()) / wav_px_;
        clamp_wav_view();
        return;
    }
    if (drag_ == 13) {  // drag a warp marker to a new source position (its beat stays fixed)
        if (marker_drag_ >= 0 && marker_drag_ < static_cast<int>(warp_norm_.size()))
            warp_norm_[marker_drag_] = std::clamp(static_cast<float>(wnorm_at(x)), 0.f, 1.f);
        return;
    }
    if (drag_ == 4) { marq_x_ = x; marq_y_ = y; return; }   // marquee: finalized on mouse-up
    if (drag_ == 6) {                                        // expression paint stroke
        paint_.push_back({ lane_t_at(x), lane_value_at(y) });
        return;
    }
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
    if (drag_ == 6) finish_paint();
    if (drag_ == 13) { aud_req_ |= 4; marker_drag_ = -1; }   // commit the dragged warp marker
    if (drag_ == 22 && audition_pitch_ >= 0) {               // release the auditioned key
        if (audition_cb_) audition_cb_(track_, audition_pitch_, 0.f, false);
        audition_pitch_ = -1;
    }
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

// The expression lane occupies [gy()+roll_h() , gy()+gh()]; a small inset keeps the
// curve off the edges. Bend maps ±kBendRange to the full height (center = 0); pressure
// and timbre map 0..1 bottom-to-top.
float ClipEditor::lane_value_at(double y) const {
    const float top = lane_top() + 6.f, bot = gy() + gh() - 4.f;
    float f = (bot - static_cast<float>(y)) / std::max(1.f, bot - top);   // 0 bottom .. 1 top
    f = std::clamp(f, 0.f, 1.f);
    if (lane_axis_ == 0) { float v = (f * 2.f - 1.f) * kBendRange; return bend_snap_ ? std::round(v) : v; }
    return f;
}
float ClipEditor::lane_y_for(float v) const {
    const float top = lane_top() + 6.f, bot = gy() + gh() - 4.f;
    float f = (lane_axis_ == 0) ? (v / kBendRange + 1.f) * 0.5f : v;
    f = std::clamp(f, 0.f, 1.f);
    return bot - f * (bot - top);
}
float ClipEditor::lane_t_at(double x) const {
    if (paint_note_ < 0 || paint_note_ >= static_cast<int>(notes_.size())) return 0.f;
    const auto& n = notes_[paint_note_];
    return std::clamp(static_cast<float>((beat_at(x) - n.start) / std::max(1e-6, n.dur)), 0.f, 1.f);
}
// Commit the freehand stroke into the painted note's axis curve: a tiny tap clears it
// (erase), otherwise the raw points are decimated (RDP) into breakpoints.
void ClipEditor::finish_paint() {
    if (paint_note_ < 0 || paint_note_ >= static_cast<int>(notes_.size()) || lane_axis_ < 0) return;
    auto& curve = notes_[paint_note_].expr[lane_axis_];
    float tmin = 1e9f, tmax = -1e9f;
    for (const auto& p : paint_) { tmin = std::min(tmin, p.t); tmax = std::max(tmax, p.t); }
    if (paint_.empty() || (tmax - tmin) < 0.03f) {
        curve.bp.clear();                                     // tap (near-zero time span) = erase
    } else {
        const float eps = (lane_axis_ == 0) ? 0.3f : 0.03f;   // semitone vs 0..1 tolerance
        curve.bp = vivid::session::decimate_curve(paint_, eps);
    }
    paint_.clear(); paint_note_ = -1; dirty_ = true;
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
    if (!open_) return;
    if (audio_) {   // waveform: Cmd = h-zoom (cursor-anchored), Alt = amplitude, else pan
        const bool cmd = (mods & GLFW_MOD_SUPER) != 0, alt = (mods & GLFW_MOD_ALT) != 0;
        if (cmd) {
            const double anchor = wnorm_at(mx);
            wav_px_ *= std::pow(1.15f, static_cast<float>(yoff));
            clamp_wav_view();
            wav_x0_ = anchor - (mx - gx()) / wav_px_;
        } else if (alt) {
            wav_amp_ *= std::pow(1.15f, static_cast<float>(yoff));
        } else {
            const double amt = std::fabs(xoff) > 1e-3 ? xoff : -yoff;
            wav_x0_ += amt * 0.03 * (gw() / wav_px_);   // pan by a fraction of the visible span
        }
        clamp_wav_view();
        return;
    }
    const bool cmd = (mods & GLFW_MOD_SUPER) != 0;
    const bool alt = (mods & GLFW_MOD_ALT) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    if (cmd) {                                              // horizontal zoom around cursor
        const double anchor = beat_at(mx);
        beat_px_ *= std::pow(1.15f, static_cast<float>(yoff));
        clamp_view();
        view_beat0_ = anchor - (mx - gx()) / beat_px_;
    } else if (alt) {                                       // vertical zoom around cursor
        const double anchor = view_row_top_ + (my - roll_top()) / row_h_;   // row under the cursor
        row_h_ *= std::pow(1.15f, static_cast<float>(yoff));
        clamp_view();
        view_row_top_ = static_cast<int>(std::lround(anchor - (my - roll_top()) / row_h_));
    } else if (shift || std::fabs(xoff) > 1e-3) {          // horizontal pan
        const double amt = std::fabs(xoff) > 1e-3 ? xoff : -yoff;
        view_beat0_ += amt * 0.5;
    } else {                                                // vertical pan (rows)
        view_row_top_ += (yoff > 0 ? -2 : 2);              // scroll up = earlier rows (higher pitch)
    }
    clamp_view();
}

bool ClipEditor::on_key(int key, int mods) {
    if (!open_) return false;
    if (audio_) {   // waveform: F fits the whole clip
        if (key == GLFW_KEY_F) { wav_x0_ = 0.0; wav_px_ = gw() > 0 ? gw() : 600.f; wav_amp_ = 1.f; return true; }
        return false;
    }
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
    if (key == GLFW_KEY_E) { lane_axis_ = lane_axis_ >= 2 ? -1 : lane_axis_ + 1; return true; }  // lane: vel/bend/pres/timbre
    if (key == GLFW_KEY_J) { bend_snap_ = !bend_snap_; return true; }                            // semitone-snap painted bend
    if (key == GLFW_KEY_F) { fit_view(); return true; }
    if (key == GLFW_KEY_L) { follow_ = !follow_; return true; }
    if (key == GLFW_KEY_K) {                                 // K cycles scale root, Shift+K the type
        if (shift) scale_type_ = (scale_type_ + 1) % kNumScales;
        else       scale_root_ = (scale_root_ + 2) % 13 - 1;
        return true;
    }
    // --- M5 musical tools (operate on the selection, or the whole clip if none) ---
    namespace nt = vivid::session;
    if (key == GLFW_KEY_I) { push_undo(); nt::invert_pitches(notes_, sel_); dirty_ = true; return true; }
    if (key == GLFW_KEY_R) { push_undo(); nt::retrograde(notes_, sel_); dirty_ = true; return true; }
    if (key == GLFW_KEY_H) { push_undo(); nt::humanize(notes_, sel_, cell_ * 0.15, 0.12f, ++tool_seed_); dirty_ = true; return true; }
    if (key == GLFW_KEY_T) { push_undo(); nt::strum(notes_, sel_, cell_ * 0.5); dirty_ = true; return true; }
    if (key == GLFW_KEY_Y) {   // quantize pitches to the editor's scale (C major if scale is off)
        const int root = scale_root_ >= 0 ? scale_root_ : 0;
        const uint16_t mask = scale_root_ >= 0 ? kScales[scale_type_].mask : kScales[0].mask;
        push_undo(); nt::quantize_to_scale(notes_, sel_, root, mask); dirty_ = true; return true;
    }
    if (key == GLFW_KEY_APOSTROPHE) {   // glide: bend each note in from the previous pitch
        push_undo(); nt::apply_glide(notes_, sel_, 0.35f, kBendRange); dirty_ = true; return true;
    }
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
        char sc[16];
        if (scale_root_ < 0) std::snprintf(sc, sizeof sc, "scale off");
        else std::snprintf(sc, sizeof sc, "%s %s", kPitchNames[scale_root_], kScales[scale_type_].label);
        r.draw_text(px + pw - 430.f, py + 8.f, "ghost",
                    ghost_ ? 0.72f : 0.5f, ghost_ ? 0.74f : 0.55f, ghost_ ? 0.82f : 0.6f, 1.0f, 0.82f);
        r.draw_text(px + pw - 360.f, py + 8.f, "fold",
                    fold_ ? 0.55f : 0.5f, fold_ ? 0.82f : 0.55f, fold_ ? 0.85f : 0.6f, 1.0f, 0.82f);
        r.draw_text(px + pw - 290.f, py + 8.f, "step",
                    step_mode_ ? 0.95f : 0.5f, step_mode_ ? 0.6f : 0.55f, step_mode_ ? 0.28f : 0.6f, 1.0f, 0.82f);
        r.draw_text(px + pw - 210.f, py + 8.f, sc, 0.55f, 0.78f, 0.6f, 1.0f, 0.8f);   // scale (click cycles)
        r.draw_text(px + pw - 120.f, py + 8.f, draw ? "Draw" : "Select",
                    draw ? 0.55f : 0.6f, draw ? 0.82f : 0.72f, draw ? 0.55f : 0.85f, 1.0f, 0.82f);
    } else {
        static const char* sm[] = { "slice off", "slice tran", "", "slice grid" };
        r.draw_text(px + pw - 358.f, py + 8.f, sm[slice_mode_], 0.5f, 0.6f, 0.9f, 1.0f, 0.8f);
        static const char* wm[] = { "off", "cplx", "beat", "rept" };
        char wl[20]; std::snprintf(wl, sizeof wl, "warp %s", wm[aud_warp_mode_ + 1]);
        const bool on = aud_warp_mode_ >= 0;
        r.draw_text(px + pw - 268.f, py + 8.f, wl, 0.55f, on ? 0.82f : 0.6f, on ? 0.55f : 0.62f, 1.0f, 0.82f);
        r.draw_text(px + pw - 182.f, py + 8.f, "auto", 0.62f, 0.72f, 0.9f, 1.0f, 0.82f);
        char pl[16]; std::snprintf(pl, sizeof pl, "pit %+d", static_cast<int>(std::lround(aud_pitch_)));
        r.draw_text(px + pw - 132.f, py + 8.f, pl, 0.6f, 0.72f, 0.78f, 1.0f, 0.82f);
    }
    r.draw_text(px + pw - 60.f, py + 8.f, docked_ ? "float" : "dock", 0.6f, 0.72f, 0.78f, 1.0f, 0.8f);
    r.draw_text(px + pw - 22.f, py + 8.f, "X", 0.8f, 0.55f, 0.55f, 1.0f, 1.0f);

    const float GX = roll_x0(), GY = gy(), GW = roll_w(), GH = gh();   // roll content (inset past the keys, MIDI)
    r.draw_rect(gx(), GY, gw(), GH, 0.07f, 0.08f, 0.10f, 1.0f);
    r.push_clip_rect(GX, GY, GW, GH);

    if (audio_) {
        const int n = static_cast<int>(wave_.size());
        const float midY = GY + GH * 0.5f;
        const float x0 = wxn(t0_), x1 = wxn(t1_);   // trim-handle screen x (in the zoomed view)
        // Only draw bins whose normalized position is in the visible window.
        const double vLo = wnorm_at(GX), vHi = wnorm_at(GX + GW);
        const float binw = std::max(1.f, wav_px_ / std::max(1, n));
        for (int i = 0; i < n; ++i) {
            const double np = static_cast<double>(i) / n;
            if (np < vLo - 0.01 || np > vHi + 0.01) continue;
            const float wx = wxn(np);
            const float h = std::min(wave_[i] * GH * 0.46f * wav_amp_, GH * 0.49f);
            const bool in = (np >= t0_ && np < t1_);
            r.draw_rect(wx, midY - h, binw, h * 2.f,
                        in ? 0.32f : 0.16f, in ? 0.72f : 0.26f, in ? 0.78f : 0.30f, 1.0f);
        }
        r.draw_rect(GX, midY, GW, 1.f, 0.18f, 0.20f, 0.24f, 1.0f);          // center line
        if (x0 > GX) r.draw_rect(GX, GY, std::min(x0, GX + GW) - GX, GH, 0.f, 0.f, 0.f, 0.45f);  // dim outside loop
        if (x1 < GX + GW) r.draw_rect(std::max(x1, GX), GY, GX + GW - std::max(x1, GX), GH, 0.f, 0.f, 0.f, 0.45f);
        if (x0 >= GX && x0 <= GX + GW) r.draw_rect(x0 - 1.f, GY, 2.f, GH, 0.92f, 0.84f, 0.34f, 1.0f);  // trim handles
        if (x1 >= GX && x1 <= GX + GW) r.draw_rect(x1 - 1.f, GY, 2.f, GH, 0.92f, 0.84f, 0.34f, 1.0f);
        // Detected transients (faint ticks along the bottom).
        for (float tn : trans_norm_) { const float tx = wxn(tn); if (tx >= GX && tx < GX + GW) r.draw_rect(tx, GY + GH - 9.f, 1.f, 8.f, 0.5f, 0.5f, 0.36f, 0.7f); }
        // Slice boundaries (A6): blue dividers (drawn under the warp markers).
        for (float sn : slice_norm_) { const float sx = wxn(sn); if (sx >= GX && sx < GX + GW) r.draw_rect(sx, GY, 1.f, GH, 0.4f, 0.6f, 0.95f, 0.5f); }
        // Warp markers: orange lines with a grab tab (click to drag, shift-click to delete).
        for (float wn : warp_norm_) {
            const float wx = wxn(wn);
            if (wx < GX || wx > GX + GW) continue;
            r.draw_rect(wx, GY, 1.f, GH, 0.96f, 0.62f, 0.24f, 0.85f);
            r.draw_rect(wx - 3.f, GY, 7.f, 6.f, 0.98f, 0.72f, 0.3f, 1.0f);
        }
        // Playhead: the read position within the loop window, mapped back to the buffer.
        if (playhead_ >= 0.0 && aud_loop_ > 0.0) {
            double ph = std::fmod(playhead_, aud_loop_); if (ph < 0) ph += aud_loop_;
            const double rn = t0_ + (ph / aud_loop_) * (t1_ - t0_);
            const float x = wxn(rn);
            if (x >= GX && x < GX + GW) r.draw_rect(x, GY, 1.5f, GH, 0.95f, 0.35f, 0.35f, 1.0f);
        }
        r.pop_clip_rect();
        r.draw_text(px + 12.f, py + ph - 18.f,
                    "yellow handles = loop  \xC2\xB7  header: warp / auto / pit  \xC2\xB7  Cmd-scroll zoom  \xC2\xB7  \xE2\x8C\xA5 amp  \xC2\xB7  scroll pan  \xC2\xB7  F fit",
                    0.45f, 0.48f, 0.53f, 1.0f, 0.78f);
        return;
    }

    const float RH = row_h_;
    const float ROLL = roll_h();
    const int visRows = std::max(1, static_cast<int>(ROLL / RH) + 2);
    const int rTop = view_row_top_, rBot = std::min(nrows(), view_row_top_ + visRows);
    const float RTOP = roll_top(), RBOT = lane_top();   // piano-roll band (below ruler, above lane)
    for (int rr = rTop; rr < rBot; ++rr) {
        const int p = pitch_of_row(rr);
        if (p < 0 || p > 127) continue;
        const float y = roll_top() + (rr - view_row_top_) * RH;
        if (y >= RBOT || y + RH <= RTOP) continue;
        if (fold_) r.draw_rect(GX, y + RH - 1.f, GW, 1.f, 0.16f, 0.17f, 0.20f, 1.0f);   // row separators when folded
        if (is_black(p)) r.draw_rect(GX, y, GW, RH, 0.09f, 0.10f, 0.12f, 1.0f);
        if (scale_root_ >= 0) {
            const int pc = (((p - scale_root_) % 12) + 12) % 12;
            if (pc == 0)                              r.draw_rect(GX, y, GW, RH, 0.35f, 0.85f, 0.45f, 0.17f);
            else if (kScales[scale_type_].mask & (1u << pc)) r.draw_rect(GX, y, GW, RH, 0.35f, 0.85f, 0.45f, 0.07f);
        }
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
        r.draw_rect(x, RTOP, 1.f, RBOT - RTOP, whole ? 0.24f : 0.14f, whole ? 0.26f : 0.15f, whole ? 0.30f : 0.17f, 1.0f);
    }
    // Bars/beats ruler strip along the top.
    r.draw_rect(GX, GY, GW, ruler_h(), 0.13f, 0.14f, 0.17f, 1.0f);
    r.draw_rect(GX, GY + ruler_h() - 1.f, GW, 1.f, 0.22f, 0.24f, 0.28f, 1.0f);
    for (double b = std::ceil(bLeft); b <= bRight + 1e-6; b += 1.0) {
        if (b < 0) continue;
        const float x = xb(b);
        const bool bar = std::fmod(b, 4.0) < 1e-6;   // 4 beats/bar
        r.draw_rect(x, GY + (bar ? 3.f : 7.f), 1.f, ruler_h() - (bar ? 3.f : 7.f), 0.4f, 0.43f, 0.48f, 1.0f);
        if (bar) {
            char lbl[8]; std::snprintf(lbl, sizeof lbl, "%d", static_cast<int>(b / 4.0) + 1);
            r.draw_text(x + 3.f, GY + 3.f, lbl, 0.6f, 0.63f, 0.68f, 1.0f, 0.72f);
        }
    }
    // Ghost notes (other tracks' same-scene notes), drawn faintly behind the editable ones.
    if (ghost_ && !fold_) {
        for (const auto& n : ghost_notes_) {
            const float nx = xb(n.start), ny = yp(n.pitch), nw = std::max(2.f, float(n.dur) * bw());
            if (ny >= RBOT || ny + RH <= RTOP || nx > GX + GW || nx + nw < GX) continue;
            r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.30f, 0.34f, 0.42f, 0.5f);
        }
    }
    // Notes.
    for (size_t i = 0; i < notes_.size(); ++i) {
        const auto& n = notes_[i];
        const float nx = xb(n.start), ny = yp(n.pitch), nw = std::max(2.f, float(n.dur) * bw());
        if (ny >= RBOT || ny + RH <= RTOP) continue;
        const float v = 0.4f + 0.5f * std::clamp(n.vel, 0.f, 1.f);
        if (sel_[i]) {
            r.draw_rect(nx - 1.f, ny, nw + 2.f, RH, 0.98f, 0.86f, 0.42f, 1.0f);   // selection halo
            r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.42f, 0.60f, 0.95f, 1.0f);
        } else {
            r.draw_rect(nx, ny + 1.f, nw, RH - 2.f, 0.30f * v + 0.1f, 0.78f * v, 0.80f * v, 1.0f);
        }
        r.draw_rect(nx, ny + 1.f, 2.f, RH - 2.f, 0.6f, 0.92f, 0.9f, 1.0f);
    }
    // Bottom lane: velocity bars, or a painted per-note expression curve for one MPE axis.
    const float laneTop = lane_top(), laneBot = GY + GH, laneH = laneBot - laneTop - 6.f;
    r.draw_rect(GX, laneTop, GW, 1.f, 0.20f, 0.22f, 0.26f, 1.0f);
    r.draw_rect(GX, laneTop + 1.f, GW, laneBot - laneTop - 1.f, 0.05f, 0.055f, 0.07f, 1.0f);
    if (lane_axis_ < 0) {
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
    } else {
        char lbl[24]; std::snprintf(lbl, sizeof lbl, "%s%s", kAxisNames[lane_axis_], bend_snap_ && lane_axis_ == 0 ? " (snap)" : "");
        r.draw_text(GX + 2.f, laneTop + 3.f, lbl, 0.6f, 0.78f, 0.55f, 1.0f, 0.66f);
        if (lane_axis_ == 0) {  // bend: zero line
            const float yc = lane_y_for(0.f);
            r.draw_rect(GX, yc, GW, 1.f, 0.24f, 0.26f, 0.30f, 1.0f);
        }
        // Each note's curve, sampled across its span.
        for (size_t i = 0; i < notes_.size(); ++i) {
            const auto& n = notes_[i];
            const ExprCurve& c = n.expr[lane_axis_];
            if (c.empty()) continue;
            const float nx0 = xb(n.start), nx1 = xb(n.start + n.dur);
            if (nx1 < GX || nx0 > GX + GW) continue;
            const bool s = sel_[i] != 0;
            const int K = 28;
            float px = nx0, py = lane_y_for(c.sample(0.f));
            for (int k = 1; k <= K; ++k) {
                const float t = static_cast<float>(k) / K;
                const float cx = nx0 + t * (nx1 - nx0), cy = lane_y_for(c.sample(t));
                r.draw_line(px, py, cx, cy, s ? 2.0f : 1.4f,
                            s ? 0.98f : 0.45f, s ? 0.80f : 0.70f, s ? 0.35f : 0.55f, 1.0f);
                px = cx; py = cy;
            }
        }
        // Live stroke being painted.
        if (drag_ == 6 && paint_.size() > 1 && paint_note_ >= 0 && paint_note_ < static_cast<int>(notes_.size())) {
            const auto& n = notes_[paint_note_];
            float px = xb(n.start + paint_[0].t * n.dur), py = lane_y_for(paint_[0].v);
            for (size_t k = 1; k < paint_.size(); ++k) {
                const float cx = xb(n.start + paint_[k].t * n.dur), cy = lane_y_for(paint_[k].v);
                r.draw_line(px, py, cx, cy, 1.6f, 1.0f, 0.9f, 0.4f, 0.9f);
                px = cx; py = cy;
            }
        }
    }
    // Playhead (spans roll + lane).
    if (playhead_ >= 0.0 && length_ > 0.0) {
        double p = std::fmod(playhead_, length_); if (p < 0) p += length_;
        const float x = xb(p);
        if (x >= GX && x < GX + GW) r.draw_rect(x, GY, 1.5f, GH, 0.95f, 0.35f, 0.35f, 1.0f);
    }
    // Scrollbars (drawn over the roll; hidden when the content fits).
    { float tx, ty, tw, th, t0, tl;
      if (vscroll_geom(tx, ty, tw, th, t0, tl)) {
          r.draw_rect(tx, ty, tw, th, 0.10f, 0.11f, 0.13f, 0.7f);
          r.draw_rounded_rect(tx + 1.f, t0, tw - 2.f, tl, 2.f, 0.42f, 0.45f, 0.52f, 0.9f);
      }
      if (hscroll_geom(tx, ty, tw, th, t0, tl)) {
          r.draw_rect(tx, ty, tw, th, 0.10f, 0.11f, 0.13f, 0.7f);
          r.draw_rounded_rect(t0, ty + 1.f, tl, th - 2.f, 2.f, 0.42f, 0.45f, 0.52f, 0.9f);
      } }
    // Step-input cursor: a gold vertical marking where the next note lands.
    if (step_mode_ && length_ > 0.0) {
        const float x = xb(std::fmod(step_cursor_, length_));
        if (x >= GX && x < GX + GW) r.draw_rect(x, GY, 1.5f, GH, 0.95f, 0.78f, 0.30f, 0.9f);
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

    // Piano-keyboard sidebar (left of the roll). Click a key to audition the edited track.
    {
        const float kx = gx(), kw = key_w(), ktop = roll_top(), kbot = lane_top();
        r.push_clip_rect(kx, ktop, kw, kbot - ktop);
        r.draw_rect(kx, ktop, kw, kbot - ktop, 0.12f, 0.13f, 0.15f, 1.0f);
        const int krows = std::max(1, static_cast<int>((kbot - ktop) / row_h_) + 2);
        for (int rr = view_row_top_; rr < std::min(nrows(), view_row_top_ + krows); ++rr) {
            const int p = pitch_of_row(rr);
            if (p < 0 || p > 127) continue;
            const float y = roll_top() + (rr - view_row_top_) * row_h_;
            const bool blk = is_black(p), lit = (p == audition_pitch_);
            const float cr = lit ? 0.55f : (blk ? 0.16f : 0.82f);
            const float cg = lit ? 0.78f : (blk ? 0.17f : 0.84f);
            const float cb = lit ? 0.95f : (blk ? 0.20f : 0.88f);
            r.draw_rect(kx + 1.f, y + 0.5f, kw - 2.f, std::max(1.f, row_h_ - 1.f), cr, cg, cb, 1.0f);
            if (p % 12 == 0) {
                char lbl[8]; std::snprintf(lbl, sizeof lbl, "C%d", p / 12 - 1);
                r.draw_text(kx + 3.f, y + row_h_ * 0.5f - 5.f, lbl, 0.25f, 0.27f, 0.30f, 1.0f, 0.66f);
            }
        }
        r.draw_rect(kx + kw - 1.f, ktop, 1.f, kbot - ktop, 0.22f, 0.24f, 0.28f, 1.0f);
        r.pop_clip_rect();
    }

    char foot[200];
    std::snprintf(foot, sizeof foot,
                  "%s \xC2\xB7 grid %s \xC2\xB7 %d sel \xC2\xB7 lane %s \xC2\xB7 E lane/J snap \xC2\xB7 tools: I invert R retro H human T strum Y scale ' glide \xC2\xB7 paint in lane; tap=erase",
                  tool_ == Tool::Draw ? "Draw" : "Select", kGrids[grid_idx_].label, selected_count(),
                  lane_axis_ < 0 ? "vel" : kAxisNames[lane_axis_]);
    r.draw_text(px + 12.f, py + ph - 18.f, foot, 0.45f, 0.48f, 0.53f, 1.0f, 0.76f);
}

}  // namespace vivid::ui
