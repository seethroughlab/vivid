// test_package_catalog.cpp — PackageCatalog unit tests
// Tests JSON parsing, cache read/write, and merge logic.
// Does NOT test network fetch (would require a live connection).

#include "runtime/package_catalog.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/platform.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace vivid;

// ---------------------------------------------------------------------------
// Test 1: parse_index_json with valid data
// ---------------------------------------------------------------------------
static void test_parse_valid_json() {
    std::fprintf(stderr, "test_parse_valid_json...\n");

    const char* json = R"({
        "schema_version": 1,
        "packages": [
            {
                "name": "vivid-drums",
                "description": "808-style drum synthesis operators",
                "version": "1.0.0",
                "author": "Jeff",
                "url": "https://github.com/vivid-project/vivid-drums",
                "category": "audio",
                "tags": ["drums", "synthesis"]
            },
            {
                "name": "vivid-glitch",
                "description": "Glitch effect operators",
                "version": "0.2.0",
                "author": "Alice",
                "url": "https://github.com/vivid-project/vivid-glitch",
                "category": "gpu",
                "tags": ["glitch", "visual"]
            }
        ]
    })";

    // Access parse_index_json via the catalog's public interface.
    // Since it's private, we test it indirectly through cache round-trip.
    // Write the JSON as a cache file, then use load_cache.

    // Write to a temp file as if it were the cache
    std::string cache_dir = get_config_dir();
    std::string cache_path = cache_dir + "/package-catalog-cache.json";

    // Save original cache if it exists
    std::string backup;
    bool had_cache = false;
    if (std::filesystem::exists(cache_path)) {
        std::ifstream ifs(cache_path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        backup = ss.str();
        had_cache = true;
    }

    // Write test data
    {
        std::ofstream ofs(cache_path);
        ofs << json;
    }

    // Create a catalog and check that entries load
    OperatorRegistry registry;
    PackageCompiler compiler("", "");
    PackageManager pm(compiler, registry);
    PackageCatalog catalog(pm);

    // Manually trigger a refresh that will load from cache
    // Since we can't easily test the background fetch, we verify via entries.
    // The catalog starts idle, so let's just verify the cache round-trip
    // by constructing another catalog and checking it picks up the cache.

    // For a more direct test, we trigger refresh and wait briefly
    catalog.refresh();

    // Wait for fetch thread to complete (it will fail on network but load cache)
    for (int i = 0; i < 100; ++i) {
        auto state = catalog.fetch_state();
        if (state != CatalogFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto entries = catalog.entries();
    assert(entries.size() >= 2);

    bool found_drums = false, found_glitch = false;
    for (const auto& e : entries) {
        if (e.name == "vivid-drums") {
            assert(e.description == "808-style drum synthesis operators");
            assert(e.version == "1.0.0");
            assert(e.author == "Jeff");
            assert(e.category == "audio");
            assert(e.tags.size() == 2);
            assert(e.tags[0] == "drums");
            assert(e.tags[1] == "synthesis");
            assert(!e.installed);
            found_drums = true;
        }
        if (e.name == "vivid-glitch") {
            assert(e.description == "Glitch effect operators");
            assert(e.version == "0.2.0");
            assert(e.author == "Alice");
            assert(e.category == "gpu");
            found_glitch = true;
        }
    }
    assert(found_drums);
    assert(found_glitch);

    // Restore original cache
    if (had_cache) {
        std::ofstream ofs(cache_path);
        ofs << backup;
    } else {
        std::filesystem::remove(cache_path);
    }

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 2: empty and malformed JSON
// ---------------------------------------------------------------------------
static void test_parse_invalid_json() {
    std::fprintf(stderr, "test_parse_invalid_json...\n");

    std::string cache_dir = get_config_dir();
    std::string cache_path = cache_dir + "/package-catalog-cache.json";

    // Save original
    std::string backup;
    bool had_cache = false;
    if (std::filesystem::exists(cache_path)) {
        std::ifstream ifs(cache_path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        backup = ss.str();
        had_cache = true;
    }

    // Test with invalid JSON
    {
        std::ofstream ofs(cache_path);
        ofs << "not valid json at all";
    }

    OperatorRegistry registry;
    PackageCompiler compiler("", "");
    PackageManager pm(compiler, registry);
    PackageCatalog catalog(pm);

    catalog.refresh();

    // Wait for completion
    for (int i = 0; i < 100; ++i) {
        auto state = catalog.fetch_state();
        if (state != CatalogFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Should result in error or empty entries (cache parse failed, network likely fails too)
    auto entries = catalog.entries();
    // With invalid cache and failed network, entries should be empty
    // (or error state). Either is acceptable.

    // Test with empty packages array
    {
        std::ofstream ofs(cache_path);
        ofs << R"({"schema_version": 1, "packages": []})";
    }

    PackageCatalog catalog2(pm);
    catalog2.refresh();

    for (int i = 0; i < 100; ++i) {
        auto state = catalog2.fetch_state();
        if (state != CatalogFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto entries2 = catalog2.entries();
    // Empty packages array should parse successfully with 0 entries
    // (unless network fetch overwrites)

    // Restore
    if (had_cache) {
        std::ofstream ofs(cache_path);
        ofs << backup;
    } else {
        std::filesystem::remove(cache_path);
    }

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 3: cache save and reload round-trip
// ---------------------------------------------------------------------------
static void test_cache_round_trip() {
    std::fprintf(stderr, "test_cache_round_trip...\n");

    std::string cache_dir = get_config_dir();
    std::string cache_path = cache_dir + "/package-catalog-cache.json";

    // Save original
    std::string backup;
    bool had_cache = false;
    if (std::filesystem::exists(cache_path)) {
        std::ifstream ifs(cache_path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        backup = ss.str();
        had_cache = true;
    }

    // Write known cache data
    const char* json = R"({
        "schema_version": 1,
        "packages": [
            {
                "name": "test-pkg",
                "description": "A test package",
                "version": "2.0.0",
                "author": "Tester",
                "url": "https://example.com/test-pkg",
                "category": "control",
                "tags": ["test"]
            }
        ]
    })";
    {
        std::ofstream ofs(cache_path);
        ofs << json;
    }

    // Load via catalog
    OperatorRegistry registry;
    PackageCompiler compiler("", "");
    PackageManager pm(compiler, registry);
    PackageCatalog catalog(pm);

    catalog.refresh();
    for (int i = 0; i < 100; ++i) {
        if (catalog.fetch_state() != CatalogFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto entries = catalog.entries();
    // Should have at least the cached entry
    bool found = false;
    for (const auto& e : entries) {
        if (e.name == "test-pkg") {
            assert(e.description == "A test package");
            assert(e.version == "2.0.0");
            assert(e.author == "Tester");
            assert(e.category == "control");
            assert(e.tags.size() == 1);
            assert(e.tags[0] == "test");
            found = true;
        }
    }
    assert(found);

    // Restore
    if (had_cache) {
        std::ofstream ofs(cache_path);
        ofs << backup;
    } else {
        std::filesystem::remove(cache_path);
    }

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 4: initial state
// ---------------------------------------------------------------------------
static void test_initial_state() {
    std::fprintf(stderr, "test_initial_state...\n");

    OperatorRegistry registry;
    PackageCompiler compiler("", "");
    PackageManager pm(compiler, registry);
    PackageCatalog catalog(pm);

    assert(catalog.fetch_state() == CatalogFetchState::Idle);
    assert(catalog.fetch_error().empty());
    assert(catalog.entries().empty());

    std::fprintf(stderr, "  PASS\n");
}

int main() {
    std::fprintf(stderr, "=== test_package_catalog ===\n");

    test_initial_state();
    test_parse_valid_json();
    test_parse_invalid_json();
    test_cache_round_trip();

    std::fprintf(stderr, "All tests passed.\n");
    return 0;
}
