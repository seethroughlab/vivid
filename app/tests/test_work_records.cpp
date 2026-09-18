// Headless tests for the ADR-0061/0062 work-records layer (app/src/work/): the durable creative
// memory under <project>/work/. Pure JSON + filesystem, so it runs anywhere. Covers: SHA-256 against
// the FIPS vectors; every record's JSON round-trip; brief versioning (rev bump + append-only log);
// version snapshots (copy everything but work/ and build outputs, hash each file, read plugin
// identities); dependency checks (missing / changed / document-only); feedback dedup; tolerant JSONL
// (a truncated trailing line is skipped, not fatal); "since your last review"; and that the
// preferred_version pointer survives the undo canonical projection (it is DOCUMENT state).
#include "work/work_records.h"
#include "work/sha256.h"
#include "persist_undo.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace vivid::work;
using nlohmann::json;

static fs::path fresh_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / ("vivid_test_work_records_" + std::string(name));
    std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
    return d;
}
static void write_file(const fs::path& p, const std::string& text) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc); f << text;
}
static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary); std::string s((std::istreambuf_iterator<char>(f)), {}); return s;
}

// A minimal project.json with one track carrying a VST3 node (path+uid+state) and one without.
static const char* kDoc = R"({
  "version": 3, "window": {"w": 1280, "h": 800},
  "tracks": [
    {"name": "Lead", "audio_graph": {"nodes": [
        {"id": 1, "kind": 0, "op": "Surge XT", "src": 1, "path": "/Library/Audio/Plug-Ins/VST3/Surge XT.vst3",
         "uid": "ABCD", "state": "b64=="},
        {"id": 2, "kind": 2, "op": "Output"}]}},
    {"name": "Drums", "audio_graph": {"nodes": [{"id": 1, "kind": 0, "op": "Sampler", "src": 3, "path": "kit.wav"}]}}
  ],
  "graph": {"view": {"ox": 1, "oy": 2, "scale": 1.5}, "nodes": []}
})";

static void test_sha256_vectors() {
    CHECK(Sha256::of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56 bytes: exercises the padding boundary (one extra block).
    CHECK(Sha256::of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Streaming in odd chunks must equal one-shot.
    Sha256 h; const std::string m(1000, 'a');
    for (std::size_t i = 0; i < m.size(); i += 7) h.update(m.data() + i, std::min<std::size_t>(7, m.size() - i));
    CHECK(h.hex_digest() == Sha256::of(m));
}

static void test_ids_and_time() {
    const std::string a = make_id("v", "same"), b = make_id("v", "same");
    CHECK(a != b);                       // unique within one second
    CHECK(a.rfind("v-", 0) == 0);
    CHECK(a.size() == 2 + 16 + 1 + 6);   // "v-" + YYYYMMDDTHHMMSSZ + "-" + 6 hex
    CHECK(iso8601(0) == "1970-01-01T00:00:00Z");
    CHECK(now_iso8601().size() == 20);
}

static void test_brief_roundtrip_and_log() {
    const fs::path proj = fresh_dir("brief");
    Brief b; b.direction = "Keep the drums and bass. Make the chorus feel more expansive.";
    b.protections.push_back({ "p1", "Keep the drums and bass", true, json{{"tracks", {"Drums", "Bass"}}} });
    b.preferences.push_back({ "q1", "chorus more expansive; visuals open up with it" });
    b.interpretation = json{{"focus", "chorus"}};
    CHECK(save_brief(proj, b));
    CHECK(b.rev == 1);
    CHECK(!b.updated.empty());
    Brief l; CHECK(load_brief(proj, l));
    CHECK(l.rev == 1);
    CHECK(l.direction == b.direction);
    CHECK(l.protections.size() == 1 && l.protections[0].hard && l.protections[0].scope["tracks"][1] == "Bass");
    CHECK(l.preferences.size() == 1 && l.preferences[0].id == "q1");
    CHECK(l.interpretation["focus"] == "chorus");
    // A second save bumps rev and APPENDS to the log; the first revision is still there.
    l.direction += " Give me two directions.";
    CHECK(save_brief(proj, l));
    CHECK(l.rev == 2);
    const auto log = read_jsonl(work_dir(proj) / "brief.log.jsonl");
    CHECK(log.size() == 2);
    CHECK(log[0]["rev"] == 1 && log[1]["rev"] == 2);
    CHECK(log[0]["direction"].get<std::string>().find("two directions") == std::string::npos);
    // Missing brief: load fails cleanly with a message.
    std::string err; Brief none;
    CHECK(!load_brief(fresh_dir("nobrief"), none, &err));
    CHECK(!err.empty());
}

static void test_snapshot_version() {
    const fs::path proj = fresh_dir("snapshot");
    write_file(proj / "project.json", kDoc);
    write_file(proj / "aurora.glsl", "// shader v1");
    write_file(proj / "aurora_field.cpp", "// op source");
    write_file(proj / "vivid-package.json", "{}");
    write_file(proj / "AuroraField.dylib", "MACH-O");             // build output: excluded
    write_file(proj / "media" / "kit.wav", "RIFF....");
    write_file(proj / "work" / "brief.json", "{\"rev\":1}");       // the records themselves: excluded
    write_file(proj / ".DS_Store", "junk");                       // excluded

    std::string err;
    SnapshotOptions opt; opt.label = "baseline"; opt.source = "manual"; opt.brief_rev = 1;
    const std::string id = snapshot_version(proj, opt, &err);
    CHECK(!id.empty());
    CHECK(err.empty());
    Version v; CHECK(load_version(proj, id, v));
    CHECK(v.id == id && v.label == "baseline" && v.source == "manual" && v.brief_rev == 1 && v.parent.empty());
    CHECK(v.document_sha256 == Sha256::of(kDoc));
    // Copied: document + 4 co-located files. Not copied: dylib, work/, .DS_Store.
    const fs::path vd = version_dir(proj, id);
    CHECK(fs::exists(vd / "project.json"));
    CHECK(fs::exists(vd / "aurora.glsl"));
    CHECK(fs::exists(vd / "media" / "kit.wav"));
    CHECK(!fs::exists(vd / "AuroraField.dylib"));
    CHECK(!fs::exists(vd / "work"));
    CHECK(!fs::exists(vd / ".DS_Store"));
    CHECK(v.dependencies.size() == 5);
    // Kinds + hashes.
    int doc = 0, src = 0, manifest = 0, asset = 0;
    for (const auto& d : v.dependencies) {
        CHECK(d.sha256.size() == 64);
        if (d.kind == "document") { ++doc; CHECK(d.path == "project.json"); }
        else if (d.kind == "package_source") ++src;
        else if (d.kind == "package_manifest") ++manifest;
        else if (d.kind == "asset") ++asset;
    }
    CHECK(doc == 1 && src == 1 && manifest == 1 && asset == 2);
    CHECK(v.dependencies.front().path <= v.dependencies.back().path);   // sorted by path
    // Plugin identities read from the document.
    CHECK(v.plugins.size() == 2);
    CHECK(v.plugins[0].track == 0 && v.plugins[0].format == "vst3" && v.plugins[0].uid == "ABCD" && v.plugins[0].state_present);
    CHECK(v.plugins[1].track == 1 && v.plugins[1].format == "sampler" && !v.plugins[1].state_present);
    // A second snapshot with the first as parent gets a different id and lists in creation order.
    SnapshotOptions opt2; opt2.label = "candidate A"; opt2.source = "candidate"; opt2.parent = id;
    const std::string id2 = snapshot_version(proj, opt2, &err);
    CHECK(!id2.empty() && id2 != id);
    const auto all = list_versions(proj);
    CHECK(all.size() == 2);
    CHECK(all[1].parent == id || all[0].parent == id);
    // No project.json → clean failure.
    CHECK(snapshot_version(fresh_dir("empty"), opt, &err).empty());
    CHECK(err.find("project.json") != std::string::npos);
    CHECK(is_build_output("x/y.dylib") && is_build_output("Foo.dSYM/Contents/Info.plist") && !is_build_output("a/b.cpp"));
}

static void test_dependency_checks() {
    const fs::path proj = fresh_dir("deps");
    write_file(proj / "project.json", kDoc);
    write_file(proj / "aurora.glsl", "// shader v1");
    write_file(proj / "kit.wav", "RIFF");
    SnapshotOptions opt; opt.label = "baseline";
    const std::string id = snapshot_version(proj, opt);
    Version v; CHECK(load_version(proj, id, v));
    CHECK(check_dependencies(proj, v).empty());                 // pristine folder reproduces the version
    CHECK(differs_only_in_document(proj, v));
    // Edit only the document: still document-only.
    write_file(proj / "project.json", std::string(kDoc) + "\n");
    auto issues = check_dependencies(proj, v);
    CHECK(issues.size() == 1 && issues[0].path == "project.json" && issues[0].status == "changed");
    std::vector<DependencyIssue> non_doc;
    CHECK(differs_only_in_document(proj, v, &non_doc));
    CHECK(non_doc.empty());
    // Change a shared asset: the version is no longer document-only (gate-1 promotion must refuse).
    write_file(proj / "aurora.glsl", "// shader v2");
    CHECK(!differs_only_in_document(proj, v, &non_doc));
    CHECK(non_doc.size() == 1 && non_doc[0].path == "aurora.glsl" && non_doc[0].status == "changed");
    // Delete an asset: reported as missing, never silently substituted.
    fs::remove(proj / "kit.wav");
    issues = check_dependencies(proj, v);
    bool saw_missing = false;
    for (const auto& i : issues) if (i.path == "kit.wav" && i.status == "missing") saw_missing = true;
    CHECK(saw_missing);
}

static void test_candidate_and_review_roundtrip() {
    const fs::path proj = fresh_dir("cand");
    Candidate c; c.id = make_id("c", "A"); c.version = "v-1"; c.baseline = "v-0"; c.purpose = "expansive chorus";
    c.brief_rev = 2; c.created = now_iso8601();
    c.provenance = json{{"runner", "claude"}, {"run", "run-1"}, {"summary", "widened the pad + opened the field"}};
    c.media.push_back({ "work/media/" + c.id + "/chorus.mp4", "av",
                        json{{"scene", 2}, {"start_bar", 17}, {"bars", 8}, {"fps", 60}, {"warmup_sec", 2.0}} });
    c.technical = json::array({ json{{"check", "peak"}, {"ok", true}} });
    c.intent = json::array({ json{{"evaluator", "unavailable"}} });
    CHECK(save_candidate(proj, c));
    Candidate l; CHECK(load_candidate(proj, c.id, l));
    CHECK(l.id == c.id && l.version == "v-1" && l.baseline == "v-0" && l.brief_rev == 2 && l.status == "proposed");
    CHECK(l.provenance["run"] == "run-1");
    CHECK(l.media.size() == 1 && l.media[0].kind == "av" && l.media[0].recipe["start_bar"] == 17);
    CHECK(l.technical[0]["ok"] == true && l.intent[0]["evaluator"] == "unavailable");
    l.status = "kept"; CHECK(save_candidate(proj, l));
    CHECK(list_candidates(proj).size() == 1 && list_candidates(proj)[0].status == "kept");
    // A candidate with no id is refused.
    Candidate bad; std::string err; CHECK(!save_candidate(proj, bad, &err)); CHECK(!err.empty());

    Review r; r.id = make_id("r", "q"); r.created = now_iso8601(); r.question = "Which chorus opens up better?";
    r.baseline = "v-0"; r.candidates = { c.id, "c-other" }; r.passage = { 17, 25 };
    CHECK(save_review(proj, r));
    Review lr; CHECK(load_review(proj, r.id, lr));
    CHECK(lr.status == "open" && lr.resolution.choice.empty() && lr.candidates.size() == 2);
    CHECK(lr.passage.start_bar == 17 && lr.passage.end_bar == 25);
    lr.status = "resolved"; lr.resolution = { "use", c.id, now_iso8601(), "f-1" };
    CHECK(save_review(proj, lr));
    Review lr2; CHECK(load_review(proj, r.id, lr2));
    CHECK(lr2.status == "resolved" && lr2.resolution.choice == "use" && lr2.resolution.candidate == c.id);
    CHECK(list_reviews(proj).size() == 1);
}

static void test_feedback_dedup_and_events() {
    const fs::path proj = fresh_dir("fb");
    Feedback f; f.id = "f-1"; f.at = now_iso8601(); f.review = "r-1"; f.version = "v-1"; f.candidate = "c-1";
    f.passage = json{{"bar", 25}}; f.text = "Keep this opening, but the particles get too busy."; f.kind = "comment";
    f.dedup_key = "client-abc";
    bool appended = false;
    CHECK(append_feedback(proj, f, &appended)); CHECK(appended);
    CHECK(append_feedback(proj, f, &appended)); CHECK(!appended);   // the retry is a no-op
    Feedback g = f; g.id = "f-2"; g.dedup_key = ""; g.kind = "decision"; g.text = "use";
    CHECK(append_feedback(proj, g, &appended)); CHECK(appended);      // no key: always appended
    const auto all = read_feedback(proj);
    CHECK(all.size() == 2);
    CHECK(all[0].text == f.text && all[0].passage["bar"] == 25 && all[0].kind == "comment");
    CHECK(all[1].kind == "decision");

    CHECK(append_event(proj, { "", "review_opened", json{{"review", "r-1"}} }));
    CHECK(append_event(proj, { "", "review_visited", json() }));
    CHECK(append_event(proj, { "", "promoted", json{{"candidate", "c-1"}} }));
    CHECK(append_event(proj, { "", "kept", json{{"candidate", "c-2"}} }));
    const auto ev = read_events(proj);
    CHECK(ev.size() == 4 && !ev[0].at.empty());
    const auto since = events_since_last_visit(ev);
    CHECK(since.size() == 2 && since[0].type == "promoted" && since[1].type == "kept");
    CHECK(events_since_last_visit({}).empty());
}

static void test_jsonl_tolerates_truncation() {
    const fs::path proj = fresh_dir("jsonl");
    const fs::path f = work_dir(proj) / "feedback.jsonl";
    write_file(f, "{\"id\":\"f-1\",\"text\":\"a\"}\n\n{\"id\":\"f-2\",\"text\":\"b\"}\n{\"id\":\"f-3\",\"tex");
    const auto rows = read_jsonl(f);
    CHECK(rows.size() == 2);   // blank line skipped; truncated tail skipped, earlier history intact
    CHECK(rows[1]["id"] == "f-2");
    // An append after the truncation still lands on its own line.
    Feedback x; x.id = "f-4"; x.text = "c";
    CHECK(append_feedback(proj, x));
    CHECK(read_feedback(proj).size() == 3);
    CHECK(read_jsonl(work_dir(proj) / "does_not_exist.jsonl").empty());
}

static void test_preferred_version_is_document_state() {
    // The undo canonical projection strips view/performance state; preferred_version must SURVIVE
    // it (so undo of a promotion restores the pointer with the contents), and absence stays absent.
    json doc = json::parse(kDoc);
    doc["preferred_version"] = "v-20260918T000000Z-abc123";
    const json proj = vivid::canonical_document_projection(doc);
    CHECK(proj.contains("preferred_version") && proj["preferred_version"] == "v-20260918T000000Z-abc123");
    CHECK(!proj.contains("window"));                       // sanity: the projection still strips view state
    json without = json::parse(kDoc);
    CHECK(!vivid::canonical_document_projection(without).contains("preferred_version"));
    // Two documents differing only in the pointer are DIFFERENT documents (promotion is an edit).
    CHECK(vivid::canonical_document_projection(doc) != vivid::canonical_document_projection(without));
}

int main() {
    test_sha256_vectors();
    test_ids_and_time();
    test_brief_roundtrip_and_log();
    test_snapshot_version();
    test_dependency_checks();
    test_candidate_and_review_roundtrip();
    test_feedback_dedup_and_events();
    test_jsonl_tolerates_truncation();
    test_preferred_version_is_document_state();
    return vivid::test::summary("test_work_records");
}
