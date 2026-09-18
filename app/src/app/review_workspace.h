#pragma once
// ADR-0064 — the REVIEW workspace controller. Owns what the Review surface needs at runtime — the
// loaded work records (app/src/work), one ReviewPlayer + texture per source of the selected review
// item, the synchronized A/B audition, the comment buffer — and performs the creator's decisions:
//   Use this version  → PROMOTION through the edit gateway (one reversible document edit; ADR-0062 §3)
//   Keep              → the candidate stays as an alternative (status kept), the question stays open
//   Revise            → resolves the item with the typed feedback as the next round's direction
//   Dismiss / Neither → explicit rejection; "neither" is a valid answer (ADR-0064 §4)
//   Comment           → feedback attached to the AUDIBLE version + the current bar (§5)
// Every action writes work/ records synchronously and needs no connected agent (§4). Drawing is
// ui/review_view (pure); this class builds the ReviewModel it draws and routes input through the
// same ReviewGeom, so hit-rects and pixels agree.
#include "platform/review_player.h"
#include "ui/media_texture.h"
#include "ui/review_audition.h"
#include "ui/review_view.h"
#include "work/work_records.h"
#include "app/window.h"   // Window::Workspace (switch_workspace)

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vivid {

struct App;

class ReviewWorkspace {
public:
    explicit ReviewWorkspace(App& app) : app_(app) {}
    ~ReviewWorkspace();

    // Workspace lifecycle. enter() (re)loads the records for the current project; leave() stops
    // playback and appends the `review_visited` event that anchors "since your last review".
    void enter(Window& win);
    void leave(Window& win);
    // Re-read work/ from disk (after an action, or when the project path changed).
    void reload();
    void select_review(const std::string& id);

    // Per frame while Review is the active workspace.
    void tick(Window& win, double now);
    void draw(ui::Renderer2D& r, Window& win, double mx, double my, double now);
    // Input while Review is active. Return true when consumed.
    bool mouse(Window& win, int button, int action, double mx, double my);
    bool key(Window& win, int key, int action, int mods);
    bool chr(unsigned codepoint);
    void motion(double mx, double my);   // scrub drag
    bool text_active() const { return comment_active_; }

    // Open (unresolved) reviews — the transport-bar badge, shown in BOTH workspaces.
    int pending_count() const;
    // Cheap check the frame loop can call from Create: has the project path changed under us?
    void sync_project();
    // From Create, every frame: re-lists the reviews every few seconds so the badge notices a decision
    // a script/agent published while the creator was authoring. Never switches the workspace.
    void poll(double now);

private:
    struct Source {
        std::string   label, sublabel, status, candidate_id, version_id;
        work::Media   media;
        bool          is_candidate = false;
        double        rms_db = 0.0; bool has_rms = false;   // from the render evidence (recipe.rms_db)
        std::unique_ptr<ReviewPlayer> player;
        ui::MediaTexture tex;
    };

    void build_sources();
    void unload_sources();
    const work::Review* selected() const;
    work::Candidate* candidate_by_id(const std::string& id);
    const work::Version* version_by_id(const std::string& id) const;
    std::string version_label(const std::string& id) const;
    // "A (cooler, more open chorus)" for a candidate of the selected review; purpose or short id otherwise.
    std::string candidate_display(const std::string& id) const;
    // seconds <-> bars for the loaded excerpt
    double bar_at(double sec) const;
    double sec_at_bar(double bar) const;
    double excerpt_len() const;
    void   apply_loop();

    // the decisions
    void act_use(std::size_t i);
    void act_keep(std::size_t i);
    void act_revise(std::size_t i);
    void act_dismiss(std::size_t i, bool from_neither);
    void act_neither();
    void add_comment();
    std::string record_decision(const std::string& choice, const Source& src, const std::string& text);
    void resolve_review(const std::string& choice, const std::string& candidate, const std::string& feedback_id);
    void watch_preferred_version();
    void notice(const std::string& text, double now = 0.0);

    ui::ReviewModel build_model(const Window& win, double now) const;

    App& app_;
    std::filesystem::path project_dir_;
    bool has_project_ = false;

    work::Brief                  brief_;  bool has_brief_ = false;
    std::vector<work::Version>   versions_;
    std::vector<work::Candidate> candidates_;
    std::vector<work::Review>    reviews_;
    std::vector<work::Event>     events_;
    std::vector<work::Feedback>  feedback_;

    std::string          selected_review_;
    std::vector<Source>  sources_;
    ui::ReviewAudition   audition_;
    double bpm_ = 120.0, beats_per_bar_ = 4.0, start_bar_ = 1.0;   // the excerpt's recipe
    bool   loop_on_ = false;

    std::string comment_;
    bool        comment_active_ = false;
    std::string notice_;
    double      notice_until_ = 0.0;
    double      now_ = 0.0;

    unsigned    opened_rev_ = 0;              // gateway revision when the selected item was opened
    std::string known_preferred_;             // the session's preferred_version as last seen (undo/redo watcher)
    ui::ReviewGeom geom_;                     // last frame's geometry (input uses it)
    bool scrubbing_ = false;
    double next_poll_ = 0.0;
};

// The ONLY way the workspace changes (ADR-0064 §1: never automatically): runs leave()/enter() on the
// Review controller so records reload and the visit is recorded. No-op when already there.
void switch_workspace(Window& win, Window::Workspace ws);

}  // namespace vivid
