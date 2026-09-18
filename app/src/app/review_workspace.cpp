#include "app/review_workspace.h"

#include "app/app.h"
#include "app/edit_gateway.h"
#include "app/project_paths.h"
#include "app/window.h"
#include "audio/vst3_host.h"
#include "gpu/visual_graph.h"
#include "transport.h"
#include "ui/toasts.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace vivid {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string short_id(const std::string& id) {   // "v-20260918T010203Z-abc123" -> "abc123"
    const auto p = id.rfind('-');
    return p == std::string::npos ? id : id.substr(p + 1);
}

// "2h ago" / "3d ago" from an ISO-8601 UTC stamp; "" if unparsable.
std::string ago(const std::string& iso) {
    std::tm tm{};
    if (iso.size() < 20 || std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return {};
    tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    const long d = static_cast<long>(std::difftime(std::time(nullptr), t));
    char b[32];
    if (d < 90)          std::snprintf(b, sizeof b, "just now");
    else if (d < 5400)   std::snprintf(b, sizeof b, "%ldm ago", d / 60);
    else if (d < 172800) std::snprintf(b, sizeof b, "%ldh ago", d / 3600);
    else                 std::snprintf(b, sizeof b, "%ldd ago", d / 86400);
    return b;
}

std::string bar_text(double bar) {
    char b[32]; std::snprintf(b, sizeof b, "bar %.1f", bar); return b;
}

void utf8_append(std::string& s, unsigned cp) {
    if (cp < 0x80) s.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { s.push_back(static_cast<char>(0xE0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { s.push_back(static_cast<char>(0xF0 | (cp >> 18))); s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}
void utf8_pop(std::string& s) {
    if (s.empty()) return;
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
}

}  // namespace

ReviewWorkspace::~ReviewWorkspace() { unload_sources(); }

// --- records -------------------------------------------------------------------------------------
void ReviewWorkspace::sync_project() {
    const std::string& p = app_.project.current_project_path;
    const bool folder = !p.empty() && project_paths::is_folder_project(p);
    const fs::path dir = folder ? fs::path(p) : fs::path();
    if (dir != project_dir_ || folder != has_project_) {
        project_dir_ = dir; has_project_ = folder;
        selected_review_.clear();
        reload();
    }
}

void ReviewWorkspace::reload() {
    unload_sources();
    versions_.clear(); candidates_.clear(); reviews_.clear(); events_.clear(); feedback_.clear();
    has_brief_ = false; brief_ = work::Brief{};
    if (!has_project_) return;
    has_brief_  = work::load_brief(project_dir_, brief_);
    versions_   = work::list_versions(project_dir_);
    candidates_ = work::list_candidates(project_dir_);
    reviews_    = work::list_reviews(project_dir_);
    events_     = work::read_events(project_dir_);
    feedback_   = work::read_feedback(project_dir_);
    // Keep the selection if it still exists; else the oldest OPEN review (the one waiting longest).
    bool still = false;
    for (const auto& r : reviews_) if (r.id == selected_review_) still = true;
    if (!still) {
        selected_review_.clear();
        for (const auto& r : reviews_) if (r.status == "open") { selected_review_ = r.id; break; }
        if (selected_review_.empty() && !reviews_.empty()) selected_review_ = reviews_.back().id;
    }
    if (app_.session) known_preferred_ = session::session_preferred_version(app_.session);
    build_sources();
}

void ReviewWorkspace::select_review(const std::string& id) {
    if (id == selected_review_) return;
    selected_review_ = id;
    build_sources();
}

const work::Review* ReviewWorkspace::selected() const {
    for (const auto& r : reviews_) if (r.id == selected_review_) return &r;
    return nullptr;
}
work::Candidate* ReviewWorkspace::candidate_by_id(const std::string& id) {
    for (auto& c : candidates_) if (c.id == id) return &c;
    return nullptr;
}
const work::Version* ReviewWorkspace::version_by_id(const std::string& id) const {
    for (const auto& v : versions_) if (v.id == id) return &v;
    return nullptr;
}
std::string ReviewWorkspace::version_label(const std::string& id) const {
    if (id.empty()) return {};
    if (const auto* v = version_by_id(id)) return v->label.empty() ? short_id(id) : v->label + " (" + short_id(id) + ")";
    return short_id(id) + " (missing version record)";
}

std::string ReviewWorkspace::candidate_display(const std::string& id) const {
    for (const auto& s : sources_) if (s.candidate_id == id) return s.label + (s.sublabel.empty() ? "" : " (" + s.sublabel + ")");
    for (const auto& c : candidates_) if (c.id == id) return c.purpose.empty() ? short_id(id) : c.purpose;
    return short_id(id);
}

// --- sources / media -----------------------------------------------------------------------------
void ReviewWorkspace::unload_sources() {
    audition_.pause();
    audition_.clear_sources();
    sources_.clear();
    loop_on_ = false;
}

void ReviewWorkspace::build_sources() {
    unload_sources();
    const work::Review* r = selected();
    if (!r) return;
    opened_rev_ = app_.edit_gateway ? app_.edit_gateway->revision() : 0;
    // The excerpt's musical frame comes from the baseline recipe (every source shares one recipe).
    const double tbpm = app_.transport ? app_.transport->bpm.load(std::memory_order_relaxed) : 120.0;
    const double tbpb = app_.transport ? app_.transport->beats_per_bar.load(std::memory_order_relaxed) : 4;
    const json& rec = r->baseline_media.recipe;
    bpm_ = rec.is_object() ? rec.value("bpm", tbpm) : tbpm;
    beats_per_bar_ = rec.is_object() ? rec.value("beats_per_bar", tbpb) : tbpb;
    start_bar_ = rec.is_object() ? rec.value("start_bar", 1.0) : 1.0;

    auto add = [&](Source s) {
        if (!s.media.path.empty()) {
            s.player = make_platform_review_player();
            s.player->open((project_dir_ / fs::path(s.media.path)).string());
            if (s.media.recipe.is_object() && s.media.recipe.contains("rms_db")) { s.rms_db = s.media.recipe.value("rms_db", 0.0); s.has_rms = true; }
        }
        sources_.push_back(std::move(s));
    };
    { Source b; b.label = "Baseline"; b.sublabel = version_label(r->baseline); b.version_id = r->baseline; b.media = r->baseline_media; add(std::move(b)); }
    char letter = 'A';
    for (const auto& cid : r->candidates) {
        Source s; s.is_candidate = true; s.candidate_id = cid; s.label = std::string(1, letter++);
        if (const auto* c = candidate_by_id(cid)) {
            s.sublabel = c->purpose.empty() ? version_label(c->version) : c->purpose;
            s.status = c->status; s.version_id = c->version;
            if (!c->media.empty()) s.media = c->media.front();
        } else { s.sublabel = "missing candidate record " + cid; }
        add(std::move(s));
    }
    for (auto& s : sources_) audition_.add_source(s.player.get());
    // Level match gains from evidence: normalize to the loudest so nothing is pushed over 0 dBFS.
    bool all_rms = !sources_.empty(); double loudest = -1e9;
    for (const auto& s : sources_) { if (!s.has_rms) all_rms = false; else loudest = std::max(loudest, s.rms_db); }
    if (all_rms) for (std::size_t i = 0; i < sources_.size(); ++i)
        audition_.set_source_gain(i, static_cast<float>(std::pow(10.0, (loudest - sources_[i].rms_db) / 20.0)));
    // Hear the first candidate by default: the question is about the change, the baseline is a reference.
    audition_.set_audible(sources_.size() > 1 ? 1 : 0);
}

double ReviewWorkspace::bar_at(double sec) const { return start_bar_ + sec * bpm_ / 60.0 / std::max(1.0, beats_per_bar_); }
double ReviewWorkspace::sec_at_bar(double bar) const { return (bar - start_bar_) * 60.0 / bpm_ * std::max(1.0, beats_per_bar_); }
double ReviewWorkspace::excerpt_len() const { return audition_.duration(); }

void ReviewWorkspace::apply_loop() {
    const work::Review* r = selected();
    if (!loop_on_ || !r) { audition_.clear_loop(); return; }
    double a = 0.0, b = excerpt_len();
    if (r->passage.end_bar > r->passage.start_bar) {
        a = std::clamp(sec_at_bar(r->passage.start_bar), 0.0, b);
        b = std::clamp(sec_at_bar(r->passage.end_bar), a, excerpt_len());
    }
    if (b > a) audition_.set_loop(a, b); else audition_.clear_loop();
}

// --- lifecycle / frame -----------------------------------------------------------------------------
void ReviewWorkspace::enter(Window& win) {
    (void)win;
    sync_project();
    reload();
}
void ReviewWorkspace::leave(Window& win) {
    (void)win;
    audition_.pause();
    comment_active_ = false;
    if (has_project_) work::append_event(project_dir_, { "", "review_visited", json() });
}

int ReviewWorkspace::pending_count() const {
    int n = 0;
    for (const auto& r : reviews_) if (r.status == "open") ++n;
    return n;
}

void ReviewWorkspace::notice(const std::string& text, double now) {
    notice_ = text; notice_until_ = (now > 0.0 ? now : now_) + 8.0;
}

void ReviewWorkspace::watch_preferred_version() {
    // ADR-0062 §5: undo/redo of a promotion moves the document's preferred_version pointer. Mirror
    // that truthfully into the records — the reversal is an event, nothing is deleted.
    if (!app_.session || !has_project_) return;
    const std::string cur = session::session_preferred_version(app_.session);
    if (cur == known_preferred_) return;
    for (auto& c : candidates_) {
        if (c.version == known_preferred_ && c.status == "promoted") {
            c.status = "kept"; work::save_candidate(project_dir_, c);
            work::append_event(project_dir_, { "", "promotion_undone", json{{"candidate", c.id}, {"version", c.version}} });
            notice("Promotion undone: " + candidate_display(c.id) + " is kept as an alternative");
        }
        if (c.version == cur && !cur.empty() && c.status != "promoted") {
            c.status = "promoted"; work::save_candidate(project_dir_, c);
            work::append_event(project_dir_, { "", "promoted", json{{"candidate", c.id}, {"version", c.version}, {"via", "redo"}} });
            notice("Promotion redone: the project derives from " + candidate_display(c.id) + " again");
        }
    }
    known_preferred_ = cur;
    events_ = work::read_events(project_dir_);
    for (auto& s : sources_) if (const auto* c = candidate_by_id(s.candidate_id)) s.status = c->status;
}

void ReviewWorkspace::tick(Window& win, double now) {
    (void)win;
    now_ = now;
    sync_project();
    // While a load's undo baseline is still settling (async plugin rebinds), every settle frame bumps
    // the gateway revision without any creator edit. Keep the stale-promotion reference tracking it,
    // so the check fires for real edits only.
    if (app_.reseed_undo_baseline && app_.edit_gateway) opened_rev_ = app_.edit_gateway->revision();
    watch_preferred_version();
    if (!app_.vgraph) return;
    // Pull frames + upload; the audition keeps the group in lockstep.
    for (auto& s : sources_) {
        if (!s.player) continue;
        ReviewFrame f;
        if (s.player->poll_frame(f)) s.tex.update(app_.vgraph->device(), app_.vgraph->queue(), f);
    }
    audition_.tick();
}

// --- model -----------------------------------------------------------------------------------------
ui::ReviewModel ReviewWorkspace::build_model(const Window& win, double now) const {
    (void)win;
    ui::ReviewModel m;
    m.has_project = has_project_;
    m.project_name = has_project_ ? project_dir_.filename().string() : "no folder project open";
    const std::string pref = app_.session ? session::session_preferred_version(app_.session) : "";
    if (pref.empty()) { m.preferred_label = "No preferred version yet"; m.preferred_sub = "the open project is the reference"; }
    else { m.preferred_label = version_label(pref); m.preferred_sub = pref; }
    for (const auto& e : work::events_since_last_visit(events_)) {
        std::string line;
        const std::string cid = e.data.is_object() ? candidate_display(e.data.value("candidate", "")) : "";
        if (e.type == "promoted")              line = "Promoted " + cid + (e.data.is_object() && e.data.value("via", "") == "redo" ? " (redo)" : "");
        else if (e.type == "promotion_undone") line = "Promotion of " + cid + " undone";
        else if (e.type == "kept")             line = "Kept " + cid + " as an alternative";
        else if (e.type == "dismissed")        line = "Dismissed " + cid;
        else if (e.type == "revise")           line = "Asked for a revision of " + cid;
        else if (e.type == "review_opened")    line = "New decision: " + (e.data.is_object() ? e.data.value("question", "") : std::string());
        else if (e.type == "candidate_added")  line = "New candidate " + cid;
        else if (e.type == "brief_updated")    line = "Direction updated";
        else                                   line = e.type;
        const std::string when = ago(e.at);
        if (!when.empty()) line += "  \xC2\xB7  " + when;
        m.since_lines.push_back(line);
    }
    std::reverse(m.since_lines.begin(), m.since_lines.end());   // newest first
    // Gate 1 has no runner/worker: say so, never "working".
    m.work_status = "idle"; m.work_status_reason = "no runner connected";
    if (audition_.level_match()) m.work_status_reason += "   \xC2\xB7   level match is audition-only; the authored mix is untouched";
    if (has_brief_) {
        m.brief_direction = brief_.direction; m.brief_rev = brief_.rev;
        for (const auto& p : brief_.protections) m.brief_lines.push_back({ p.text, p.hard, false });
        for (const auto& p : brief_.preferences) m.brief_lines.push_back({ p.text, false, true });
    }
    for (const auto& r : reviews_) {
        ui::ReviewQueueRow row; row.id = r.id; row.question = r.question; row.selected = (r.id == selected_review_);
        row.resolved = r.status != "open";
        char meta[96];
        std::snprintf(meta, sizeof meta, "%d alternative%s \xC2\xB7 %s%s", static_cast<int>(r.candidates.size()),
                      r.candidates.size() == 1 ? "" : "s", row.resolved ? "resolved: " : "", row.resolved ? r.resolution.choice.c_str() : ago(r.created).c_str());
        row.meta = meta;
        m.queue.push_back(row);
    }
    // Open first, then resolved (newest first within each).
    std::stable_sort(m.queue.begin(), m.queue.end(), [](const ui::ReviewQueueRow& a, const ui::ReviewQueueRow& b) { return !a.resolved && b.resolved; });
    if (now < notice_until_) m.notice = notice_;

    const work::Review* r = selected();
    if (!has_project_) { m.empty_reason = "Review works on a folder project's work records (<project>/work/). Open or save a project as a folder, then return here."; return m; }
    if (!r) { m.empty_reason = reviews_.empty() ? "No review is waiting. When background work publishes a decision it appears in the Decisions list on the left; the current version is always playable from Create."
                                                : "Select a decision on the left."; return m; }
    m.has_item = true;
    auto& it = m.item;
    it.id = r->id; it.question = r->question;
    if (r->passage.end_bar > r->passage.start_bar) { char pb[48]; std::snprintf(pb, sizeof pb, "bars %g\xE2\x80\x93%g", r->passage.start_bar, r->passage.end_bar); it.passage = pb; }
    else it.passage = "whole excerpt";
    it.resolved = r->status != "open";
    if (it.resolved) {
        std::string who = r->resolution.candidate;
        for (const auto& s : sources_) if (s.candidate_id == r->resolution.candidate) who = s.label;
        it.resolution = r->resolution.choice == "use" ? "Chose " + who : r->resolution.choice == "neither" ? "Chose neither"
                      : r->resolution.choice == "revise" ? "Revise " + who : r->resolution.choice + " " + who;
        const std::string when = ago(r->resolution.at); if (!when.empty()) it.resolution += "  \xC2\xB7  " + when;
    }
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const auto& s = sources_[i];
        ui::ReviewSourceView v; v.label = s.label; v.sublabel = s.sublabel; v.status = s.status; v.is_candidate = s.is_candidate;
        v.audible = audition_.audible() == i;
        if (s.player) {
            v.ready = s.player->is_ready();
            v.error = s.player->error();
            if (v.ready && s.player->video_width() && s.player->video_height()) v.aspect = static_cast<float>(s.player->video_width()) / s.player->video_height();
        } else v.error = "no rendered media";
        v.tex = s.tex.valid() ? s.tex.view() : nullptr;
        it.sources.push_back(v);
    }
    it.playing = audition_.playing(); it.all_ready = audition_.all_ready();
    const double len = excerpt_len(), pos = audition_.position();
    it.position01 = len > 0.0 ? std::clamp(pos / len, 0.0, 1.0) : 0.0;
    { char pt[64]; const int mm = static_cast<int>(pos) / 60, ss = static_cast<int>(pos) % 60;
      std::snprintf(pt, sizeof pt, "%s  \xC2\xB7  %d:%02d", bar_text(bar_at(pos)).c_str(), mm, ss); it.position_text = pt; }
    it.looping = audition_.looping();
    if (it.looping && len > 0.0) { it.loop01_start = audition_.loop_start() / len; it.loop01_end = audition_.loop_end() / len; }
    it.level_match = audition_.level_match();
    it.level_match_available = !sources_.empty() && std::all_of(sources_.begin(), sources_.end(), [](const Source& s) { return s.has_rms; });
    it.comment_buf = comment_; it.comment_active = comment_active_;
    it.comment_at = "at " + bar_text(bar_at(pos));
    for (const auto& f : feedback_) {
        if (f.review != r->id) continue;
        ui::ReviewFeedbackLine l; l.when = ago(f.at); l.kind = f.kind;
        std::string where;
        for (const auto& s : sources_) if (!f.candidate.empty() ? s.candidate_id == f.candidate : s.version_id == f.version) where = s.label;
        if (f.passage.is_object() && f.passage.contains("bar")) { char b[32]; std::snprintf(b, sizeof b, " bar %.1f", f.passage.value("bar", 0.0)); where += b; }
        l.where = where; l.text = f.text;
        it.feedback.push_back(l);
    }
    std::reverse(it.feedback.begin(), it.feedback.end());
    return m;
}

void ReviewWorkspace::draw(ui::Renderer2D& r, Window& win, double mx, double my, double now) {
    const ui::ReviewModel m = build_model(win, now);
    geom_ = ui::review_geom(win.win_w, win.win_h, static_cast<int>(std::max<std::size_t>(1, m.item.sources.size())),
                            static_cast<int>(m.queue.size()), static_cast<int>(m.since_lines.size()), static_cast<int>(m.brief_lines.size()));
    ui::draw_review(r, m, geom_, mx, my, now);
}

// --- decisions -------------------------------------------------------------------------------------
std::string ReviewWorkspace::record_decision(const std::string& choice, const Source& src, const std::string& text) {
    work::Feedback f;
    f.id = work::make_id("f", choice + src.candidate_id + text);
    f.at = work::now_iso8601(); f.review = selected_review_; f.version = src.version_id; f.candidate = src.candidate_id;
    f.text = text.empty() ? choice : text; f.kind = "decision";
    f.interpretation = json{{"choice", choice}};
    f.dedup_key = f.id;
    work::append_feedback(project_dir_, f);
    return f.id;
}

void ReviewWorkspace::resolve_review(const std::string& choice, const std::string& candidate, const std::string& feedback_id) {
    for (auto& r : reviews_) if (r.id == selected_review_) {
        r.status = "resolved"; r.resolution = { choice, candidate, work::now_iso8601(), feedback_id };
        work::save_review(project_dir_, r);
    }
}

void ReviewWorkspace::act_use(std::size_t i) {
    if (i >= sources_.size() || !sources_[i].is_candidate) return;
    Source& src = sources_[i];
    work::Candidate* c = candidate_by_id(src.candidate_id);
    const work::Version* v = version_by_id(src.version_id);
    const work::Review* r = selected();
    if (!c || !v || !r) { notice("Promotion refused: candidate or version record is missing"); return; }
    if (!app_.edit_gateway || !app_.session) { notice("Promotion refused: no document"); return; }
    // ADR-0062 §4 — stale checks, all before any mutation.
    if (app_.reseed_undo_baseline) { notice("The project is still loading â try again in a moment"); return; }
    if (app_.edit_gateway->revision() != opened_rev_) {
        notice("The project changed since this review was opened \xE2\x80\x94 re-select it to promote against the current document");
        opened_rev_ = app_.edit_gateway->revision();   // the re-check is now against what the creator sees
        return;
    }
    const std::string cur_pref = session::session_preferred_version(app_.session);
    if (!r->baseline.empty() && !cur_pref.empty() && cur_pref != r->baseline) {
        notice("Promotion refused: the current version is no longer this review's baseline (" + short_id(r->baseline) + ")"); return;
    }
    if (has_brief_ && c->brief_rev > 0 && c->brief_rev != brief_.rev) {
        notice("Promotion refused: the brief changed since this candidate was made (rev " + std::to_string(c->brief_rev) + " \xE2\x86\x92 " + std::to_string(brief_.rev) + ")"); return;
    }
    std::vector<work::DependencyIssue> issues;
    if (!work::differs_only_in_document(project_dir_, *v, &issues)) {
        std::string what; for (std::size_t k = 0; k < issues.size() && k < 3; ++k) what += (k ? ", " : "") + issues[k].path + " (" + issues[k].status + ")";
        notice("Promotion refused: the candidate changes files outside project.json \xE2\x80\x94 " + what + ". Kept for a later round."); return;
    }
    // Apply: the snapshot's document + the pointer, as ONE gateway edit.
    std::ifstream in(work::version_dir(project_dir_, v->id) / "project.json", std::ios::binary);
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) { notice("Promotion refused: the snapshot's project.json is unreadable"); return; }
    doc["preferred_version"] = v->id;
    audition_.pause();
    if (!app_.edit_gateway->apply_document(doc, "Promote " + src.label, project_dir_.string())) { notice("Promotion failed to apply"); return; }
    known_preferred_ = v->id;
    // Records: candidate → promoted, the review resolved, the decision + event logged.
    c->status = "promoted"; work::save_candidate(project_dir_, *c); src.status = "promoted";
    const std::string fid = record_decision("use", src, "Use " + src.label);
    resolve_review("use", c->id, fid);
    work::append_event(project_dir_, { "", "promoted", json{{"candidate", c->id}, {"version", v->id}, {"review", r->id}} });
    events_ = work::read_events(project_dir_); feedback_ = work::read_feedback(project_dir_);
    opened_rev_ = app_.edit_gateway->revision();
    notice("Promoted " + src.label + " \xE2\x80\x94 the project now derives from " + short_id(v->id) + " (undo reverses it; save to keep it)");
}

void ReviewWorkspace::act_keep(std::size_t i) {
    if (i >= sources_.size() || !sources_[i].is_candidate) return;
    Source& src = sources_[i];
    work::Candidate* c = candidate_by_id(src.candidate_id);
    if (!c) return;
    if (c->status == "kept") return;
    c->status = "kept"; work::save_candidate(project_dir_, *c); src.status = "kept";
    record_decision("keep", src, "Keep " + src.label + " as an alternative");
    work::append_event(project_dir_, { "", "kept", json{{"candidate", c->id}, {"review", selected_review_}} });
    events_ = work::read_events(project_dir_); feedback_ = work::read_feedback(project_dir_);
    notice("Kept " + src.label + " as an alternative \xE2\x80\x94 the question stays open");
}

void ReviewWorkspace::act_revise(std::size_t i) {
    if (i >= sources_.size() || !sources_[i].is_candidate) return;
    if (comment_.empty()) { notice("Type what should change in the comment box, then Revise"); comment_active_ = true; return; }
    Source& src = sources_[i];
    const std::string fid = record_decision("revise", src, comment_);
    resolve_review("revise", src.candidate_id, fid);
    work::append_event(project_dir_, { "", "revise", json{{"candidate", src.candidate_id}, {"review", selected_review_}, {"feedback", fid}} });
    comment_.clear(); comment_active_ = false;
    events_ = work::read_events(project_dir_); feedback_ = work::read_feedback(project_dir_);
    notice("Revision requested from " + src.label + " \xE2\x80\x94 recorded for the next round");
}

void ReviewWorkspace::act_dismiss(std::size_t i, bool from_neither) {
    if (i >= sources_.size() || !sources_[i].is_candidate) return;
    Source& src = sources_[i];
    work::Candidate* c = candidate_by_id(src.candidate_id);
    if (!c || c->status == "dismissed") return;
    c->status = "dismissed"; work::save_candidate(project_dir_, *c); src.status = "dismissed";
    record_decision("dismiss", src, "Dismiss " + src.label);
    work::append_event(project_dir_, { "", "dismissed", json{{"candidate", c->id}, {"review", selected_review_}} });
    if (!from_neither) {
        // All alternatives rejected one by one: that IS "neither".
        bool all = true;
        for (const auto& s : sources_) if (s.is_candidate && s.status != "dismissed") all = false;
        if (all) resolve_review("neither", "", "");
        events_ = work::read_events(project_dir_); feedback_ = work::read_feedback(project_dir_);
        notice(all ? "Neither alternative chosen \xE2\x80\x94 the baseline stays" : "Dismissed " + src.label);
    }
}

void ReviewWorkspace::act_neither() {
    for (std::size_t i = 0; i < sources_.size(); ++i) if (sources_[i].is_candidate) act_dismiss(i, true);
    resolve_review("neither", "", "");
    events_ = work::read_events(project_dir_); feedback_ = work::read_feedback(project_dir_);
    notice("Neither alternative chosen \xE2\x80\x94 the baseline stays");
}

void ReviewWorkspace::add_comment() {
    if (comment_.empty() || sources_.empty()) return;
    const Source& src = sources_[std::min(audition_.audible(), sources_.size() - 1)];
    work::Feedback f;
    f.id = work::make_id("f", comment_); f.at = work::now_iso8601(); f.review = selected_review_;
    f.version = src.version_id; f.candidate = src.candidate_id;
    f.passage = json{{"bar", std::round(bar_at(audition_.position()) * 100.0) / 100.0}};
    f.text = comment_; f.kind = "comment"; f.dedup_key = f.id;
    if (!work::append_feedback(project_dir_, f)) { notice("Could not write the comment (disk?)"); return; }
    feedback_ = work::read_feedback(project_dir_);
    comment_.clear(); comment_active_ = false;
    notice("Comment saved on " + src.label + " " + bar_text(f.passage["bar"].get<double>()));
}

// --- input -----------------------------------------------------------------------------------------
bool ReviewWorkspace::mouse(Window& win, int button, int action, double mx, double my) {
    if (my < ui::kTopBarH) return false;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return true;   // Review owns the area below the bar
    if (action == GLFW_RELEASE) { scrubbing_ = false; return true; }
    const ui::ReviewGeom& g = geom_;
    // Left column.
    if (const int row = g.queue_row_at(mx, my); row >= 0) {
        // Rows are drawn in the model's sorted order; rebuild that order to map the click.
        ui::ReviewModel m = build_model(win, now_);
        if (row < static_cast<int>(m.queue.size())) select_review(m.queue[static_cast<std::size_t>(row)].id);
        return true;
    }
    if (!selected()) return true;
    if (const int t = g.tile_at(mx, my); t >= 0) { audition_.set_audible(static_cast<std::size_t>(t)); comment_active_ = false; return true; }
    if (ui::hit(g.play, mx, my)) { audition_.toggle_play(); return true; }
    if (ui::hit({ g.scrub.x, g.scrub.y - 6.f, g.scrub.w, g.scrub.h + 12.f }, mx, my)) {
        const double f = std::clamp((mx - g.scrub.x) / g.scrub.w, 0.0, 1.0);
        audition_.seek(f * excerpt_len()); scrubbing_ = true; return true;
    }
    if (ui::hit(g.loop_btn, mx, my)) { loop_on_ = !loop_on_; apply_loop(); return true; }
    if (ui::hit(g.level_btn, mx, my)) { audition_.set_level_match(!audition_.level_match()); return true; }
    for (std::size_t i = 0; i < sources_.size() && i < g.act_use.size(); ++i) {
        if (!sources_[i].is_candidate) continue;
        if (ui::hit(g.act_use[i], mx, my))     { act_use(i); return true; }
        if (ui::hit(g.act_keep[i], mx, my))    { act_keep(i); return true; }
        if (ui::hit(g.act_revise[i], mx, my))  { act_revise(i); return true; }
        if (ui::hit(g.act_dismiss[i], mx, my)) { act_dismiss(i, false); return true; }
    }
    if (ui::hit(g.neither, mx, my)) { if (selected() && selected()->status == "open") act_neither(); return true; }
    if (ui::hit(g.open_create, mx, my)) { switch_workspace(win, Window::Workspace::Create); return true; }
    if (ui::hit(g.comment_box, mx, my)) { comment_active_ = true; return true; }
    if (ui::hit(g.comment_send, mx, my)) { add_comment(); return true; }
    comment_active_ = false;
    return true;
}

bool ReviewWorkspace::key(Window& win, int key, int action, int mods) {
    (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return comment_active_;
    if (comment_active_) {
        if (key == GLFW_KEY_ESCAPE) { comment_active_ = false; }
        else if (key == GLFW_KEY_BACKSPACE) utf8_pop(comment_);
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) add_comment();
        else if ((mods & GLFW_MOD_SUPER) && key == GLFW_KEY_V) {
            if (const char* s = glfwGetClipboardString(win.glfw)) for (const char* p = s; *p; ++p) if (*p != '\n' && *p != '\r') comment_.push_back(*p);
        }
        return true;   // the box owns the keyboard
    }
    // ⌘-chords stay with the shell (⌘Z/⇧⌘Z undo/redo, ⌘S save … are routed by key_callback / the
    // native menu); every other key belongs to Review while it is the workspace, so a Create shortcut
    // can't toggle an overlay Review does not draw.
    if (mods & GLFW_MOD_SUPER) return false;
    if (!selected()) return true;
    switch (key) {
        case GLFW_KEY_SPACE: audition_.toggle_play(); return true;
        case GLFW_KEY_1: case GLFW_KEY_2: case GLFW_KEY_3: case GLFW_KEY_4:
            audition_.set_audible(static_cast<std::size_t>(key - GLFW_KEY_1)); return true;
        case GLFW_KEY_TAB: audition_.next_audible(); return true;
        case GLFW_KEY_L: loop_on_ = !loop_on_; apply_loop(); return true;
        case GLFW_KEY_LEFT:  audition_.seek(audition_.position() - 60.0 / bpm_ * beats_per_bar_); return true;   // one bar
        case GLFW_KEY_RIGHT: audition_.seek(audition_.position() + 60.0 / bpm_ * beats_per_bar_); return true;
        case GLFW_KEY_HOME:  audition_.seek(0.0); return true;
        default: return true;
    }
}

void ReviewWorkspace::motion(double mx, double my) {
    (void)my;
    if (!scrubbing_ || geom_.scrub.w <= 0.f) return;
    const double f = std::clamp((mx - geom_.scrub.x) / geom_.scrub.w, 0.0, 1.0);
    audition_.seek(f * excerpt_len());
}

void ReviewWorkspace::poll(double now) {
    sync_project();
    if (now < next_poll_) return;
    next_poll_ = now + 5.0;
    if (has_project_) reviews_ = work::list_reviews(project_dir_);
}

void switch_workspace(Window& win, Window::Workspace ws) {
    if (win.workspace == ws) return;
    if (win.review && win.workspace == Window::Workspace::Review) win.review->leave(win);
    win.workspace = ws;
    if (win.review && ws == Window::Workspace::Review) win.review->enter(win);
}

bool ReviewWorkspace::chr(unsigned cp) {
    if (!comment_active_) return false;
    if (cp < 0x20) return true;
    utf8_append(comment_, cp);
    return true;
}

}  // namespace vivid
