#include "ui/clip_editor.h"
#include "ui/ui_style.h"
#include "ui/editor_controls.h"   // ADR-0048: the shared control substrate (icon_button, segmented, …)
#include "ui/waveform_view.h"     // ADR-0048/0049: the shared waveform component (one waveform language)
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
static constexpr float kEditorHeaderH = 30.f;

// ADR-0048: shared header-control rects — draw() and on_down() compute a control's bounds from the SAME
// helper, so the click target can never drift from what's drawn (the old code hand-authored mismatched
// x-ranges in each). More controls migrate to real widgets + a laid-out inspector strip in later slices.
static Rect close_btn_rect(float px, float py, float pw) { return { px + pw - 26.f, py + 6.f, 20.f, 18.f }; }
static Rect dock_btn_rect (float px, float py, float pw) { return { px + pw - 76.f, py + 6.f, 44.f, 18.f }; }
// Title-strip transport buttons, right side (left of the dock/close buttons).
static Rect fit_btn_rect  (float px, float py, float pw) { return { px + pw - 108.f, py + 6.f, 26.f, 18.f }; }
static Rect follow_btn_rect(float px, float py, float pw){ return { px + pw - 138.f, py + 6.f, 26.f, 18.f }; }

// ADR-0048: the MIDI inspector strip — real controls, packed left-to-right, one shared Rect each for
// draw + hit. `midi_insp()` is pure geometry (from the panel), so draw() and on_down() lay out identically.
struct MidiInsp { Rect tool, grid, fold, key, scale, ghost, step, lane, quant, xform; };
static MidiInsp midi_insp(float px, float py, float /*pw*/) {
    const float y = py + kEditorHeaderH + 5.f, h = 22.f;
    float x = px + 10.f;
    auto take = [&](float w) { Rect r{ x, y, w, h }; x += w + 6.f; return r; };
    auto gap  = [&]() { x += 8.f; };
    MidiInsp m;
    m.tool  = take(112.f); gap();      // Draw | Select
    m.grid  = take(116.f); gap();      // GRID stepper (wide enough for kicker + value)
    m.fold  = take(46.f);              // toggles
    m.ghost = take(52.f);
    m.step  = take(46.f);  gap();
    m.key   = take(64.f);              // KEY (scale root) dropdown
    m.scale = take(76.f);  gap();      // SCALE (type) dropdown
    m.lane  = take(96.f);  gap();      // velocity/expression lane
    m.quant = take(84.f);              // Quantize
    m.xform = take(104.f);             // ⋯ Transform menu
    return m;
}
// ADR-0048: the AUDIO inspector strip — real controls (warp / auto / pitch / slice / slice→MIDI), one
// shared Rect each for draw + hit, driving the same `aud_req_` commit bits the old header text did.
struct AudioInsp { Rect warp, autow, pitch, slice, to_midi; };
static AudioInsp audio_insp(float px, float py, float /*pw*/) {
    const float y = py + kEditorHeaderH + 5.f, h = 22.f;
    float x = px + 10.f;
    auto take = [&](float w) { Rect r{ x, y, w, h }; x += w + 6.f; return r; };
    auto gap  = [&]() { x += 8.f; };
    AudioInsp a;
    a.warp    = take(196.f); gap();    // Off | Cplx | Beat | Rept
    a.autow   = take(92.f);            // Auto-warp
    a.pitch   = take(104.f); gap();    // PITCH stepper
    a.slice   = take(150.f);           // Off | Tran | Grid
    a.to_midi = take(116.f);           // Slice → MIDI
    return a;
}

// The ⋯ Transform menu items — each replays the existing on_key handler (zero duplication).
struct XItem { const char* label; int key; };
static const XItem kXItems[] = {
    { "Quantize (\xE2\x8C\x98U)", GLFW_KEY_U },   // Cmd+U; item passes SUPER below
    { "Invert",       GLFW_KEY_I },
    { "Retrograde",   GLFW_KEY_R },
    { "Humanize",     GLFW_KEY_H },
    { "Strum",        GLFW_KEY_T },
    { "To scale",     GLFW_KEY_Y },
    { "Glide",        GLFW_KEY_APOSTROPHE },
    { "Velocity louder (>)", GLFW_KEY_PERIOD },   // scale the selection's velocities up / down (repeatable)
    { "Velocity softer (<)", GLFW_KEY_COMMA },
};
static constexpr int kNumXItems = static_cast<int>(sizeof(kXItems) / sizeof(kXItems[0]));
static constexpr float kXItemH = 22.f, kXMenuW = 150.f;
static Rect xform_item_rect(Rect xbtn, int i) { return { xbtn.x, xbtn.y + xbtn.h + 2.f + i * kXItemH, kXMenuW, kXItemH }; }
// MIDI-1: shared dropdown geometry + item labels for the Key / Scale / Lane pickers.
static Rect menu_item_rect(Rect anchor, int i) { return { anchor.x, anchor.y + anchor.h + 2.f + i * kXItemH, kXMenuW, kXItemH }; }
static const char* kKeyLabels[]  = { "Off", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };  // idx = scale_root_+1
static constexpr int kNumKeys = static_cast<int>(sizeof(kKeyLabels) / sizeof(kKeyLabels[0]));
static const char* kLaneLabels[] = { "Velocity", "Bend", "Pressure", "Timbre" };   // idx = lane_axis_+1
// Quantize is a live popover with continuous Amount (strength) + Swing sliders (leftover: was presets).
static constexpr float kQSwingMax = 0.6f;                 // swing slider maps 0 .. 0.6 of a grid cell
static Rect quant_pop_rect(Rect anchor) { return { anchor.x, anchor.y + anchor.h + 2.f, 208.f, 2.f * 30.f + 12.f }; }
static Rect quant_slider_rect(Rect anchor, int i) { return { anchor.x + 12.f, anchor.y + anchor.h + 10.f + i * 30.f, 184.f, 22.f }; }

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
    const double visB = roll_w() / beat_px_;          // visible beats in the note roll (not gw(), which includes the key sidebar)
    if (visB >= length_) return;                       // the whole clip fits -> never scroll (view stays put across the loop)
    // Paged follow: hold the view until the playhead leaves the current page, then jump to the page
    // that contains it. A loop wrap (p -> ~0) lands cleanly on page 0 instead of yanking the view
    // mid-clip.
    if (p < view_beat0_ || p >= view_beat0_ + visB) {
        view_beat0_ = std::floor(p / visB) * visB;
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
float ClipEditor::gy() const { float x,y,w,h; panel(x,y,w,h); return y + kEditorHeaderH + insp_h() + 8.f; }
float ClipEditor::gw() const { float x,y,w,h; panel(x,y,w,h); return w - 20.f; }
float ClipEditor::gh() const { float x,y,w,h; panel(x,y,w,h); return h - kEditorHeaderH - insp_h() - 18.f; }

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

void ClipEditor::save_view() {
    if (!open_ || audio_) return;
    view_mem_[{track_, scene_}] = ViewState{ view_beat0_, beat_px_, row_h_, view_row_top_ };
}

void ClipEditor::open(int track, int scene, const std::string& title,
                      const vivid::session::ClipNote* notes, int n, double length) {
    save_view();                          // remember the previously-open clip's view
    track_ = track; scene_ = scene; title_ = title;
    length_ = length > 0 ? length : 4.0;
    notes_.assign(notes, notes + (n > 0 ? n : 0));
    sel_.assign(notes_.size(), 0);
    undo_.clear(); redo_.clear();
    drag_ = 0; last_idx_ = -1; dirty_ = false; audio_ = false;
    step_cursor_ = 0.0; step_held_ = 0;   // reset step input for the new clip
    loop_start_ = loop_end_ = 0.0; loop_dirty_ = false;   // caller reloads via set_loop
    ghost_notes_.clear();                 // caller repopulates via set_ghost_notes
    tool_ = Tool::Draw;
    grid_idx_ = std::clamp(grid_idx_, 0, kNumGrids - 1);
    cell_ = kGrids[grid_idx_].v;
    docked_ = true;             // open in the shared bottom inspector dock (float via header toggle)
    open_ = true;
    if (fold_) rebuild_fold();            // fold is a global mode; refresh its rows for this clip
    auto it = view_mem_.find({track, scene});
    if (it != view_mem_.end()) {          // restore this clip's remembered zoom + scroll
        const ViewState& v = it->second;
        view_beat0_ = v.beat0; beat_px_ = v.beat_px; row_h_ = v.row_h; view_row_top_ = v.row_top;
        clamp_view();
    } else {
        fit_view();
    }
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
    const bool nosnap = (mods & GLFW_MOD_ALT) != 0;   // MIDI-4: Alt held at drag start = bypass grid snap
    float px, py, pw, ph; panel(px, py, pw, ph);
    // MIDI-1: an open inspector dropdown owns the next click — dispatch to the picked item, else close.
    if (!audio_ && (xform_open_ || key_open_ || scale_open_ || lane_open_ || quant_open_)) {
        const MidiInsp m = midi_insp(px, py, pw);
        auto pick = [&](Rect anchor, int n) { for (int i = 0; i < n; ++i) if (hit(menu_item_rect(anchor, i), x, y)) return i; return -1; };
        if (xform_open_) { const int i = pick(m.xform, kNumXItems);
            if (i >= 0) { const int k = kXItems[i].key; on_key(k, k == GLFW_KEY_U ? GLFW_MOD_SUPER : 0); } xform_open_ = false; if (i >= 0) return true; }
        else if (key_open_)   { const int i = pick(m.key, kNumKeys);   if (i >= 0) scale_root_ = i - 1;      key_open_ = false;   if (i >= 0) return true; }
        else if (scale_open_) { const int i = pick(m.scale, kNumScales); if (i >= 0) { scale_type_ = i; if (scale_root_ < 0) scale_root_ = 0; } scale_open_ = false; if (i >= 0) return true; }
        else if (lane_open_)  { const int i = pick(m.lane, 4);         if (i >= 0) lane_axis_ = i - 1;       lane_open_ = false;  if (i >= 0) return true; }
        else if (quant_open_) {   // grab a slider (live re-quantize), consume clicks inside, close outside
            if (hit(quant_slider_rect(m.quant, 0), x, y)) { drag_ = 30; on_move(x, y); return true; }   // Amount
            if (hit(quant_slider_rect(m.quant, 1), x, y)) { drag_ = 31; on_move(x, y); return true; }   // Swing
            if (hit(quant_pop_rect(m.quant), x, y)) return true;
            quant_open_ = false;
        }
        // a click elsewhere just closed the menu; fall through to normal handling
    }
    // Title strip: close / dock (both modes), MIDI follow/fit, audio per-mode controls, or drag-to-move.
    if (y < py + kEditorHeaderH) {
        // ADR-0048: hit-test the SAME rects the controls are drawn from (no magic-offset drift).
        if (hit(close_btn_rect(px, py, pw), x, y)) { close(); return true; }             // [✕]
        if (hit(dock_btn_rect(px, py, pw), x, y)) { docked_ = !docked_; drag_ = 0; return true; }  // dock
        if (hit(follow_btn_rect(px, py, pw), x, y)) { follow_ = !follow_; return true; }
        if (hit(fit_btn_rect(px, py, pw), x, y)) { fit_view(); return true; }
        if (!docked_) { drag_ = 3; down_off_x_ = x - px_; down_off_y_ = y - py_; }       // start move
        return true;
    }
    // ADR-0048: MIDI inspector strip — hit the SAME midi_insp() rects the controls are drawn from.
    if (!audio_ && y < py + kEditorHeaderH + insp_h()) {
        const MidiInsp m = midi_insp(px, py, pw);
        if (int c = segmented_hit(m.tool, 2, x, y); c >= 0) { tool_ = c == 0 ? Tool::Draw : Tool::Select; return true; }
        if (int s = stepper_hit(m.grid, x, y)) {
            grid_idx_ = (grid_idx_ + (s > 0 ? 1 : kNumGrids - 1)) % kNumGrids; cell_ = kGrids[grid_idx_].v; return true;
        }
        if (hit(m.fold, x, y))  { fold_ = !fold_; rebuild_fold(); fit_view(); return true; }
        if (hit(m.ghost, x, y)) { ghost_ = !ghost_; return true; }
        if (hit(m.step, x, y))  { step_mode_ = !step_mode_; step_cursor_ = 0.0; step_held_ = 0; return true; }
        // MIDI-1: Key / Scale / Lane open a real pick-list (mutually exclusive); clicking an open one closes it.
        auto toggle = [&](bool& m1) { const bool was = m1; key_open_ = scale_open_ = lane_open_ = quant_open_ = xform_open_ = false; m1 = !was; };
        if (hit(m.key, x, y))   { toggle(key_open_);   return true; }
        if (hit(m.scale, x, y)) { toggle(scale_open_); return true; }
        if (hit(m.lane, x, y))  { toggle(lane_open_);  return true; }
        if (hit(m.quant, x, y)) { const bool was = quant_open_; toggle(quant_open_);   // Amount + Swing sliders
            if (quant_open_ && !was) { push_undo(); quant_snap_ = notes_; } return true; }
        if (hit(m.xform, x, y)) { toggle(xform_open_); return true; }
        return true;   // a click anywhere in the inspector strip is consumed (no drag-through to the roll)
    }
    // ADR-0048: AUDIO inspector strip — hit the SAME audio_insp() rects; drive the aud_req_ commit bits.
    if (audio_ && y < py + kEditorHeaderH + insp_h()) {
        const AudioInsp a = audio_insp(px, py, pw);
        if (int c = segmented_hit(a.warp, 4, x, y); c >= 0) { aud_warp_mode_ = c - 1; aud_req_ |= 1; return true; }  // Off/Cplx/Beat/Rept
        if (hit(a.autow, x, y)) { aud_req_ |= 2; return true; }                                                     // auto-warp
        if (int s = stepper_hit(a.pitch, x, y)) {
            aud_pitch_ = std::clamp(aud_pitch_ + (s > 0 ? 1.f : -1.f), -24.f, 24.f);
            if (aud_warp_mode_ < 0) aud_warp_mode_ = 0;    // pitch implies warp on (Complex)
            aud_req_ |= 1; return true;
        }
        if (int c = segmented_hit(a.slice, 3, x, y); c >= 0) { slice_mode_ = c == 0 ? 0 : c == 1 ? 1 : 3; aud_req_ |= 8; return true; }
        if (hit(a.to_midi, x, y)) { aud_req_ |= 16; return true; }   // slice -> Sampler-driven MIDI track
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
        // Ruler: drag out an in-clip loop region (release without dragging clears it).
        if (y < roll_top() && x >= roll_x0()) {
            down_beat_ = std::clamp(snap(beat_at(x)), 0.0, length_);
            loop_start_ = loop_end_ = down_beat_; drag_ = 23;
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
            if (lane_axis_ < 0) {                             // velocity: drag a ramp; Shift = scale the dynamics
                push_undo();
                drag_ = 5; vel_x0_ = x; vel_y0_ = y; vel_scale_ = shift;
                if (vel_scale_) drag_orig_ = notes_;          // snapshot velocities for proportional scaling
                apply_vel_ramp(x, y);
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
            drag_ = re ? 2 : 1; drag_nosnap_ = nosnap;
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
    if (drag_ == 23) {  // dragging out the in-clip loop region
        const double b = std::clamp(snap(beat_at(x)), 0.0, length_);
        loop_start_ = std::min(down_beat_, b);
        loop_end_   = std::max(down_beat_, b);
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
        py_ = std::clamp(static_cast<float>(y - down_off_y_), 44.f, win_h_ - kEditorHeaderH - 4.f);
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
    if (drag_ == 5) { apply_vel_ramp(x, y); return; }        // velocity ramp-drag
    if (drag_ == 30 || drag_ == 31) {                        // quantize Amount/Swing slider — live re-quantize
        float px, py, pw, ph; panel(px, py, pw, ph);
        const MidiInsp m = midi_insp(px, py, pw);
        const Rect sr = quant_slider_rect(m.quant, drag_ == 30 ? 0 : 1);
        const float v01 = std::clamp(static_cast<float>((x - sr.x) / std::max(1.0, static_cast<double>(sr.w))), 0.f, 1.f);
        if (drag_ == 30) quant_amount_ = v01; else quant_swing_ = v01 * kQSwingMax;
        notes_ = quant_snap_;
        vivid::session::quantize_swing(notes_, sel_, cell_, quant_amount_, quant_swing_);
        dirty_ = true;
        return;
    }
    if (drag_ != 1 && drag_ != 2) return;
    const double dbeat = beat_at(x) - down_beat_;
    const int    dpitch = pitch_at(y) - down_pitch_;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (!sel_[i] || i >= drag_orig_.size()) continue;
        const auto& o = drag_orig_[i];
        auto maybe_snap = [&](double b) { return drag_nosnap_ ? b : snap(b); };   // Alt held = fine (no grid)
        if (drag_ == 1) {                                   // move
            double ns = maybe_snap(o.start + dbeat);
            notes_[i].start = std::clamp(ns, 0.0, std::max(0.0, length_ - o.dur));
            notes_[i].pitch = std::clamp(o.pitch + dpitch, 0, 127);
        } else {                                            // resize
            double nd = maybe_snap(o.dur + dbeat);
            notes_[i].dur = std::clamp(nd, drag_nosnap_ ? 0.02 : cell_, length_ - notes_[i].start);
        }
    }
    dirty_ = true;
}

void ClipEditor::on_up(double x, double y) {
    if (drag_ == 4) finish_marquee(x, y);
    if (drag_ == 6) finish_paint();
    if (drag_ == 13) { aud_req_ |= 4; marker_drag_ = -1; }   // commit the dragged warp marker
    if (drag_ == 23) {                                        // finish the loop-region drag
        if (loop_end_ - loop_start_ < 1e-6) { loop_start_ = loop_end_ = 0.0; }   // no drag -> clear
        loop_dirty_ = true;
    }
    if (drag_ == 22 && audition_pitch_ >= 0) {               // release the auditioned key
        if (audition_cb_) audition_cb_(track_, audition_pitch_, 0.f, false);
        audition_pitch_ = -1;
    }
    drag_ = 0; lane_idx_ = -1; vel_scale_ = false;
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
// MIDI-3: draw a velocity line across the notes the drag spans — start note gets the start velocity,
// end note the end velocity, everything between interpolates. A near-vertical drag (or a click) just
// sets the note under the cursor, so the single-note case still works.
void ClipEditor::apply_vel_ramp(double x1, double y1) {
    if (notes_.empty()) return;
    const float top = lane_top() + 6.f, bot = gy() + gh() - 4.f;
    auto vel_at = [&](double yy) { return std::clamp(static_cast<float>((bot - yy) / std::max(1.f, bot - top)), 0.f, 1.f); };
    if (vel_scale_) {   // Shift+drag: scale the selection's (or all) velocities proportionally — keep the shape
        const float factor = std::clamp(1.f + (vel_at(y1) - vel_at(vel_y0_)) * 1.8f, 0.f, 2.f);
        bool any = false;
        for (size_t i = 0; i < notes_.size(); ++i) if (i < sel_.size() && sel_[i]) { any = true; break; }
        for (size_t i = 0; i < notes_.size() && i < drag_orig_.size(); ++i) {
            if (any && !(i < sel_.size() && sel_[i])) continue;
            notes_[i].vel = std::clamp(drag_orig_[i].vel * factor, 0.f, 1.f);
        }
        dirty_ = true; return;
    }
    const float v0 = vel_at(vel_y0_), v1 = vel_at(y1);
    const double xa = std::min(vel_x0_, x1), xz = std::max(vel_x0_, x1);
    bool any = false;
    for (auto& n : notes_) {
        const double nx = xb(n.start);
        if (nx < xa - 6.0 || nx > xz + 6.0) continue;
        const double f = (std::fabs(x1 - vel_x0_) < 1.0) ? 1.0 : std::clamp((nx - vel_x0_) / (x1 - vel_x0_), 0.0, 1.0);
        n.vel = std::clamp(static_cast<float>(v0 + (v1 - v0) * f), 0.f, 1.f);
        any = true;
    }
    if (!any) {   // a click clear of any note in the span → set just the nearest note
        int best = -1; double bd = 1e18;
        for (size_t i = 0; i < notes_.size(); ++i) { const double d = std::fabs(x1 - xb(notes_[i].start)); if (d < bd) { bd = d; best = static_cast<int>(i); } }
        if (best >= 0 && bd < 40.0) notes_[best].vel = v1;
    }
    dirty_ = true;
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
    if (key == GLFW_KEY_PERIOD) { push_undo(); nt::scale_velocity(notes_, sel_, 1.15f);      dirty_ = true; return true; }  // louder
    if (key == GLFW_KEY_COMMA)  { push_undo(); nt::scale_velocity(notes_, sel_, 1.f / 1.15f); dirty_ = true; return true; }  // softer
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
    const Style& sty = style();
    editor_panel(r, { px, py, pw, ph }, title_.c_str(), sty.audio, kEditorHeaderH);
    auto hov = [&](Rect rr) { return hit(rr, hover_x_, hover_y_); };
    hover_status_.clear();
    // ADR-0048: shared title-strip controls (both modes) — dock/float toggle + close, real bounded buttons.
    icon_button(r, dock_btn_rect(px, py, pw), docked_ ? "float" : "dock", hov(dock_btn_rect(px, py, pw)));
    icon_button(r, close_btn_rect(px, py, pw), "\xE2\x9C\x95", hov(close_btn_rect(px, py, pw)), false, sty.red);  // ✕
    icon_button(r, follow_btn_rect(px, py, pw), "Flw", hov(follow_btn_rect(px, py, pw)), follow_);   // follow (active)
    icon_button(r, fit_btn_rect(px, py, pw), "Fit", hov(fit_btn_rect(px, py, pw)));                  // fit view
    // Inspector strip background (shared shell zone).
    r.draw_rect(px + 1.f, py + kEditorHeaderH + 1.f, pw - 2.f, insp_h() - 1.f, sty.region[0], sty.region[1], sty.region[2], 1.0f);
    r.draw_rect(px + 1.f, py + kEditorHeaderH + insp_h(), pw - 2.f, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
    if (!audio_) {
        { char rd[48]; std::snprintf(rd, sizeof rd, "%s \xC2\xB7 %d sel", kGrids[grid_idx_].label, selected_count());
          draw_text_r(r, follow_btn_rect(px, py, pw).x - 10.f, py + 9.f, rd, sty.dim, 1.0f, sty.fs_value); }
        // MIDI inspector controls, each drawn + hit from the SAME midi_insp() rect.
        const MidiInsp m = midi_insp(px, py, pw);
        segmented(r, m.tool, { "Draw", "Select" }, tool_ == Tool::Draw ? 0 : 1,
                  segmented_hit(m.tool, 2, hover_x_, hover_y_), sty.audio);
        stepper(r, m.grid, "GRID", kGrids[grid_idx_].label, stepper_hit(m.grid, hover_x_, hover_y_));
        icon_button(r, m.fold,  "Fold",  hov(m.fold),  fold_);
        icon_button(r, m.ghost, "Ghost", hov(m.ghost), ghost_);
        icon_button(r, m.step,  "Step",  hov(m.step),  step_mode_);
        { char key[12]; std::snprintf(key, sizeof key, "Key %s", scale_root_ < 0 ? "\xE2\x80\x94" : kPitchNames[scale_root_]);
          menu_button(r, m.key, key, hov(m.key), key_open_); }
        menu_button(r, m.scale, kScales[scale_type_].label, hov(m.scale), scale_open_);
        menu_button(r, m.lane, lane_axis_ < 0 ? "Velocity" : kAxisNames[lane_axis_], hov(m.lane), lane_open_);
        menu_button(r, m.quant, "Quantize", hov(m.quant), quant_open_);
        menu_button(r, m.xform, "\xE2\x8B\xAF Transform", hov(m.xform), xform_open_);
    } else {
        // AUDIO inspector controls (drive the same aud_req_ commit bits as the old header text).
        const AudioInsp a = audio_insp(px, py, pw);
        segmented(r, a.warp, { "Off", "Cplx", "Beat", "Rept" }, aud_warp_mode_ + 1,
                  segmented_hit(a.warp, 4, hover_x_, hover_y_), sty.audio);
        icon_button(r, a.autow, "Auto-warp", hov(a.autow));
        { char pl[16]; std::snprintf(pl, sizeof pl, "%+d st", static_cast<int>(std::lround(aud_pitch_)));
          stepper(r, a.pitch, "PITCH", pl, stepper_hit(a.pitch, hover_x_, hover_y_)); }
        const int slc = slice_mode_ == 0 ? 0 : slice_mode_ == 1 ? 1 : 2;
        segmented(r, a.slice, { "Off", "Tran", "Grid" }, slc, segmented_hit(a.slice, 3, hover_x_, hover_y_), sty.audio);
        icon_button(r, a.to_midi, "Slice \xE2\x86\x92 MIDI", hov(a.to_midi), slice_mode_ > 0);   // active when slicing
    }

    const float GX = roll_x0(), GY = gy(), GW = roll_w(), GH = gh();   // roll content (inset past the keys, MIDI)
    recess(r, { gx(), GY, gw(), GH });
    r.push_clip_rect(GX, GY, GW, GH);

    if (audio_) {
        // ADR-0048/0049: compose the shared waveform language (same component the Sampler editor reuses).
        // The view transform matches wxn()/wnorm_at() (key_w()==0 for audio, so GX==gx()), so drawing and
        // the interaction code below stay in lockstep.
        const WaveformView wv{ { GX, GY, GW, GH }, wav_x0_, wav_px_, wav_amp_ };
        static const float kTrim[3]  = { 0.92f, 0.84f, 0.34f };   // trim/loop handles (yellow)
        static const float kTrans[3] = { 0.50f, 0.50f, 0.36f };   // transient ticks
        static const float kSlice[3] = { 0.40f, 0.60f, 0.95f };   // slice dividers (blue)
        static const float kWarp[3]  = { 0.96f, 0.62f, 0.24f };   // warp markers (orange)
        static const float kPlay[3]  = { 0.95f, 0.35f, 0.35f };   // playhead (red)
        wv.bins(r, wave_.data(), static_cast<int>(wave_.size()), t0_, t1_);
        wv.center_line(r);
        wv.dim_outside(r, t0_, t1_);
        wv.handle(r, t0_, kTrim); wv.handle(r, t1_, kTrim);
        wv.ticks(r, trans_norm_.data(), static_cast<int>(trans_norm_.size()), kTrans);
        wv.dividers(r, slice_norm_.data(), static_cast<int>(slice_norm_.size()), kSlice, 0.5f);
        wv.dividers(r, warp_norm_.data(), static_cast<int>(warp_norm_.size()), kWarp, 0.85f, /*grab_tab*/true);
        if (playhead_ >= 0.0 && aud_loop_ > 0.0) {   // read position, mapped through the loop window
            double ph = std::fmod(playhead_, aud_loop_); if (ph < 0) ph += aud_loop_;
            wv.playhead(r, t0_ + (ph / aud_loop_) * (t1_ - t0_), kPlay);
        }
        r.pop_clip_rect();
        // ADR-0048: the footer crawl is gone — a hover-status pill names what the hovered handle does.
        {
            const float hx0 = wxn(t0_), hx1 = wxn(t1_);
            if (hover_y_ >= GY && hover_y_ < GY + GH &&
                (std::abs(hover_x_ - hx0) < 6.f || std::abs(hover_x_ - hx1) < 6.f))
                hover_status_ = "drag \xE2\x86\x92 trim loop handle";
            else if (hover_x_ >= GX && hover_x_ < GX + GW && hover_y_ >= GY && hover_y_ < GY + GH)
                hover_status_ = "scroll \xE2\x86\x92 zoom  \xC2\xB7  \xE2\x8C\xA5 amp  \xC2\xB7  drag marker \xE2\x86\x92 warp";
        }
        if (!hover_status_.empty())
            hover_status(r, px + 12.f, py + ph - 26.f, hover_status_.c_str(), sty.audio);
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
    // In-clip loop region: a gold brace in the ruler, faint tint over the roll, end bars.
    if (loop_end_ > loop_start_ + 1e-6) {
        const float lx0 = std::max(xb(loop_start_), GX), lx1 = std::min(xb(loop_end_), GX + GW);
        if (lx1 > lx0) {
            r.draw_rect(lx0, GY, lx1 - lx0, ruler_h(), 0.95f, 0.78f, 0.32f, 0.35f);        // ruler brace
            r.draw_rect(lx0, roll_top(), lx1 - lx0, lane_top() - roll_top(), 0.95f, 0.78f, 0.32f, 0.05f);  // region tint
        }
        const float e0 = xb(loop_start_), e1 = xb(loop_end_);
        if (e0 >= GX && e0 < GX + GW) r.draw_rect(e0, GY, 1.5f, GH, 0.95f, 0.78f, 0.32f, 0.85f);
        if (e1 >= GX && e1 < GX + GW) r.draw_rect(e1 - 1.5f, GY, 1.5f, GH, 0.95f, 0.78f, 0.32f, 0.85f);
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
          r.draw_rect(tx + 1.f, t0, tw - 2.f, tl, 0.42f, 0.45f, 0.52f, 0.9f);
      }
      if (hscroll_geom(tx, ty, tw, th, t0, tl)) {
          r.draw_rect(tx, ty, tw, th, 0.10f, 0.11f, 0.13f, 0.7f);
          r.draw_rect(t0, ty + 1.f, tl, th - 2.f, 0.42f, 0.45f, 0.52f, 0.9f);
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

    // ADR-0048: the footer instruction crawl is GONE. A hover-status pill names what the hovered thing
    // will do; the key-only power tools now live in the ⋯ Transform menu (drawn on top, below).
    {
        bool re = false;
        const int hn = hit_note(hover_x_, hover_y_, re);
        if (hn >= 0) hover_status_ = re ? "drag right edge \xE2\x86\x92 resize note  \xC2\xB7  \xE2\x8C\xA5 fine"
                                        : "drag \xE2\x86\x92 move  \xC2\xB7  \xE2\x8C\xA5 fine  \xC2\xB7  double-click \xE2\x86\x92 delete";
    }
    if (!hover_status_.empty())
        hover_status(r, px + 12.f, py + ph - 26.f, hover_status_.c_str(), sty.audio);

    // MIDI-1: inspector dropdowns, drawn LAST so they overlay the roll. One shared painter for all four;
    // `selected` gets a subtle marker, hover a brighter one.
    if (xform_open_ || key_open_ || scale_open_ || lane_open_ || quant_open_) {
        const MidiInsp m = midi_insp(px, py, pw);
        auto draw_menu = [&](Rect anchor, int n, int selected, const char* (*label)(int)) {
            const Rect menu{ anchor.x, anchor.y + anchor.h + 2.f, kXMenuW, n * kXItemH + 2.f };
            r.draw_rect(menu.x, menu.y, menu.w, menu.h, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
            r.draw_rect_outline(menu.x, menu.y, menu.w, menu.h, 1.f, sty.border[0], sty.border[1], sty.border[2], 1.0f);
            r.draw_rect(menu.x, menu.y, sty.accent_bar, menu.h, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f);
            for (int i = 0; i < n; ++i) {
                const Rect it = menu_item_rect(anchor, i);
                const bool h = hit(it, hover_x_, hover_y_);
                if (h)                 r.draw_rect(it.x + 1.f, it.y, it.w - 2.f, it.h, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 1.0f);
                else if (i == selected) r.draw_rect(it.x + 1.f, it.y, it.w - 2.f, it.h, sty.card[0], sty.card[1], sty.card[2], 1.0f);
                const float* tc = (h || i == selected) ? sty.text : sty.body;
                r.draw_text(it.x + 12.f, it.y + (kXItemH - 15.f * sty.fs_label) * 0.5f, label(i), tc[0], tc[1], tc[2], 1.0f, sty.fs_label);
            }
        };
        if (xform_open_) draw_menu(m.xform, kNumXItems, -1,             [](int i) { return kXItems[i].label; });
        if (key_open_)   draw_menu(m.key,   kNumKeys,    scale_root_ + 1, [](int i) { return kKeyLabels[i]; });
        if (scale_open_) draw_menu(m.scale, kNumScales,  scale_type_,     [](int i) { return kScales[i].label; });
        if (lane_open_)  draw_menu(m.lane,  4,           lane_axis_ + 1,  [](int i) { return kLaneLabels[i]; });
        if (quant_open_) {   // the Quantize popover: two live sliders (Amount, Swing), drawn over the roll
            const Rect pop = quant_pop_rect(m.quant);
            r.draw_rect(pop.x, pop.y, pop.w, pop.h, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
            r.draw_rect_outline(pop.x, pop.y, pop.w, pop.h, 1.f, sty.border[0], sty.border[1], sty.border[2], 1.0f);
            r.draw_rect(pop.x, pop.y, sty.accent_bar, pop.h, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f);
            const Rect sa = quant_slider_rect(m.quant, 0), ss = quant_slider_rect(m.quant, 1);
            char av[8]; std::snprintf(av, sizeof av, "%d%%", static_cast<int>(std::lround(quant_amount_ * 100.f)));
            char sv[8]; std::snprintf(sv, sizeof sv, "%d%%", static_cast<int>(std::lround(quant_swing_ / kQSwingMax * 100.f)));
            slider(r, sa.x, sa.y, sa.w, sa.h, quant_amount_,         "Amount", av, sty.audio, false, hit(sa, hover_x_, hover_y_) || drag_ == 30);
            slider(r, ss.x, ss.y, ss.w, ss.h, quant_swing_ / kQSwingMax, "Swing", sv, sty.audio, false, hit(ss, hover_x_, hover_y_) || drag_ == 31);
        }
    }
}

}  // namespace vivid::ui
