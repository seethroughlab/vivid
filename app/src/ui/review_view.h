#pragma once
// ADR-0064 — the REVIEW workspace's draw side + geometry. Pure: it draws a `ReviewModel` (plain data
// the controller in app/review_workspace.* assembles from the work records + the audition) and
// exposes the hit-rects through `ReviewGeom`, so draw and input agree on every rect (the layout.h
// discipline). No App, no records, no players here — those are the controller's.
//
// Layout (below the transport bar, the whole window):
//   left column  — Current version · Since your last review · Direction & protections · Decisions
//   main column  — work-status strip · the selected review item: question + passage · source tiles
//                  (Baseline | A | B, synchronized; click a tile = hear it) · transport (play, scrub,
//                  loop passage, level match) · per-candidate actions (Use / Keep / Revise / Dismiss)
//                  · Neither · comment box · this review's feedback
#include "ui/renderer_2d.h"
#include "ui/review_geom.h"

#include <string>
#include <vector>

namespace vivid::ui {

struct ReviewSourceView {
    std::string     label;        // "Baseline" / "A" / "B"
    std::string     sublabel;     // candidate purpose or version label
    std::string     status;       // candidate status ("proposed"/"kept"/"promoted"/"dismissed"), "" for baseline
    WGPUTextureView tex = nullptr; // latest video frame (null = nothing yet)
    float           aspect = 16.f / 9.f;
    bool            ready = false; // media loaded
    bool            audible = false;
    std::string     error;        // load failure to show instead of a black tile
    bool            is_candidate = false;
};

struct ReviewFeedbackLine { std::string when, where, text, kind; };

struct ReviewItemView {
    std::string id;
    std::string question;
    std::string passage;          // "bars 17–25"
    bool        resolved = false;
    std::string resolution;       // "Chose A · 2026-09-18" or ""
    std::vector<ReviewSourceView> sources;
    // transport
    bool   playing = false;
    bool   all_ready = false;
    double position01 = 0.0;      // 0..1 over the excerpt
    std::string position_text;    // "bar 21.3 · 0:08"
    bool   looping = false;
    double loop01_start = 0.0, loop01_end = 0.0;
    bool   level_match = false;
    bool   level_match_available = false;   // evidence carried loudness for every source
    // comment
    std::string comment_buf;
    bool        comment_active = false;
    std::string comment_at;       // "at bar 21"
    std::vector<ReviewFeedbackLine> feedback;
};

struct ReviewQueueRow { std::string id, question, meta; bool selected = false, resolved = false; };
struct ReviewBriefLine { std::string text; bool hard = false; bool is_preference = false; };

struct ReviewModel {
    std::string project_name;
    bool        has_project = false;
    std::string preferred_label, preferred_sub;    // "v-… · baseline" / "no preferred version yet"
    std::vector<std::string> since_lines;          // "Promoted A (chorus) · 2h ago"
    std::string work_status, work_status_reason;   // "idle" / "no runner connected"
    std::string brief_direction;
    int         brief_rev = 0;
    std::vector<ReviewBriefLine> brief_lines;
    std::vector<ReviewQueueRow>  queue;
    bool           has_item = false;
    ReviewItemView item;
    std::string    empty_reason;                   // shown in the main column when !has_item
    std::string    notice;                         // transient result line (e.g. "Promotion refused: …")
};

// `pending` open reviews show as a count badge on the Review segment.
void draw_workspace_switch(Renderer2D& r, bool review_selected, int pending, double mx, double my);

void draw_review(Renderer2D& r, const ReviewModel& m, const ReviewGeom& g, double mx, double my, double now);

}  // namespace vivid::ui
