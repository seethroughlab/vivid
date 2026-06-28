#pragma once
#include "ui/renderer_2d.h"
#include "midi/midi_clip.h"   // vivid_poc::ClipNote
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
              const vivid_poc::ClipNote* notes, int n, double length);
    void close() { open_ = false; drag_ = 0; }
    bool is_open() const { return open_; }
    int  track() const { return track_; }
    int  scene() const { return scene_; }
    double length() const { return length_; }
    const std::vector<vivid_poc::ClipNote>& notes() const { return notes_; }
    bool take_dirty() { bool d = dirty_; dirty_ = false; return d; }

    void draw(Renderer2D& r);
    bool on_down(double x, double y, double now);  // true if consumed
    void on_move(double x, double y);
    void on_up(double x, double y);
    void scroll(double dy);

private:
    bool   open_ = false, dirty_ = false;
    int    track_ = 0, scene_ = 0;
    std::string title_;
    std::vector<vivid_poc::ClipNote> notes_;
    double length_ = 4.0;
    int    pitch_lo_ = 48;
    double cell_ = 0.25;          // grid = 1/16 note

    int    drag_ = 0;             // 0 none, 1 move, 2 resize
    int    drag_idx_ = -1;
    double down_beat_ = 0; int down_pitch_ = 0;
    double orig_start_ = 0, orig_dur_ = 0; int orig_pitch_ = 0;
    double last_down_ = -1; int last_idx_ = -1;   // double-click tracking

    // Layout (pure functions of constants + pitch_lo_/length_).
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
