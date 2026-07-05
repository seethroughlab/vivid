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
                    const float* bins, int n, float t0, float t1);
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

private:
    bool   open_ = false, dirty_ = false, docked_ = false, audio_ = false;
    int    track_ = 0, scene_ = 0;
    std::string title_;
    std::vector<vivid::session::ClipNote> notes_;
    std::vector<uint8_t> sel_;         // selection mask, parallel to notes_
    std::vector<float> wave_;          // audio mode: peak bins
    float  t0_ = 0.f, t1_ = 1.f;       // audio mode: loop window
    double length_ = 4.0;
    Tool   tool_ = Tool::Select;
    double playhead_ = -1.0;           // absolute transport beats (< 0 = none)
    int    grid_idx_ = 3;              // index into the grid preset table (default 1/16)
    int    scale_root_ = -1;          // -1 = highlight off; else 0..11 (C..B)
    int    scale_type_ = 0;           // index into the scale table
    bool   follow_ = true;            // auto-scroll to keep the playhead in view
    std::vector<vivid::session::ClipNote> clip_;   // internal copy/paste clipboard (based at beat 0)

    // View transform (piano-roll). x = gx + (beat - view_beat0_)*beat_px_;
    // y = gy + (view_pitch_top_ - pitch)*row_h_.
    double view_beat0_ = 0.0;          // leftmost visible beat
    float  beat_px_ = 120.f;           // horizontal zoom (px per beat)
    int    view_pitch_top_ = 84;       // pitch drawn at the top row
    float  row_h_ = 12.f;              // vertical zoom (px per semitone)
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
    float yp(int p) const { return roll_top() + float(view_pitch_top_ - p) * row_h_; }
    double beat_at(double x) const { return view_beat0_ + (x - gx()) / beat_px_; }
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
