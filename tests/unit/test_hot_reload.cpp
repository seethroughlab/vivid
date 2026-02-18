/**
 * @file test_hot_reload.cpp
 * @brief Unit tests for the HotReload class
 *
 * Tests file watching, compilation, loading, error handling, and the
 * update cycle. Compilation tests shell out to clang++ so they take
 * ~2-4s each. No GPU required.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vivid/hot_reload.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>

using namespace vivid;
namespace fs = std::filesystem;

// Fixture paths (resolved via VIVID_SOURCE_DIR compile definition)
static const fs::path kValidChain     = VIVID_SOURCE_DIR "/tests/fixtures/hot-reload/valid/chain.cpp";
static const fs::path kConfigChain    = VIVID_SOURCE_DIR "/tests/fixtures/hot-reload/valid-config/chain.cpp";
static const fs::path kMissingMacro   = VIVID_SOURCE_DIR "/tests/fixtures/hot-reload/missing-macro/chain.cpp";
static const fs::path kBrokenChain    = VIVID_SOURCE_DIR "/tests/fixtures/broken-chain/chain.cpp";
static const fs::path kRootDir        = VIVID_SOURCE_DIR;

// Helper: create a HotReload with root dir pre-configured
static std::unique_ptr<HotReload> makeHR() {
    auto hr = std::make_unique<HotReload>();
    hr->setRootDir(kRootDir);
    return hr;
}

// =============================================================================
// Initial State
// =============================================================================

TEST_CASE("HotReload initial state", "[unit][hot-reload]") {
    HotReload hr;

    SECTION("default constructed state") {
        CHECK_FALSE(hr.isLoaded());
        CHECK(hr.getSetupFn() == nullptr);
        CHECK(hr.getUpdateFn() == nullptr);
        CHECK(hr.getConfigFn() == nullptr);
        CHECK_FALSE(hr.hasError());
        CHECK_FALSE(hr.hasConfig());
    }

    SECTION("getConfig returns defaults when no config function") {
        auto cfg = hr.getConfig();
        CHECK(cfg.windowWidth == 1280);
        CHECK(cfg.windowHeight == 720);
    }
}

// =============================================================================
// File Watching
// =============================================================================

TEST_CASE("HotReload file watching", "[unit][hot-reload]") {
    auto hr = makeHR();

    SECTION("checkNeedsReload false with no source set") {
        CHECK_FALSE(hr->checkNeedsReload());
    }

    SECTION("setSourceFile triggers needs-reload") {
        hr->setSourceFile(kValidChain);
        CHECK(hr->checkNeedsReload());
    }

    SECTION("setSourcePath does NOT trigger needs-reload") {
        hr->setSourcePath(kValidChain);
        CHECK_FALSE(hr->checkNeedsReload());
    }

    SECTION("forceReload makes checkNeedsReload return true") {
        hr->setSourcePath(kValidChain);
        CHECK_FALSE(hr->checkNeedsReload());
        hr->forceReload();
        CHECK(hr->checkNeedsReload());
    }

    SECTION("file modification detected by checkNeedsReload") {
        // Copy fixture to a temp file so we can safely modify it
        auto tmpDir = fs::temp_directory_path() / "vivid_test_watch";
        fs::create_directories(tmpDir);
        auto tmpChain = tmpDir / "chain.cpp";
        fs::copy_file(kValidChain, tmpChain, fs::copy_options::overwrite_existing);

        hr->setSourcePath(tmpChain);
        CHECK_FALSE(hr->checkNeedsReload());

        // Touch the file with a future timestamp
        auto newTime = fs::last_write_time(tmpChain) + std::chrono::seconds(2);
        fs::last_write_time(tmpChain, newTime);

        CHECK(hr->checkNeedsReload());

        // Cleanup
        fs::remove_all(tmpDir);
    }
}

// =============================================================================
// Compilation
// =============================================================================

TEST_CASE("HotReload compilation", "[unit][hot-reload]") {
    auto hr = makeHR();

    SECTION("tryCompile succeeds with valid chain") {
        hr->setSourcePath(kValidChain);
        CHECK(hr->tryCompile());
        CHECK_FALSE(hr->hasError());
    }

    SECTION("tryCompile fails with broken chain and populates errors") {
        hr->setSourcePath(kBrokenChain);
        CHECK_FALSE(hr->tryCompile());
        CHECK(hr->hasError());
        CHECK_FALSE(hr->getCompileErrors().empty());

        // Verify at least one error has the expected fields
        auto& err = hr->getCompileErrors().front();
        CHECK_FALSE(err.file.empty());
        CHECK(err.line > 0);
        CHECK(err.severity == "error");
        CHECK_FALSE(err.message.empty());
    }

    SECTION("tryCompile alone does NOT set isLoaded") {
        hr->setSourcePath(kValidChain);
        hr->tryCompile();
        CHECK_FALSE(hr->isLoaded());
    }

    SECTION("successful compile clears previous errors") {
        hr->setSourcePath(kBrokenChain);
        hr->tryCompile();
        CHECK_FALSE(hr->getCompileErrors().empty());

        hr->setSourcePath(kValidChain);
        hr->tryCompile();
        CHECK(hr->getCompileErrors().empty());
    }
}

// =============================================================================
// Loading
// =============================================================================

TEST_CASE("HotReload loading", "[unit][hot-reload]") {
    auto hr = makeHR();

    SECTION("reload succeeds with valid chain") {
        hr->setSourceFile(kValidChain);
        CHECK(hr->reload());
        CHECK(hr->isLoaded());
        CHECK(hr->getSetupFn() != nullptr);
        CHECK(hr->getUpdateFn() != nullptr);
    }

    SECTION("reload fails on broken chain") {
        hr->setSourceFile(kBrokenChain);
        CHECK_FALSE(hr->reload());
        CHECK_FALSE(hr->isLoaded());
        CHECK(hr->hasError());
    }

    SECTION("tryCompile + loadCompiled two-step succeeds") {
        hr->setSourcePath(kValidChain);
        REQUIRE(hr->tryCompile());
        CHECK(hr->loadCompiled());
        CHECK(hr->isLoaded());
    }

    SECTION("missing VIVID_CHAIN macro: compiles but loadCompiled fails") {
        hr->setSourcePath(kMissingMacro);
        REQUIRE(hr->tryCompile());
        CHECK_FALSE(hr->loadCompiled());
        CHECK_FALSE(hr->isLoaded());
        CHECK_THAT(hr->getError(), Catch::Matchers::ContainsSubstring("entry point"));
    }
}

// =============================================================================
// Config Function
// =============================================================================

TEST_CASE("HotReload config function", "[unit][hot-reload]") {
    SECTION("VIVID_CHAIN_CONFIG provides custom config") {
        auto hr = makeHR();
        hr->setSourceFile(kConfigChain);
        REQUIRE(hr->reload());
        CHECK(hr->hasConfig());
        CHECK(hr->getConfigFn() != nullptr);
        auto cfg = hr->getConfig();
        CHECK(cfg.windowWidth == 1920);
        CHECK(cfg.windowHeight == 1080);
    }

    SECTION("basic VIVID_CHAIN has no config") {
        auto hr = makeHR();
        hr->setSourceFile(kValidChain);
        REQUIRE(hr->reload());
        CHECK_FALSE(hr->hasConfig());
        auto cfg = hr->getConfig();
        CHECK(cfg.windowWidth == 1280);
        CHECK(cfg.windowHeight == 720);
    }
}

// =============================================================================
// Failed Compile Preserves Previous Load
// =============================================================================

TEST_CASE("HotReload failed compile preserves previous load", "[unit][hot-reload]") {
    auto hr = makeHR();

    hr->setSourceFile(kValidChain);
    REQUIRE(hr->reload());
    CHECK(hr->isLoaded());

    // Attempt to compile a broken chain (don't call loadCompiled)
    hr->setSourcePath(kBrokenChain);
    CHECK_FALSE(hr->tryCompile());

    // Old library should still be loaded
    CHECK(hr->isLoaded());
}

// =============================================================================
// Error JSON
// =============================================================================

TEST_CASE("HotReload error JSON", "[unit][hot-reload]") {
    auto hr = makeHR();

    SECTION("no errors produces empty array") {
        CHECK(hr->getErrorsJson() == "{\"errors\":[]}");
    }

    SECTION("failed compile produces valid JSON with expected keys") {
        hr->setSourcePath(kBrokenChain);
        hr->tryCompile();

        auto json = hr->getErrorsJson();
        // Verify it starts and ends correctly
        CHECK_THAT(json, Catch::Matchers::StartsWith("{\"errors\":[{"));
        CHECK_THAT(json, Catch::Matchers::EndsWith("]}"));

        // Verify expected keys are present
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"file\":"));
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"line\":"));
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"column\":"));
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"severity\":"));
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"message\":"));
    }

    SECTION("CompileError::toJson escapes quotes and backslashes") {
        CompileError err;
        err.file = "test.cpp";
        err.line = 1;
        err.column = 1;
        err.severity = "error";
        err.message = R"(use of "deleted" function\n)";

        auto json = err.toJson();
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring(R"(use of \"deleted\" function\\n)"));
    }
}

// =============================================================================
// Update Cycle
// =============================================================================

TEST_CASE("HotReload update cycle", "[unit][hot-reload]") {
    auto hr = makeHR();

    SECTION("update returns false when no source set") {
        CHECK_FALSE(hr->update());
    }

    SECTION("update returns true and loads after setSourceFile") {
        hr->setSourceFile(kValidChain);
        CHECK(hr->update());
        CHECK(hr->isLoaded());
    }

    SECTION("update returns false when no change since last reload") {
        hr->setSourceFile(kValidChain);
        REQUIRE(hr->update());
        CHECK_FALSE(hr->update());
    }
}
