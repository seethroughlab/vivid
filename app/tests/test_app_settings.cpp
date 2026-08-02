// Headless test for the app-level settings store (app/src/app/app_settings.*): a missing/invalid file
// yields defaults, and a save→load round-trip preserves the reduce-motion toggle. (UX Ph4 F1)
#include "app/app_settings.h"
#include "test_helpers.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vivid;

int main() {
    const std::string path = (std::filesystem::temp_directory_path() / "vivid_app_settings_test.json").string();
    std::error_code ec; std::filesystem::remove(path, ec);

    // Missing file → defaults (reduce_motion off).
    CHECK(load_app_settings(path).reduce_motion == false);
    // Empty path → defaults, no crash.
    CHECK(load_app_settings("").reduce_motion == false);

    // Round-trip true.
    CHECK(save_app_settings({ /*reduce_motion*/ true }, path));
    CHECK(load_app_settings(path).reduce_motion == true);

    // Round-trip false (overwrites).
    CHECK(save_app_settings({ /*reduce_motion*/ false }, path));
    CHECK(load_app_settings(path).reduce_motion == false);

    // Malformed JSON → defaults, no throw.
    { std::ofstream(path, std::ios::trunc) << "{ not valid json"; }
    CHECK(load_app_settings(path).reduce_motion == false);

    // Wrong-typed value → falls back to the default rather than throwing.
    { std::ofstream(path, std::ios::trunc) << "{ \"reduce_motion\": \"yes\" }"; }
    CHECK(load_app_settings(path).reduce_motion == false);

    std::filesystem::remove(path, ec);
    return vivid::test::summary("test_app_settings");
}
