// Tests for graph file I/O: example entry parsing, path resolution, tilde expansion.
#include "runtime/control/graph_file_io.h"
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
            "schema_version": 3,
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
            "schema_version": 3,
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

int main() {
    std::fprintf(stderr, "=== test_graph_file_io ===\n");

    test_expand_tilde();
    test_load_example_entry();
    test_load_example_entry_missing();
    test_resolve_graph_input_path();
    test_save_load_meta_roundtrip();

    std::fprintf(stderr, "\n=== %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
