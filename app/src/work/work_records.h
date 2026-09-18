#pragma once
// ADR-0061/0062 — the durable WORK RECORDS of a project: the creative memory that outlives an agent
// conversation. Everything lives under `<project>/work/` as plain JSON (append-only files are JSON
// Lines), readable by the app, the control server, and any script WITHOUT the app running. This
// module is pure data + filesystem — no App, GPU, or audio — so it is headless-testable and sits low
// in the layer stack (app/ and cli/ both depend on it; it depends on nothing above nlohmann/std).
//
// Layout (docs/roadmap/continuous-creative-workflow-gate1.md):
//   brief.json                 the current brief (direction / protections / preferences), `rev`-versioned
//   brief.log.jsonl            every brief revision ever saved (superseded briefs are never deleted)
//   versions/<id>/version.json an immutable version: parentage + per-dependency SHA-256
//   versions/<id>/project.json (+ copied assets) the complete snapshot (complete copies first; dedup later)
//   candidates/<id>.json       a version proposed for a purpose, with provenance + evidence + status
//   reviews/<id>.json          one focused question over exact candidate versions
//   feedback.jsonl             comments + decisions, original wording kept, dedup-keyed
//   events.jsonl               what happened (promoted / kept / dismissed / review_visited …)
//   media/<candidate>/…        rendered evidence, referenced from candidate evidence with its recipe
//
// The one work-records fact stored OUTSIDE this folder is `preferred_version` in project.json: it is
// document state (saved/loaded/undone with the document) so promotion is a single reversible edit.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vivid::work {

constexpr int kSchema = 1;

// --- paths ---------------------------------------------------------------------------------------
std::filesystem::path work_dir(const std::filesystem::path& project_dir);           // <project>/work
std::filesystem::path version_dir(const std::filesystem::path& project_dir, const std::string& id);
std::filesystem::path media_dir(const std::filesystem::path& project_dir, const std::string& candidate_id);

// ISO-8601 UTC "YYYY-MM-DDTHH:MM:SSZ" for now (or a given time_t).
std::string now_iso8601();
std::string iso8601(std::time_t t);
// A record id: "<prefix>-<utc compact time>-<6 hex of sha256(seed + time + counter)>". Unique within
// a process even when called many times in one second; `seed` should carry the record's content.
std::string make_id(const std::string& prefix, const std::string& seed);

// --- brief (ADR-0061 §2/§4) -----------------------------------------------------------------------
struct Protection {
    std::string    id;
    std::string    text;            // the creator's wording
    bool           hard = true;     // hard protection (cannot change) vs a guiding preference
    nlohmann::json scope;           // covered objects + relevant dependencies, e.g. {"tracks":["Drums"]}
};
struct Preference { std::string id; std::string text; };
struct Brief {
    int                     schema = kSchema;
    int                     rev    = 0;       // bumped by every save_brief
    std::string             updated;          // ISO-8601
    std::string             direction;        // the creator's wording, verbatim
    std::vector<Protection> protections;
    std::vector<Preference> preferences;
    nlohmann::json          interpretation;   // structured reading of the above (optional, agent-written)
};
nlohmann::json to_json(const Brief&);
Brief          brief_from_json(const nlohmann::json&);
bool  load_brief(const std::filesystem::path& project_dir, Brief& out, std::string* err = nullptr);
// Bumps `rev`, stamps `updated`, writes brief.json and appends the same record to brief.log.jsonl.
bool  save_brief(const std::filesystem::path& project_dir, Brief& brief, std::string* err = nullptr);

// --- versions (ADR-0062 §1/§2) --------------------------------------------------------------------
struct Dependency {
    std::string path;     // project-relative, forward slashes
    std::string sha256;
    std::string kind;     // "document" | "asset" | "package_source" | "package_manifest"
};
struct PluginDependency {
    int         track = -1;
    std::string name, uid, format;   // as the document records them
    bool        state_present = false;
};
struct Version {
    int                           schema = kSchema;
    std::string                   id;
    std::string                   parent;           // version id or ""
    std::string                   created;          // ISO-8601
    std::string                   label;
    std::string                   source;           // "manual" | "candidate" | "promotion"
    int                           brief_rev = 0;
    std::string                   document_sha256;  // of versions/<id>/project.json
    std::vector<Dependency>       dependencies;
    std::vector<PluginDependency> plugins;
};
nlohmann::json to_json(const Version&);
Version        version_from_json(const nlohmann::json&);
bool load_version(const std::filesystem::path& project_dir, const std::string& id, Version& out,
                  std::string* err = nullptr);
bool save_version(const std::filesystem::path& project_dir, const Version& v, std::string* err = nullptr);
std::vector<Version> list_versions(const std::filesystem::path& project_dir);   // sorted by `created`

// Snapshot the project folder into versions/<new id>/: copies project.json and every co-located
// file EXCEPT `work/` itself and build outputs (*.dylib, *.so, *.o, *.dSYM), records a SHA-256 per
// copied file, and writes version.json. `parent`/`label`/`source`/`brief_rev` are recorded as given;
// plugin identities are read from the document's tracks. Returns the id; "" + err on failure.
struct SnapshotOptions {
    std::string parent, label, source = "manual";
    int         brief_rev = 0;
};
std::string snapshot_version(const std::filesystem::path& project_dir, const SnapshotOptions& opt,
                             std::string* err = nullptr);
// Is `rel` a build output the snapshot leaves behind? (Exposed for the tests.)
bool is_build_output(const std::filesystem::path& rel);

// Compare a version's recorded dependencies with the LIVE project folder. "missing" = the file is
// gone; "changed" = its digest differs. Empty = the version can be reproduced from the folder as-is.
struct DependencyIssue { std::string path, status; /* "missing" | "changed" */ };
std::vector<DependencyIssue> check_dependencies(const std::filesystem::path& project_dir, const Version& v);
// Does the version differ from the live folder ONLY in project.json? (The gate-1 promotion
// precondition: assets and project-local code are identical, so applying the snapshot's document is
// the whole promotion.) `issues` receives the non-document differences.
bool differs_only_in_document(const std::filesystem::path& project_dir, const Version& v,
                              std::vector<DependencyIssue>* issues = nullptr);

// --- candidates (ADR-0062 §1/§3/§6) ---------------------------------------------------------------
struct Media {
    std::string    path;     // project-relative (usually under work/media/<candidate>/)
    std::string    kind;     // "av" | "audio" | "image"
    nlohmann::json recipe;   // how it was rendered: scene, start_bar, bars, fps, warmup_sec, seeds …
};
struct Candidate {
    int            schema = kSchema;
    std::string    id;
    std::string    version;      // the version this candidate proposes
    std::string    baseline;     // the version it was explored from
    std::string    purpose;      // what it tries to achieve (from the brief / work item)
    int            brief_rev = 0;
    std::string    created;
    nlohmann::json provenance;   // {"runner":..., "run":..., "summary":...}
    std::vector<Media> media;
    nlohmann::json technical;    // array of technical-check results (separate from preference)
    nlohmann::json intent;       // array of content/intent evaluations (ADR-0026; may say "unavailable")
    std::string    status = "proposed";   // proposed | kept | promoted | dismissed | superseded
};
nlohmann::json to_json(const Candidate&);
Candidate      candidate_from_json(const nlohmann::json&);
bool load_candidate(const std::filesystem::path& project_dir, const std::string& id, Candidate& out,
                    std::string* err = nullptr);
bool save_candidate(const std::filesystem::path& project_dir, const Candidate& c, std::string* err = nullptr);
std::vector<Candidate> list_candidates(const std::filesystem::path& project_dir);   // sorted by `created`

// --- reviews (ADR-0061 §5, ADR-0064 §3/§4) --------------------------------------------------------
struct Passage { double start_bar = 0.0, end_bar = 0.0; };   // 0,0 = the whole excerpt
struct Resolution {
    std::string choice;      // "use" | "keep" | "revise" | "dismiss" | "neither"
    std::string candidate;   // the candidate the choice applies to ("" for neither)
    std::string at;
    std::string feedback;    // feedback id of the decision line
};
struct Review {
    int                      schema = kSchema;
    std::string              id;
    std::string              created;
    std::string              question;      // ONE focused question
    std::string              baseline;      // version id the candidates are compared against
    std::vector<std::string> candidates;    // candidate ids (start with at most two)
    Passage                  passage;
    std::string              status = "open";   // open | resolved
    Resolution               resolution;
};
nlohmann::json to_json(const Review&);
Review         review_from_json(const nlohmann::json&);
bool load_review(const std::filesystem::path& project_dir, const std::string& id, Review& out,
                 std::string* err = nullptr);
bool save_review(const std::filesystem::path& project_dir, const Review& r, std::string* err = nullptr);
std::vector<Review> list_reviews(const std::filesystem::path& project_dir);   // sorted by `created`

// --- feedback + events (append-only) --------------------------------------------------------------
struct Feedback {
    std::string    id;
    std::string    at;
    std::string    review;          // review id ("" for free comments)
    std::string    version;         // the exact version the comment targets
    std::string    candidate;       // optional
    nlohmann::json passage;         // optional {"bar": 25} or {"sec": 12.5}
    nlohmann::json object;          // optional project object {"track":..,"node":..}
    std::string    text;            // the creator's original wording — never rewritten
    std::string    kind;            // "comment" | "decision"
    nlohmann::json interpretation;  // optional structured reading (agent-written, shown as such)
    std::string    dedup_key;       // a retried write with the same key is a no-op
};
nlohmann::json to_json(const Feedback&);
Feedback       feedback_from_json(const nlohmann::json&);
// Appends unless a line with the same non-empty dedup_key exists; returns false only on I/O error.
// `appended` (optional) tells the caller whether a line was actually written.
bool append_feedback(const std::filesystem::path& project_dir, const Feedback& f, bool* appended = nullptr,
                     std::string* err = nullptr);
std::vector<Feedback> read_feedback(const std::filesystem::path& project_dir);

struct Event { std::string at, type; nlohmann::json data; };
bool append_event(const std::filesystem::path& project_dir, const Event& e, std::string* err = nullptr);
std::vector<Event> read_events(const std::filesystem::path& project_dir);
// Events after the last "review_visited" event (or all, if none) — the "since your last review" feed.
std::vector<Event> events_since_last_visit(const std::vector<Event>& all);

// Tolerant JSON-Lines reader: skips blank lines and a truncated/corrupt trailing line (a crash mid-
// append must not make the whole history unreadable). Exposed for the tests.
std::vector<nlohmann::json> read_jsonl(const std::filesystem::path& file);

}  // namespace vivid::work
