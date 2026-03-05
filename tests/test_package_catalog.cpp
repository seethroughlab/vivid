// test_package_catalog.cpp — PackageCatalog unit tests
// Tests JSON parsing helpers. Does NOT test network fetch.

#include "runtime/package_catalog.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

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
                "name": "codex-test-drums",
                "description": "808-style drum synthesis operators",
                "version": "1.0.0",
                "vivid_core": ">=0.1.0 <2.0.0",
                "author": "Jeff",
                "url": "https://github.com/vivid-project/vivid-drums",
                "category": "audio",
                "tags": ["drums", "synthesis"]
            },
            {
                "name": "codex-test-glitch",
                "description": "Glitch effect operators",
                "version": "0.2.0",
                "vivid_core": ">=0.1.0 <2.0.0",
                "author": "Alice",
                "url": "https://github.com/vivid-project/vivid-glitch",
                "category": "gpu",
                "tags": ["glitch", "visual"]
            }
        ]
    })";

    std::vector<CatalogEntry> entries;
    assert(PackageCatalog::parse_index_json(json, entries));
    assert(entries.size() == 2);

    bool found_drums = false, found_glitch = false;
    for (const auto& e : entries) {
        if (e.name == "codex-test-drums") {
            assert(e.description == "808-style drum synthesis operators");
            assert(e.version == "1.0.0");
            assert(e.vivid_core == ">=0.1.0 <2.0.0");
            assert(e.author == "Jeff");
            assert(e.category == "audio");
            assert(e.tags.size() == 2);
            assert(e.tags[0] == "drums");
            assert(e.tags[1] == "synthesis");
            assert(!e.installed);
            found_drums = true;
        }
        if (e.name == "codex-test-glitch") {
            assert(e.description == "Glitch effect operators");
            assert(e.version == "0.2.0");
            assert(e.vivid_core == ">=0.1.0 <2.0.0");
            assert(e.author == "Alice");
            assert(e.category == "gpu");
            found_glitch = true;
        }
    }
    assert(found_drums && found_glitch);

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 2: empty and malformed JSON
// ---------------------------------------------------------------------------
static void test_parse_invalid_json() {
    std::fprintf(stderr, "test_parse_invalid_json...\n");

    std::vector<CatalogEntry> entries;
    assert(!PackageCatalog::parse_index_json("not valid json at all", entries));

    entries.clear();
    assert(PackageCatalog::parse_index_json(R"({"schema_version": 1, "packages": []})", entries));
    assert(entries.empty());

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 3: parse another valid payload (cache-like payload)
// ---------------------------------------------------------------------------
static void test_parse_cache_like_json() {
    std::fprintf(stderr, "test_parse_cache_like_json...\n");

    const char* json = R"({
        "schema_version": 1,
        "packages": [
            {
                "name": "test-pkg",
                "description": "A test package",
                "version": "2.0.0",
                "vivid_core": ">=0.1.0 <2.0.0",
                "author": "Tester",
                "url": "https://example.com/test-pkg",
                "category": "control",
                "tags": ["test"]
            }
        ]
    })";

    std::vector<CatalogEntry> entries;
    assert(PackageCatalog::parse_index_json(json, entries));
    assert(entries.size() == 1);

    const auto& e = entries[0];
    assert(e.name == "test-pkg");
    assert(e.description == "A test package");
    assert(e.version == "2.0.0");
    assert(e.vivid_core == ">=0.1.0 <2.0.0");
    assert(e.author == "Tester");
    assert(e.category == "control");
    assert(e.tags.size() == 1);
    assert(e.tags[0] == "test");

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 4: parse new catalog schema fields used by vivid/catalog/packages.json
// ---------------------------------------------------------------------------
static void test_parse_vivid_catalog_schema() {
    std::fprintf(stderr, "test_parse_vivid_catalog_schema...\n");

    const char* json = R"({
        "schema_version": 1,
        "packages": [
            {
                "name": "test-schema-pkg",
                "description_short": "Short description only",
                "version": "0.3.1",
                "vivid_core": ">=0.1.0 <2.0.0",
                "author": "SchemaTester",
                "install_url": "https://example.com/test-schema-pkg.git",
                "repo_url": "https://example.com/test-schema-pkg",
                "category": "control",
                "tags": ["schema", "compat"]
            }
        ]
    })";

    std::vector<CatalogEntry> entries;
    assert(PackageCatalog::parse_index_json(json, entries));
    assert(entries.size() == 1);

    const auto& e = entries[0];
    assert(e.name == "test-schema-pkg");
    // description_short should backfill description when description is absent.
    assert(e.description == "Short description only");
    // install_url should be preferred/fallback-compatible for install source.
    assert(e.url == "https://example.com/test-schema-pkg.git");
    assert(e.version == "0.3.1");
    assert(e.vivid_core == ">=0.1.0 <2.0.0");
    assert(e.author == "SchemaTester");
    assert(e.category == "control");
    assert(e.tags.size() == 2);
    assert(e.tags[0] == "schema");
    assert(e.tags[1] == "compat");

    std::fprintf(stderr, "  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 5: initial state
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

// ---------------------------------------------------------------------------
// Test 6: refresh failure path when network fetch is disabled and no cache exists
// ---------------------------------------------------------------------------
static void test_refresh_without_cache_and_network() {
    std::fprintf(stderr, "test_refresh_without_cache_and_network...\n");

    namespace fs = std::filesystem;
    const fs::path sandbox = fs::temp_directory_path() / "vivid_test_pkg_catalog_no_cache";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);

    setenv("HOME", sandbox.string().c_str(), 1);
    setenv("VIVID_SKIP_PACKAGE_CATALOG_NETWORK", "1", 1);

    OperatorRegistry registry;
    PackageCompiler compiler("", "");
    PackageManager pm(compiler, registry);
    PackageCatalog catalog(pm);

    catalog.refresh();
    for (int i = 0; i < 200; ++i) {
        auto st = catalog.fetch_state();
        if (st == CatalogFetchState::Error || st == CatalogFetchState::Ready)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    assert(catalog.fetch_state() == CatalogFetchState::Error);
    const std::string err = catalog.fetch_error();
    assert(err.find("disabled") != std::string::npos);

    fs::remove_all(sandbox);
    std::fprintf(stderr, "  PASS\n");
}

int main() {
    std::fprintf(stderr, "=== test_package_catalog ===\n");

    test_initial_state();
    test_parse_valid_json();
    test_parse_invalid_json();
    test_parse_cache_like_json();
    test_parse_vivid_catalog_schema();
    test_refresh_without_cache_and_network();

    std::fprintf(stderr, "All tests passed.\n");
    return 0;
}
