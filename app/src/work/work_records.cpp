#include "work/work_records.h"
#include "work/sha256.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vivid::work {

namespace fs = std::filesystem;
using nlohmann::json;

// --- paths ---------------------------------------------------------------------------------------
fs::path work_dir(const fs::path& project_dir) { return project_dir / "work"; }
fs::path version_dir(const fs::path& project_dir, const std::string& id) { return work_dir(project_dir) / "versions" / id; }
fs::path media_dir(const fs::path& project_dir, const std::string& candidate_id) { return work_dir(project_dir) / "media" / candidate_id; }

static fs::path brief_file(const fs::path& p)      { return work_dir(p) / "brief.json"; }
static fs::path brief_log_file(const fs::path& p)  { return work_dir(p) / "brief.log.jsonl"; }
static fs::path candidates_dir(const fs::path& p)  { return work_dir(p) / "candidates"; }
static fs::path reviews_dir(const fs::path& p)     { return work_dir(p) / "reviews"; }
static fs::path feedback_file(const fs::path& p)   { return work_dir(p) / "feedback.jsonl"; }
static fs::path events_file(const fs::path& p)     { return work_dir(p) / "events.jsonl"; }

std::string iso8601(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}
std::string now_iso8601() {
    return iso8601(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string make_id(const std::string& prefix, const std::string& seed) {
    static std::atomic<unsigned> counter{0};
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char stamp[24];
    std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &tm);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const std::string digest = Sha256::of(seed + "|" + std::to_string(ns) + "|" + std::to_string(counter++));
    return prefix + "-" + stamp + "-" + digest.substr(0, 6);
}

// --- file helpers --------------------------------------------------------------------------------
static bool set_err(std::string* err, const std::string& m) { if (err) *err = m; return false; }

static bool read_text(const fs::path& p, std::string& out, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return set_err(err, "cannot read " + p.string());
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
// Write via a sibling temp file + rename so a crash mid-write never leaves a half record behind.
static bool write_text_atomic(const fs::path& p, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) return set_err(err, "cannot write " + tmp.string());
      f << text;
      if (!f) return set_err(err, "write failed: " + tmp.string()); }
    fs::rename(tmp, p, ec);
    if (ec) return set_err(err, "rename failed: " + p.string() + ": " + ec.message());
    return true;
}
static bool write_json_atomic(const fs::path& p, const json& j, std::string* err) {
    return write_text_atomic(p, j.dump(2) + "\n", err);
}
static bool read_json(const fs::path& p, json& out, std::string* err) {
    std::string text;
    if (!read_text(p, text, err)) return false;
    out = json::parse(text, nullptr, /*allow_exceptions*/false);
    if (out.is_discarded()) return set_err(err, "invalid JSON: " + p.string());
    return true;
}
static bool append_line(const fs::path& p, const json& j, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    // A crash mid-append can leave a truncated last line with no newline; start this record on a
    // fresh line so the fragment corrupts only itself (read_jsonl skips it), not the new record too.
    bool needs_newline = false;
    if (const auto sz = fs::file_size(p, ec); !ec && sz > 0) {
        std::ifstream in(p, std::ios::binary); in.seekg(-1, std::ios::end);
        char last = '\n'; in.get(last); needs_newline = last != '\n';
    }
    std::ofstream f(p, std::ios::binary | std::ios::app);
    if (!f) return set_err(err, "cannot append " + p.string());
    if (needs_newline) f << "\n";
    f << j.dump() << "\n";
    if (!f) return set_err(err, "append failed: " + p.string());
    return true;
}

std::vector<json> read_jsonl(const fs::path& file) {
    std::vector<json> out;
    std::ifstream f(file, std::ios::binary);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;   // a truncated/corrupt line (crash mid-append) is skipped, not fatal
        out.push_back(std::move(j));
    }
    return out;
}

static std::string sha256_file(const fs::path& p, bool* ok) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (ok) *ok = false; return {}; }
    Sha256 h; char buf[1 << 16];
    while (f.read(buf, sizeof buf) || f.gcount() > 0) h.update(buf, static_cast<std::size_t>(f.gcount()));
    if (ok) *ok = true;
    return h.hex_digest();
}

template <class T, class FromJson>
static std::vector<T> list_dir_records(const fs::path& dir, FromJson from, const char* file_name = nullptr) {
    std::vector<T> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        fs::path f = e.path();
        if (file_name) { if (!e.is_directory()) continue; f = f / file_name; }
        else if (f.extension() != ".json") continue;
        json j;
        if (!read_json(f, j, nullptr)) continue;
        out.push_back(from(j));
    }
    std::sort(out.begin(), out.end(), [](const T& a, const T& b) { return a.created < b.created; });
    return out;
}

// --- brief ---------------------------------------------------------------------------------------
json to_json(const Brief& b) {
    json j;
    j["schema"] = b.schema; j["rev"] = b.rev; j["updated"] = b.updated; j["direction"] = b.direction;
    json prot = json::array();
    for (const auto& p : b.protections) {
        json jp; jp["id"] = p.id; jp["text"] = p.text; jp["hard"] = p.hard;
        if (!p.scope.is_null()) jp["scope"] = p.scope;
        prot.push_back(jp);
    }
    j["protections"] = prot;
    json pref = json::array();
    for (const auto& p : b.preferences) pref.push_back({ {"id", p.id}, {"text", p.text} });
    j["preferences"] = pref;
    if (!b.interpretation.is_null()) j["interpretation"] = b.interpretation;
    return j;
}
Brief brief_from_json(const json& j) {
    Brief b;
    b.schema = j.value("schema", kSchema); b.rev = j.value("rev", 0);
    b.updated = j.value("updated", ""); b.direction = j.value("direction", "");
    for (const auto& jp : j.value("protections", json::array())) {
        Protection p; p.id = jp.value("id", ""); p.text = jp.value("text", ""); p.hard = jp.value("hard", true);
        if (jp.contains("scope")) p.scope = jp["scope"];
        b.protections.push_back(p);
    }
    for (const auto& jp : j.value("preferences", json::array()))
        b.preferences.push_back({ jp.value("id", ""), jp.value("text", "") });
    if (j.contains("interpretation")) b.interpretation = j["interpretation"];
    return b;
}
bool load_brief(const fs::path& project_dir, Brief& out, std::string* err) {
    json j;
    if (!read_json(brief_file(project_dir), j, err)) return false;
    out = brief_from_json(j);
    return true;
}
bool save_brief(const fs::path& project_dir, Brief& brief, std::string* err) {
    brief.rev += 1;
    brief.updated = now_iso8601();
    const json j = to_json(brief);
    if (!write_json_atomic(brief_file(project_dir), j, err)) return false;
    return append_line(brief_log_file(project_dir), j, err);
}

// --- versions ------------------------------------------------------------------------------------
json to_json(const Version& v) {
    json j;
    j["schema"] = v.schema; j["id"] = v.id; j["parent"] = v.parent; j["created"] = v.created;
    j["label"] = v.label; j["source"] = v.source; j["brief_rev"] = v.brief_rev;
    j["document_sha256"] = v.document_sha256;
    json deps = json::array();
    for (const auto& d : v.dependencies) deps.push_back({ {"path", d.path}, {"sha256", d.sha256}, {"kind", d.kind} });
    j["dependencies"] = deps;
    json plugs = json::array();
    for (const auto& p : v.plugins)
        plugs.push_back({ {"track", p.track}, {"name", p.name}, {"uid", p.uid}, {"format", p.format},
                          {"state_present", p.state_present} });
    j["plugins"] = plugs;
    return j;
}
Version version_from_json(const json& j) {
    Version v;
    v.schema = j.value("schema", kSchema); v.id = j.value("id", ""); v.parent = j.value("parent", "");
    v.created = j.value("created", ""); v.label = j.value("label", ""); v.source = j.value("source", "");
    v.brief_rev = j.value("brief_rev", 0); v.document_sha256 = j.value("document_sha256", "");
    for (const auto& d : j.value("dependencies", json::array()))
        v.dependencies.push_back({ d.value("path", ""), d.value("sha256", ""), d.value("kind", "") });
    for (const auto& p : j.value("plugins", json::array())) {
        PluginDependency pd; pd.track = p.value("track", -1); pd.name = p.value("name", "");
        pd.uid = p.value("uid", ""); pd.format = p.value("format", ""); pd.state_present = p.value("state_present", false);
        v.plugins.push_back(pd);
    }
    return v;
}
bool load_version(const fs::path& project_dir, const std::string& id, Version& out, std::string* err) {
    json j;
    if (!read_json(version_dir(project_dir, id) / "version.json", j, err)) return false;
    out = version_from_json(j);
    return true;
}
bool save_version(const fs::path& project_dir, const Version& v, std::string* err) {
    if (v.id.empty()) return set_err(err, "version has no id");
    return write_json_atomic(version_dir(project_dir, v.id) / "version.json", to_json(v), err);
}
std::vector<Version> list_versions(const fs::path& project_dir) {
    return list_dir_records<Version>(work_dir(project_dir) / "versions", version_from_json, "version.json");
}

bool is_build_output(const fs::path& rel) {
    static const char* kExt[] = { ".dylib", ".so", ".dll", ".o", ".obj", ".tmp" };
    const std::string ext = rel.extension().string();
    for (const char* e : kExt) if (ext == e) return true;
    for (const auto& part : rel) if (part == ".dSYM" || part.extension() == ".dSYM" || part == ".DS_Store") return true;
    return false;
}

static std::string rel_slashes(const fs::path& rel) {
    std::string s = rel.generic_string();
    return s;
}

// The document's plugin identities: every audio-graph node with a plugin path/uid, per track.
static std::vector<PluginDependency> plugins_from_document(const json& doc) {
    std::vector<PluginDependency> out;
    if (!doc.contains("tracks") || !doc["tracks"].is_array()) return out;
    int t = 0;
    for (const auto& jt : doc["tracks"]) {
        if (jt.contains("audio_graph") && jt["audio_graph"].contains("nodes"))
            for (const auto& jn : jt["audio_graph"]["nodes"]) {
                if (!jn.contains("path")) continue;
                PluginDependency p; p.track = t;
                p.name = jn.value("op", ""); p.uid = jn.value("uid", "");
                const int src = jn.value("src", 0);
                p.format = src == 1 ? "vst3" : src == 2 ? "clap" : src == 3 ? "sampler" : "";
                p.state_present = jn.contains("state") && !jn["state"].get<std::string>().empty();
                out.push_back(p);
            }
        ++t;
    }
    return out;
}

std::string snapshot_version(const fs::path& project_dir, const SnapshotOptions& opt, std::string* err) {
    std::error_code ec;
    const fs::path doc_src = project_dir / "project.json";
    if (!fs::is_regular_file(doc_src, ec)) { set_err(err, "no project.json in " + project_dir.string()); return {}; }
    std::string doc_text;
    if (!read_text(doc_src, doc_text, err)) return {};
    const json doc = json::parse(doc_text, nullptr, false);
    if (doc.is_discarded()) { set_err(err, "project.json is not valid JSON"); return {}; }

    Version v;
    v.id = make_id("v", Sha256::of(doc_text) + opt.label);
    v.parent = opt.parent; v.created = now_iso8601(); v.label = opt.label; v.source = opt.source;
    v.brief_rev = opt.brief_rev;
    v.document_sha256 = Sha256::of(doc_text);
    v.plugins = plugins_from_document(doc);

    const fs::path dst = version_dir(project_dir, v.id);
    fs::create_directories(dst, ec);
    if (ec) { set_err(err, "cannot create " + dst.string()); return {}; }

    const fs::path work = work_dir(project_dir);
    for (fs::recursive_directory_iterator it(project_dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p == work) { it.disable_recursion_pending(); continue; }   // never snapshot the records themselves
        const fs::path rel = fs::relative(p, project_dir, ec);
        if (it->is_directory()) {
            if (is_build_output(rel)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || is_build_output(rel)) continue;
        fs::create_directories((dst / rel).parent_path(), ec);
        fs::copy_file(p, dst / rel, fs::copy_options::overwrite_existing, ec);
        if (ec) { set_err(err, "copy failed: " + rel.string() + ": " + ec.message()); return {}; }
        bool ok = true;
        const std::string digest = sha256_file(p, &ok);
        if (!ok) { set_err(err, "cannot hash " + rel.string()); return {}; }
        Dependency d; d.path = rel_slashes(rel); d.sha256 = digest;
        const std::string name = rel.filename().string();
        d.kind = rel == fs::path("project.json") ? "document"
               : name == "vivid-package.json"    ? "package_manifest"
               : (rel.extension() == ".cpp" || rel.extension() == ".h" || rel.extension() == ".mm") ? "package_source"
               : "asset";
        v.dependencies.push_back(d);
    }
    if (ec) { set_err(err, "walk failed: " + ec.message()); return {}; }
    std::sort(v.dependencies.begin(), v.dependencies.end(),
              [](const Dependency& a, const Dependency& b) { return a.path < b.path; });
    if (!save_version(project_dir, v, err)) return {};
    return v.id;
}

std::vector<DependencyIssue> check_dependencies(const fs::path& project_dir, const Version& v) {
    std::vector<DependencyIssue> out;
    std::error_code ec;
    for (const auto& d : v.dependencies) {
        const fs::path p = project_dir / fs::path(d.path);
        if (!fs::is_regular_file(p, ec)) { out.push_back({ d.path, "missing" }); continue; }
        bool ok = true;
        const std::string digest = sha256_file(p, &ok);
        if (!ok || digest != d.sha256) out.push_back({ d.path, "changed" });
    }
    return out;
}

bool differs_only_in_document(const fs::path& project_dir, const Version& v, std::vector<DependencyIssue>* issues) {
    std::vector<DependencyIssue> non_doc;
    for (const auto& i : check_dependencies(project_dir, v))
        if (i.path != "project.json") non_doc.push_back(i);
    // Files present in the live folder but absent from the version would be DELETED by a full
    // promotion; a document-only promotion leaves them alone, so they are not a difference here.
    if (issues) *issues = non_doc;
    return non_doc.empty();
}

// --- candidates ----------------------------------------------------------------------------------
json to_json(const Candidate& c) {
    json j;
    j["schema"] = c.schema; j["id"] = c.id; j["version"] = c.version; j["baseline"] = c.baseline;
    j["purpose"] = c.purpose; j["brief_rev"] = c.brief_rev; j["created"] = c.created;
    j["provenance"] = c.provenance.is_null() ? json::object() : c.provenance;
    json media = json::array();
    for (const auto& m : c.media) {
        json jm; jm["path"] = m.path; jm["kind"] = m.kind;
        jm["recipe"] = m.recipe.is_null() ? json::object() : m.recipe;
        media.push_back(jm);
    }
    j["evidence"] = { {"media", media},
                      {"technical", c.technical.is_null() ? json::array() : c.technical},
                      {"intent",    c.intent.is_null()    ? json::array() : c.intent} };
    j["status"] = c.status;
    return j;
}
Candidate candidate_from_json(const json& j) {
    Candidate c;
    c.schema = j.value("schema", kSchema); c.id = j.value("id", ""); c.version = j.value("version", "");
    c.baseline = j.value("baseline", ""); c.purpose = j.value("purpose", ""); c.brief_rev = j.value("brief_rev", 0);
    c.created = j.value("created", ""); c.status = j.value("status", "proposed");
    if (j.contains("provenance")) c.provenance = j["provenance"];
    if (j.contains("evidence")) {
        const auto& e = j["evidence"];
        for (const auto& jm : e.value("media", json::array())) {
            Media m; m.path = jm.value("path", ""); m.kind = jm.value("kind", "");
            if (jm.contains("recipe")) m.recipe = jm["recipe"];
            c.media.push_back(m);
        }
        if (e.contains("technical")) c.technical = e["technical"];
        if (e.contains("intent"))    c.intent    = e["intent"];
    }
    return c;
}
bool load_candidate(const fs::path& project_dir, const std::string& id, Candidate& out, std::string* err) {
    json j;
    if (!read_json(candidates_dir(project_dir) / (id + ".json"), j, err)) return false;
    out = candidate_from_json(j);
    return true;
}
bool save_candidate(const fs::path& project_dir, const Candidate& c, std::string* err) {
    if (c.id.empty()) return set_err(err, "candidate has no id");
    return write_json_atomic(candidates_dir(project_dir) / (c.id + ".json"), to_json(c), err);
}
std::vector<Candidate> list_candidates(const fs::path& project_dir) {
    return list_dir_records<Candidate>(candidates_dir(project_dir), candidate_from_json);
}

// --- reviews -------------------------------------------------------------------------------------
json to_json(const Review& r) {
    json j;
    j["schema"] = r.schema; j["id"] = r.id; j["created"] = r.created; j["question"] = r.question;
    j["baseline"] = r.baseline; j["candidates"] = r.candidates;
    j["passage"] = { {"start_bar", r.passage.start_bar}, {"end_bar", r.passage.end_bar} };
    j["status"] = r.status;
    if (!r.resolution.choice.empty())
        j["resolution"] = { {"choice", r.resolution.choice}, {"candidate", r.resolution.candidate},
                            {"at", r.resolution.at}, {"feedback", r.resolution.feedback} };
    return j;
}
Review review_from_json(const json& j) {
    Review r;
    r.schema = j.value("schema", kSchema); r.id = j.value("id", ""); r.created = j.value("created", "");
    r.question = j.value("question", ""); r.baseline = j.value("baseline", "");
    for (const auto& c : j.value("candidates", json::array())) if (c.is_string()) r.candidates.push_back(c.get<std::string>());
    if (j.contains("passage")) { r.passage.start_bar = j["passage"].value("start_bar", 0.0); r.passage.end_bar = j["passage"].value("end_bar", 0.0); }
    r.status = j.value("status", "open");
    if (j.contains("resolution")) {
        const auto& s = j["resolution"];
        r.resolution.choice = s.value("choice", ""); r.resolution.candidate = s.value("candidate", "");
        r.resolution.at = s.value("at", ""); r.resolution.feedback = s.value("feedback", "");
    }
    return r;
}
bool load_review(const fs::path& project_dir, const std::string& id, Review& out, std::string* err) {
    json j;
    if (!read_json(reviews_dir(project_dir) / (id + ".json"), j, err)) return false;
    out = review_from_json(j);
    return true;
}
bool save_review(const fs::path& project_dir, const Review& r, std::string* err) {
    if (r.id.empty()) return set_err(err, "review has no id");
    return write_json_atomic(reviews_dir(project_dir) / (r.id + ".json"), to_json(r), err);
}
std::vector<Review> list_reviews(const fs::path& project_dir) {
    return list_dir_records<Review>(reviews_dir(project_dir), review_from_json);
}

// --- feedback + events ---------------------------------------------------------------------------
json to_json(const Feedback& f) {
    json j;
    j["id"] = f.id; j["at"] = f.at; j["review"] = f.review; j["version"] = f.version;
    if (!f.candidate.empty()) j["candidate"] = f.candidate;
    if (!f.passage.is_null()) j["passage"] = f.passage;
    if (!f.object.is_null())  j["object"]  = f.object;
    j["text"] = f.text; j["kind"] = f.kind;
    if (!f.interpretation.is_null()) j["interpretation"] = f.interpretation;
    if (!f.dedup_key.empty()) j["dedup_key"] = f.dedup_key;
    return j;
}
Feedback feedback_from_json(const json& j) {
    Feedback f;
    f.id = j.value("id", ""); f.at = j.value("at", ""); f.review = j.value("review", "");
    f.version = j.value("version", ""); f.candidate = j.value("candidate", "");
    if (j.contains("passage")) f.passage = j["passage"];
    if (j.contains("object"))  f.object  = j["object"];
    f.text = j.value("text", ""); f.kind = j.value("kind", "comment");
    if (j.contains("interpretation")) f.interpretation = j["interpretation"];
    f.dedup_key = j.value("dedup_key", "");
    return f;
}
bool append_feedback(const fs::path& project_dir, const Feedback& f, bool* appended, std::string* err) {
    if (appended) *appended = false;
    if (!f.dedup_key.empty())
        for (const auto& j : read_jsonl(feedback_file(project_dir)))
            if (j.value("dedup_key", "") == f.dedup_key) return true;   // a retry: already recorded
    if (!append_line(feedback_file(project_dir), to_json(f), err)) return false;
    if (appended) *appended = true;
    return true;
}
std::vector<Feedback> read_feedback(const fs::path& project_dir) {
    std::vector<Feedback> out;
    for (const auto& j : read_jsonl(feedback_file(project_dir))) out.push_back(feedback_from_json(j));
    return out;
}

bool append_event(const fs::path& project_dir, const Event& e, std::string* err) {
    json j; j["at"] = e.at.empty() ? now_iso8601() : e.at; j["type"] = e.type;
    if (!e.data.is_null()) j["data"] = e.data;
    return append_line(events_file(project_dir), j, err);
}
std::vector<Event> read_events(const fs::path& project_dir) {
    std::vector<Event> out;
    for (const auto& j : read_jsonl(events_file(project_dir))) {
        Event e; e.at = j.value("at", ""); e.type = j.value("type", "");
        if (j.contains("data")) e.data = j["data"];
        out.push_back(e);
    }
    return out;
}
std::vector<Event> events_since_last_visit(const std::vector<Event>& all) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < all.size(); ++i) if (all[i].type == "review_visited") start = i + 1;
    return std::vector<Event>(all.begin() + static_cast<std::ptrdiff_t>(start), all.end());
}

}  // namespace vivid::work
