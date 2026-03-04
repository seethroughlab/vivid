#include "runtime/editor_detect.h"
#include <cstdio>
#include <unordered_set>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    std::fprintf(stderr, "--- test_editor_detect ---\n");

    auto editors = vivid::detect_editors();

    // 1. Returns at least 2 entries (System Default + Custom Command)
    check(editors.size() >= 2, "at least 2 entries returned");

    // 2. First entry is System Default with empty app_id
    check(!editors.empty() && editors.front().name == "System Default",
          "first entry name is 'System Default'");
    check(!editors.empty() && editors.front().app_id == "",
          "first entry app_id is empty");

    // 3. Last entry is Custom Command...
    check(!editors.empty() && editors.back().name == "Custom Command...",
          "last entry name is 'Custom Command...'");

    // 4. All entries have non-empty name fields
    bool all_named = true;
    for (auto& e : editors) {
        if (e.name.empty()) { all_named = false; break; }
    }
    check(all_named, "all entries have non-empty name fields");

    // 5. No duplicate names
    std::unordered_set<std::string> seen;
    bool no_dups = true;
    for (auto& e : editors) {
        if (!seen.insert(e.name).second) { no_dups = false; break; }
    }
    check(no_dups, "no duplicate names in list");

    // 6. Runs without crashing regardless of installed apps (already reached here)
    check(true, "detect_editors() runs without crashing on CI");

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
