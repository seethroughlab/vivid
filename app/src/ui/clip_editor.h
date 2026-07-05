#pragma once
#include "ui/renderer_2d.h"
#include "midi/midi_clip.h"   // vivid::session::ClipNote
#include <vector>
#include <string>
#include <cstdint>

namespace vivid::ui {

// A modern MIDI piano-roll editor on Renderer2D (rendering rewritten rather than
// lifting classic's editor_ui toolkit). Non-modal overlay: double-click a clip to
// open. Edits a local note buffer; main commits it via session_set_clip when
// take_dirty() reports a change.
//
// M0 substrate: a view transform (beat/pitch zoom + scroll), a selection set, a
// Select/Draw tool mode, a per-editor undo stack, a keyboard + modifier-aware scroll
// router, and a transport playhead. Interactions: Select tool — click a note to
// select (shift-click toggles), drag the body to move the selection, drag the right
// edge to resize; Draw tool — click empty to add. Double-click a note deletes.
class ClipEditor {
public:
    enum class Tool { Select, Draw };

    void open(int track, int scene, const std::string& title,
              const vivid::session::ClipNote* notes, int n, double length);
    // Audio (waveform) mode: `bins` = peak amplitude per bin, trim = loop window.
    void open_audio(int track, int scene, const std::string& title,
                    const float* bins, int n, float t0, float t1, double loop_beats = 4.0);
    void close() { open_ = false; drag_ = 0; }
    bool is_open() const { return open_; }
    bool is_audio() const { return audio_; }
    int  track() const { return track_; }
    int  scene() const { return scene_; }
    double length() const { return length_; }
    const std::vector<vivid::session::ClipNote>& notes() const { return notes_; }
    void audio_trim(float& t0, float& t1) const { t0 = t0_; t1 = t1_; }
    bool take_dirty() { bool d = dirty_; dirty_ = false; return d; }

    // Transport playhead (absolute beats); the editor draws it at fmod(beats,length)
    // and, when follow is on, scrolls to keep it in view.
    void set_playhead(double abs_beats);

    void draw(Renderer2D& r);
    bool on_down(double x, double y, double now, int mods);  // true if consumed (mods = GLFW_MOD_*)
    void on_move(double x, double y);
    void on_up(double x, double y);
    // Modifier-aware scroll (mods = GLFW_MOD_*). Cmd = h-zoom, Alt = v-zoom,
    // Shift = h-pan, none = v-pan (pitch). (mx,my) is the cursor for zoom anchoring.
    void on_scroll(double xoff, double yoff, int mods, double mx, double my);
    // Keyboard router; returns true if the key was consumed (mods = GLFW_MOD_*).
    bool on_key(int key, int mods);
    bool contains(double x, double y) const;       // is (x,y) inside the panel?
    void set_window(float w, float h) { win_w_ = w; win_h_ = h; }   // for docking/clamps
    void set_dock_h(float h) { dock_h_ = h; }       // shared bottom-dock height (docked mode)
    bool is_docked() const { return docked_; }      // true = fills the bottom inspector dock
    // Step input (M6.5): when on, live note-ons (typing / hardware MIDI, routed by the
    // input layer) write a note at the step cursor and advance it by one grid cell. A
    // chord (notes held together) lands on the same step; the cursor advances when all
    // are released.
    bool step_mode() const { return step_mode_; }
    void step_note_on(int pitch, float vel);
    void step_note_off();
    // Audio warp/shaping (A5): frame.cpp loads the clip's state + markers, and drains pending edits.
    void set_audio_shape(int warp_mode, float pitch) { aud_warp_mode_ = warp_mode; aud_pitch_ = pitch; }
    void set_audio_markers(const float* warp_s, const double* warp_b, int nw, const float* trans, int nt) {
        warp_norm_.assign(warp_s, warp_s + (nw > 0 ? nw : 0));
        warp_b_.assign(warp_b, warp_b + (nw > 0 ? nw : 0));
        trans_norm_.assign(trans, trans + (nt > 0 ? nt : 0));
    }
    void set_slices(const float* s, int n) { slice_norm_.assign(s, s + (n > 0 ? n : 0)); }
    int  take_audio_req() { int r = aud_req_; aud_req_ = 0; return r; }   // pending-commit bitmask (1/2/4/8)
    int  audio_warp_mode() const { return aud_warp_mode_; }
    float audio_pitch() const { return aud_pitch_; }
    int  audio_slice_mode() const { return slice_mode_; }
    const std::vector<float>&  warp_samples() const { return warp_norm_; }
    const std::vector<double>& warp_beats()   const { return warp_b_; }

private:
    bool   open_ = false, dirty_ = false, docked_ = false, audio_ = false;
    int    track_ = 0, scene_ = 0;
    std::string title_;
    std::vector<vivid::session::ClipNote> notes_;
    std::vector<uint8_t> sel_;         // selection mask, parallel to notes_
    std::vector<float> wave_;          // audio mode: peak bins
    float  t0_ = 0.f, t1_ = 1.f;       // audio mode: loop window (normalized 0..1)
    double wav_x0_ = 0.0;              // audio: leftmost visible normalized position
    float  wav_px_ = 600.f;            // audio: pixels per normalized unit (horizontal zoom)
    float  wav_amp_ = 1.f;             // audio: vertical amplitude zoom
    double aud_loop_ = 4.0;            // audio: clip loop length in beats (for the playhead)
    // Audio warp/shaping (A5): mirrored for the header UI + overlay; committed via frame.cpp.
    int    aud_warp_mode_ = -1;        // -1 off, 0 Complex, 1 Beats, 2 Repitch
    float  aud_pitch_ = 0.f;           // clip transpose in semitones
    std::vector<float> warp_norm_, trans_norm_;   // marker (norm sample) / transient positions 0..1
    std::vector<double> warp_b_;       // marker beats (parallel to warp_norm_)
    std::vector<float> slice_norm_;    // A6: slice boundary positions (normalized)
    int    slice_mode_ = 0;            // A6: 0 off, 1 transients, 3 grid
    int    marker_drag_ = -1;          // warp marker being dragged
    int    aud_req_ = 0;               // pending commit bits: 1 shaping, 2 auto, 4 warp-pts, 8 slice
    double length_ = 4.0;
    Tool   tool_ = Tool::Select;
    double playhead_ = -1.0;           // absolute transport beats (< 0 = none)
    int    grid_idx_ = 3;              // index into the grid preset table (default 1/16)
    int    scale_root_ = -1;          // -1 = highlight off; else 0..11 (C..B)
    int    scale_type_ = 0;           // index into the scale table
    bool   follow_ = true;            // auto-scroll to keep the playhead in view
    bool   step_mode_ = false;        // step input (M6.5)
    double step_cursor_ = 0.0;        // step-input write position (beats)
    int    step_held_ = 0;            // notes currently held in the step chord
    std::vector<vivid::session::ClipNote> clip_;   // internal copy/paste clipboard (based at beat 0)

    // View transform (piano-roll). x = gx + (beat - view_beat0_)*beat_px_;
    // y = roll_top + (row_of_pitch(pitch) - view_row_top_)*row_h_. Rows go top-down:
    // unfolded row r = pitch 127-r; folded, rows are the occupied pitches (descending).
    double view_beat0_ = 0.0;          // leftmost visible beat
    float  beat_px_ = 120.f;           // horizontal zoom (px per beat)
    int    view_row_top_ = 43;         // row index at the top (43 = pitch 84, the old default)
    float  row_h_ = 12.f;              // vertical zoom (px per row)
    bool   fold_ = false;              // fold: show only occupied pitch rows
    std::vector<int> fold_rows_;       // occupied pitches, descending (row order) when folded
    double cell_ = 0.25;               // grid = 1/16 note

    float  px_ = 300.f, py_ = 110.f;   // floating panel top-left (draggable)
    float  win_w_ = 1280.f, win_h_ = 800.f;   // current window size (for dock/clamp)
    float  dock_h_ = 210.f;            // shared bottom-dock height (matches Window::dock_h)

    // Undo/redo of the note buffer (selection is not snapshotted; it's cleared on undo).
    std::vector<std::vector<vivid::session::ClipNote>> undo_, redo_;

    int    drag_ = 0;             // 0 none,1 move,2 resize,3 pan-panel,4 marquee,5 velocity,10/11 audio trim
    double down_beat_ = 0; int down_pitch_ = 0;
    std::vector<vivid::session::ClipNote> drag_orig_;   // selection snapshot at drag start
    double down_off_x_ = 0, down_off_y_ = 0;       // panel-drag grab offset
    double last_down_ = -1; int last_idx_ = -1;   // double-click tracking
    double marq_x_ = 0, marq_y_ = 0; bool marq_add_ = false;   // marquee current corner + additive
    int    lane_idx_ = -1;        // note whose velocity a lane-drag targets
    int    lane_axis_ = -1;       // bottom lane: -1 velocity, 0 bend, 1 pressure, 2 timbre
    bool   bend_snap_ = false;    // quantize painted bend to whole semitones
    uint32_t tool_seed_ = 1;      // varies humanize between repeated presses
    std::vector<vivid::session::CurveBp> paint_;   // raw freehand stroke (note-normalized t,value)
    int    paint_note_ = -1;      // note the current stroke paints into

    // Panel geometry (floating uses px_/py_; docked = bottom strip).
    void  panel(float& x, float& y, float& w, float& h) const;
    float gx() const, gy() const, gw() const, gh() const;
    float lane_h() const { return audio_ ? 0.f : 54.f; }        // velocity/expression lane height
    float ruler_h() const { return audio_ ? 0.f : 15.f; }       // bars/beats ruler strip
    float roll_top() const { return gy() + ruler_h(); }         // piano-roll top (below ruler)
    float roll_h() const { return gh() - lane_h() - ruler_h(); }// piano-roll height (ruler..lane)
    float lane_top() const { return gy() + gh() - lane_h(); }   // bottom lane top
    float bw() const { return beat_px_; }
    float rh() const { return row_h_; }
    float xb(double b) const { return gx() + float(b - view_beat0_) * beat_px_; }
    float yp(int p) const { return roll_top() + float(row_of_pitch(p) - view_row_top_) * row_h_; }
    double beat_at(double x) const { return view_beat0_ + (x - gx()) / beat_px_; }
    // Pitch<->row mapping (fold-aware). Row 0 is the top; unfolded rows are pitch 127-r.
    int    nrows() const { return fold_ ? std::max(1, static_cast<int>(fold_rows_.size())) : 128; }
    int    pitch_of_row(int r) const;
    int    row_of_pitch(int p) const;
    void   rebuild_fold();             // recompute fold_rows_ from notes_ (occupied pitches)
    // Audio mode: normalized buffer position (0..1) <-> screen x, with zoom/scroll.
    float  wxn(double n) const { return gx() + float(n - wav_x0_) * wav_px_; }
    double wnorm_at(double x) const { return wav_x0_ + (x - gx()) / wav_px_; }
    void   clamp_wav_view();
    int    pitch_at(double y) const;
    int    hit_note(double x, double y, bool& right_edge) const;
    double snap(double b) const;

    // Editing helpers that keep sel_ parallel to notes_ and manage undo.
    void  push_undo();
    void  fit_view();                       // frame content on open
    int   selected_count() const;
    void  clear_sel();
    void  add_note(const vivid::session::ClipNote& n, bool select);
    void  delete_selected();
    void  clamp_view();
    void  copy_sel();                 // -> clip_
    void  paste(double at_beat);      // clip_ -> notes, selects pasted
    void  duplicate_sel();            // copy + paste one span later
    void  finish_marquee(double x, double y);
    // Expression lane (M4) value<->pixel mapping. lane_value_at maps a lane y to the
    // current axis's units (bend = semitones, pressure/timbre 0..1); lane_y_for inverts.
    float lane_value_at(double y) const;
    float lane_t_at(double x) const;         // x -> normalized t within paint_note_
    float lane_y_for(float v) const;
    void  finish_paint();
};

}  // namespace vivid::ui
