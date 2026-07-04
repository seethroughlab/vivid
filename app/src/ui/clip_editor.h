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

    // Transport playhead (absolute beats); the editor draws it at fmod(beats,length).
    void set_playhead(double abs_beats) { playhead_ = abs_beats; }

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

    // View transform (piano-roll). x = gx + (beat - view_beat0_)*beat_px_;
    // y = gy + (view_pitch_top_ - pitch)*row_h_.
    double view_beat0_ = 0.0;          // leftmost visible beat
    float  beat_px_ = 120.f;           // horizontal zoom (px per beat)
    int    view_pitch_top_ = 84;       // pitch drawn at the top row
    float  row_h_ = 12.f;              // vertical zoom (px per semitone)
    double cell_ = 0.25;               // grid = 1/16 note

    float  px_ = 300.f, py_ = 110.f;   // floating panel top-left (draggable)
    float  win_w_ = 1280.f, win_h_ = 800.f;   // current window size (for dock/clamp)

    // Undo/redo of the note buffer (selection is not snapshotted; it's cleared on undo).
    std::vector<std::vector<vivid::session::ClipNote>> undo_, redo_;

    int    drag_ = 0;             // 0 none,1 move,2 resize,3 pan-panel,4 marquee,10/11 audio trim
    double down_beat_ = 0; int down_pitch_ = 0;
    std::vector<vivid::session::ClipNote> drag_orig_;   // selection snapshot at drag start
    double down_off_x_ = 0, down_off_y_ = 0;       // panel-drag grab offset
    double last_down_ = -1; int last_idx_ = -1;   // double-click tracking

    // Panel geometry (floating uses px_/py_; docked = bottom strip).
    void  panel(float& x, float& y, float& w, float& h) const;
    float gx() const, gy() const, gw() const, gh() const;
    float bw() const { return beat_px_; }
    float rh() const { return row_h_; }
    float xb(double b) const { return gx() + float(b - view_beat0_) * beat_px_; }
    float yp(int p) const { return gy() + float(view_pitch_top_ - p) * row_h_; }
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
};

}  // namespace vivid::ui
