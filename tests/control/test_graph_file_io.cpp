// Tests for graph file I/O: example entry parsing, path resolution, tilde expansion.
#include "runtime/control/graph_file_io.h"
#include "runtime/graph/graph.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include "test_helpers.h"

namespace fs = std::filesystem;

static void test_expand_tilde() {
    std::fprintf(stderr, "\n--- expand_tilde_path ---\n");

    ScopedTempDir tmp("graph_file_io_home");
    fs::path home_root = tmp / "home";
    std::filesystem::create_directories(home_root);
    ScopedEnvVar home_env("HOME", home_root.string());

    auto result = vivid::expand_tilde_path("~/Documents");
    check(result == home_root / "Documents", "~/Documents expands correctly");

    auto abs = vivid::expand_tilde_path("/absolute/path");
    check(abs == "/absolute/path", "absolute path unchanged");

    auto empty = vivid::expand_tilde_path("");
    check(empty.empty(), "empty string returns empty");

    ScopedEnvVar unset_home("HOME", nullptr);
    auto unresolved = vivid::expand_tilde_path("~/Documents");
    check(unresolved == "~/Documents", "tilde path stays unchanged when HOME is unset");
}

static void test_load_example_entry() {
    std::fprintf(stderr, "\n--- load_example_entry_from_graph ---\n");

    ScopedTempDir tmp("graph_file_io");
    fs::path graphs_root = tmp.path;

    // Create a minimal graph JSON with example metadata
    fs::path graph_path = tmp / "test_graph.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "My Test Graph",
                "description": "A test",
                "tags": ["audio", "demo"],
                "difficulty": "beginner",
                "envs": ["audio"]
            },
            "nodes": [],
            "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, graphs_root, entry);
    check(ok, "load succeeds for valid graph");
    check(entry.title == "My Test Graph", "title parsed");
    check(entry.difficulty == "beginner", "difficulty parsed");
    check(entry.tags.size() == 2, "tags parsed");
}

static void test_load_example_entry_missing() {
    std::fprintf(stderr, "\n--- load_example_entry: missing file ---\n");

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph("/nonexistent.json", "/tmp", entry);
    check(!ok, "load fails for missing file");
}

static void test_resolve_graph_input_path() {
    std::fprintf(stderr, "\n--- resolve_graph_input_path ---\n");

    ScopedTempDir tmp("resolve_path");
    fs::path graphs_root = tmp.path;

    // Create a graph file
    fs::path graph_path = tmp / "my_graph.json";
    { std::ofstream ofs(graph_path); ofs << "{}"; }

    // Absolute path should return as-is
    std::string resolved = vivid::resolve_graph_input_path(
        graph_path.string(), graphs_root, {});
    check(resolved == graph_path.string(), "absolute path returned as-is");

    // Relative path resolved against graphs_root
    std::string relative = vivid::resolve_graph_input_path(
        "my_graph.json", graphs_root, {});
    check(fs::exists(relative), "relative path resolved to existing file");
}

static void test_save_load_meta_roundtrip() {
    std::fprintf(stderr, "\n--- save/load graph meta round-trip ---\n");

    ScopedTempDir tmp("meta_roundtrip");
    fs::path graph_path = tmp / "roundtrip.json";

    // Create a graph with metadata
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Original Title",
                "description": "Original desc",
                "tags": ["tag1"],
                "difficulty": "intermediate",
                "envs": ["gpu"]
            },
            "nodes": [],
            "connections": []
        })";
    }

    // Load metadata
    vivid::GraphMetaEditData data;
    std::string error;
    bool loaded = vivid::load_graph_meta_edit_data(graph_path.string(), data, error);
    check(loaded, "load metadata succeeds");
    check(data.title == "Original Title", "title loaded");

    // Modify and save
    data.title = "Updated Title";
    data.tags_csv = "tag1, tag2";
    bool saved = vivid::save_graph_meta_edit_data(data, error);
    check(saved, "save metadata succeeds");

    // Reload and verify
    vivid::GraphMetaEditData reloaded;
    bool reloaded_ok = vivid::load_graph_meta_edit_data(graph_path.string(), reloaded, error);
    check(reloaded_ok, "reload metadata succeeds");
    check(reloaded.title == "Updated Title", "title updated after save");
}

static void test_domains_loading() {
    std::fprintf(stderr, "\n--- domains loading ---\n");

    ScopedTempDir tmp("domains_loading");
    fs::path graph_path = tmp / "dom.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Dom Test",
                "domains": ["gpu", "audio"]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds");
    check(entry.domains.size() == 2, "domains has 2 items");
    check(entry.domains[0] == "gpu", "first domain is gpu");
    check(entry.domains[1] == "audio", "second domain is audio");
}

static void test_envs_legacy_loading() {
    std::fprintf(stderr, "\n--- envs legacy loading ---\n");

    ScopedTempDir tmp("envs_legacy");
    fs::path graph_path = tmp / "leg.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Legacy",
                "envs": ["control"]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds with envs");
    check(entry.domains.size() == 1, "domains has 1 item from envs fallback");
    check(entry.domains[0] == "control", "domain is control");
}

static void test_domains_preferred_over_envs() {
    std::fprintf(stderr, "\n--- domains preferred over envs ---\n");

    ScopedTempDir tmp("domains_pref");
    fs::path graph_path = tmp / "both.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Both",
                "domains": ["gpu"],
                "envs": ["audio"]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds");
    check(entry.domains.size() == 1, "domains has 1 item");
    check(entry.domains[0] == "gpu", "domains wins over envs");
}

static void test_save_writes_domains_not_envs() {
    std::fprintf(stderr, "\n--- save writes domains not envs ---\n");

    ScopedTempDir tmp("save_domains");
    fs::path graph_path = tmp / "save_dom.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "SaveTest",
                "envs": ["audio"]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::GraphMetaEditData data;
    std::string error;
    bool loaded = vivid::load_graph_meta_edit_data(graph_path.string(), data, error);
    check(loaded, "load meta edit succeeds");
    check(data.domains_csv == "audio", "domains_csv populated from envs");

    bool saved = vivid::save_graph_meta_edit_data(data, error);
    check(saved, "save succeeds");

    // Re-read raw JSON and verify
    std::ifstream ifs(graph_path);
    auto root = nlohmann::json::parse(ifs);
    check(root["meta"].contains("domains"), "saved JSON has domains");
    check(!root["meta"].contains("envs"), "saved JSON has no envs");
    auto doms = root["meta"]["domains"];
    check(doms.is_array() && doms.size() == 1 && doms[0] == "audio", "domains value correct");
}

static void test_instrument_fields_roundtrip() {
    std::fprintf(stderr, "\n--- instrument fields roundtrip ---\n");

    ScopedTempDir tmp("inst_roundtrip");
    fs::path graph_path = tmp / "inst.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Synth Pad",
                "domains": ["audio"],
                "content_kind": "instrument",
                "category": "synth",
                "family": "subtractive",
                "role": "hero",
                "playability": "midi"
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds");
    check(entry.content_kind == "instrument", "content_kind");
    check(entry.category == "synth", "category");
    check(entry.family == "subtractive", "family");
    check(entry.role == "hero", "role");
    check(entry.playability == "midi", "playability");

    // Load as edit data, modify, save, reload
    vivid::GraphMetaEditData data;
    std::string error;
    bool loaded = vivid::load_graph_meta_edit_data(graph_path.string(), data, error);
    check(loaded, "load meta edit succeeds");
    check(data.content_kind == "instrument", "edit content_kind");

    data.category = "effect";
    bool saved = vivid::save_graph_meta_edit_data(data, error);
    check(saved, "save succeeds");

    vivid::GraphMetaEditData reloaded;
    bool reloaded_ok = vivid::load_graph_meta_edit_data(graph_path.string(), reloaded, error);
    check(reloaded_ok, "reload succeeds");
    check(reloaded.category == "effect", "category updated");
    check(reloaded.content_kind == "instrument", "content_kind preserved");
    check(reloaded.role == "hero", "role preserved");
}

static void test_preview_controls_roundtrip() {
    std::fprintf(stderr, "\n--- preview controls roundtrip ---\n");

    ScopedTempDir tmp("preview_ctrl");
    fs::path graph_path = tmp / "prev.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Preview Test",
                "preview_controls": [
                    {"node": "osc1", "param": "frequency", "label": "Freq"},
                    {"node": "filter1", "param": "cutoff"}
                ]
            },
            "nodes": {
                "osc1": {
                    "type": "TestOp",
                    "params": { "frequency": 220.0 }
                },
                "filter1": {
                    "type": "StringSourceOp",
                    "params": { "cutoff": "warm" }
                }
            },
            "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds");
    check(entry.preview_controls.size() == 2, "2 preview controls");
    check(entry.preview_controls[0].node == "osc1", "first node");
    check(entry.preview_controls[0].param == "frequency", "first param");
    check(entry.preview_controls[0].label == "Freq", "first label");
    check(entry.preview_controls[1].node == "filter1", "second node");
    check(entry.preview_controls[1].label.empty(), "second label empty");
    check(entry.preview_rows.size() == 2, "resolved preview rows populated");
    check(entry.preview_rows[0].label == "Freq", "preview row uses authored label");
    check(entry.preview_rows[0].value == "220.00", "numeric preview row resolved from saved params");
    check(entry.preview_rows[1].label == "cutoff", "preview row falls back to param name");
    check(entry.preview_rows[1].value == "warm", "string preview row resolved from saved string params");

    // Round-trip through meta edit
    vivid::GraphMetaEditData data;
    std::string error;
    vivid::load_graph_meta_edit_data(graph_path.string(), data, error);
    check(!data.preview_controls.empty(), "preview_controls populated");
    vivid::save_graph_meta_edit_data(data, error);

    vivid::ExampleEntry entry2;
    vivid::load_example_entry_from_graph(graph_path, tmp.path, entry2);
    check(entry2.preview_controls.size() == 2, "preview controls preserved after roundtrip");
    check(entry2.preview_rows.size() == 2, "preview rows still resolve after roundtrip");
}

static void test_invalid_preview_controls() {
    std::fprintf(stderr, "\n--- invalid preview controls ---\n");

    ScopedTempDir tmp("inv_preview");
    fs::path graph_path = tmp / "inv.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Invalid Preview",
                "preview_controls": [
                    {"node": "osc1"},
                    "not_an_object",
                    {"node": "osc2", "param": "freq"},
                    {"param": "cutoff"},
                    42
                ]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds despite invalid entries");
    check(entry.preview_controls.size() == 1, "only valid entry kept");
    check(entry.preview_controls[0].node == "osc2", "valid entry node");
    check(entry.preview_controls[0].param == "freq", "valid entry param");
}

static void test_no_instrument_fields() {
    std::fprintf(stderr, "\n--- no instrument fields ---\n");

    ScopedTempDir tmp("no_inst");
    fs::path graph_path = tmp / "plain.json";
    {
        std::ofstream ofs(graph_path);
        ofs << R"({
            "schema_version": 1,
            "meta": {
                "title": "Plain Graph",
                "domains": ["gpu"]
            },
            "nodes": [], "connections": []
        })";
    }

    vivid::ExampleEntry entry;
    bool ok = vivid::load_example_entry_from_graph(graph_path, tmp.path, entry);
    check(ok, "load succeeds");
    check(entry.content_kind.empty(), "content_kind empty by default");
    check(entry.category.empty(), "category empty by default");
    check(entry.family.empty(), "family empty by default");
    check(entry.role.empty(), "role empty by default");
    check(entry.playability.empty(), "playability empty by default");
    check(entry.preview_controls.empty(), "preview_controls empty by default");
    check(entry.package_name.empty(), "package_name empty by default");
}

static void test_graph_core_meta_roundtrip() {
    std::fprintf(stderr, "\n--- graph core meta roundtrip ---\n");

    vivid::Graph graph;
    check(graph.load_from_string(R"({
        "schema_version": 1,
        "meta": {
            "id": "wavetable-pad",
            "title": "Wavetable Pad",
            "description": "Playable pad patch",
            "tags": ["audio", "pad"],
            "difficulty": "intermediate",
            "envs": ["audio", "control"],
            "requires_packages": ["vivid-wavetable"],
            "featured_rank": 9,
            "estimated_minutes": 4,
            "content_kind": "instrument",
            "category": "synth",
            "family": "pads",
            "role": "hero",
            "playability": "midi",
            "preview_controls": [
                {"node": "osc1", "param": "frequency", "label": "Freq"},
                {"node": "osc1", "param": "shape"}
            ]
        },
        "nodes": {
            "osc1": {
                "type": "TestOp",
                "params": {
                    "frequency": 440.0,
                    "shape": 0.5
                }
            }
        },
        "connections": []
    })"), "graph loads with instrument meta");

    const auto& meta = graph.meta();
    check(meta.id == "wavetable-pad", "graph meta id parsed");
    check(meta.domains.size() == 2, "legacy envs mapped to domains");
    check(meta.domains[0] == "audio", "first canonical domain preserved");
    check(meta.preview_controls.size() == 2, "graph preview controls parsed");

    std::string out_json;
    check(graph.save_to_string(out_json), "graph save_to_string succeeds");

    auto saved = nlohmann::json::parse(out_json);
    check(saved.contains("meta"), "saved graph includes meta block");
    check(saved["meta"].contains("domains"), "saved graph writes canonical domains");
    check(!saved["meta"].contains("envs"), "saved graph omits legacy envs");
    check(saved["meta"]["content_kind"] == "instrument", "content_kind preserved on save");
    check(saved["meta"]["category"] == "synth", "category preserved on save");
    check(saved["meta"]["family"] == "pads", "family preserved on save");
    check(saved["meta"]["role"] == "hero", "role preserved on save");
    check(saved["meta"]["playability"] == "midi", "playability preserved on save");
    check(saved["meta"].contains("preview_controls") &&
          saved["meta"]["preview_controls"].is_array() &&
          saved["meta"]["preview_controls"].size() == 2,
          "preview controls preserved on save");
}

int main() {
    std::fprintf(stderr, "=== test_graph_file_io ===\n");

    test_expand_tilde();
    test_load_example_entry();
    test_load_example_entry_missing();
    test_resolve_graph_input_path();
    test_save_load_meta_roundtrip();
    test_domains_loading();
    test_envs_legacy_loading();
    test_domains_preferred_over_envs();
    test_save_writes_domains_not_envs();
    test_instrument_fields_roundtrip();
    test_preview_controls_roundtrip();
    test_invalid_preview_controls();
    test_no_instrument_fields();
    test_graph_core_meta_roundtrip();

    std::fprintf(stderr, "\n=== %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
