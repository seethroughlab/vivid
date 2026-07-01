#pragma once
#include "ui/renderer_2d.h"
#include "midi/midi_clip.h"   // vivid::session::ClipNote
#include <vector>
#include <string>

namespace vivid::ui {

// A minimal MIDI piano-roll editor on Renderer2D (rendering rewritten rather
// than lifting classic's editor_ui toolkit). Modal overlay: double-click a clip
// to open. Edits a local note buffer; main commits it via session_set_clip when
// take_dirty() reports a change. Interactions: click empty = add, drag = move,
// drag right edge = resize, double-click a note = delete, scroll = pan pitch.
class ClipEditor {
public:
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

    void draw(Renderer2D& r);
    bool on_down(double x, double y, double now);  // true if consumed
    void on_move(double x, double y);
    void on_up(double x, double y);
    void scroll(double dy);
    bool contains(double x, double y) const;       // is (x,y) inside the panel?
    void set_window(float w, float h) { win_w_ = w; win_h_ = h; }   // for docking/clamps

private:
    bool   open_ = false, dirty_ = false, docked_ = false, audio_ = false;
    int    track_ = 0, scene_ = 0;
    std::string title_;
    std::vector<vivid::session::ClipNote> notes_;
    std::vector<float> wave_;          // audio mode: peak bins
    float  t0_ = 0.f, t1_ = 1.f;       // audio mode: loop window
    double length_ = 4.0;
    int    pitch_lo_ = 48;
    double cell_ = 0.25;          // grid = 1/16 note
    float  px_ = 300.f, py_ = 110.f;   // floating panel top-left (draggable)
    float  win_w_ = 1280.f, win_h_ = 800.f;   // current window size (for dock/clamp)

    int    drag_ = 0;             // 0 none, 1 move, 2 resize, 3 pan-panel
    int    drag_idx_ = -1;
    double down_beat_ = 0; int down_pitch_ = 0;
    double orig_start_ = 0, orig_dur_ = 0; int orig_pitch_ = 0;
    double down_off_x_ = 0, down_off_y_ = 0;       // panel-drag grab offset
    double last_down_ = -1; int last_idx_ = -1;   // double-click tracking

    // Panel geometry (floating uses px_/py_; docked = bottom strip).
    void  panel(float& x, float& y, float& w, float& h) const;
    // Layout helpers derive from panel() + pitch_lo_/length_.
    float gx() const, gy() const, gw() const, gh() const;
    float bw() const { return gw() / float(length_ > 0 ? length_ : 4.0); }
    float rh() const;
    float xb(double b) const { return gx() + float(b) * bw(); }
    float yp(int p) const;
    double beat_at(double x) const { return (x - gx()) / bw(); }
    int    pitch_at(double y) const;
    int    hit_note(double x, double y, bool& right_edge) const;
    double snap(double b) const;
};

}  // namespace vivid::ui
