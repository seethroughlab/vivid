// Tests for the i18n module (src/ui/i18n.h + i18n.cpp).
// Covers load, get, get_plural, fallbacks, error paths.

#include "ui/i18n.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// Derive fixture path from source file location.
static std::string fixture_path() {
    std::filesystem::path src(__FILE__);
    return (src.parent_path() / "fixtures" / "test_locale_en.json").string();
}

int main() {
    std::fprintf(stderr, "\n=== Test: i18n ===\n\n");

    auto& i18n = vivid::ui::I18n::instance();
    std::string path = fixture_path();

    // 1. load valid fixture
    check(i18n.load(path.c_str()), "load valid fixture returns true");

    // 2. get existing key
    check(std::strcmp(i18n.get("save", "fb"), "Save") == 0,
          "get('save') returns 'Save'");

    // 3. get missing key returns fallback
    check(std::strcmp(i18n.get("missing", "fb"), "fb") == 0,
          "get('missing') returns fallback");

    // 4. get_plural n==1 returns _one variant
    check(std::strcmp(i18n.get_plural("item", 1, "1 thing", "N things"), "1 item") == 0,
          "get_plural('item', 1) returns '1 item'");

    // 5. get_plural n!=1 returns _other variant
    check(std::strcmp(i18n.get_plural("item", 5, "1 thing", "N things"), "{n} items") == 0,
          "get_plural('item', 5) returns '{n} items'");

    // 6. get_plural falls back to base key when no _one/_other
    check(std::strcmp(i18n.get_plural("base_only", 1, "s", "p"), "base value") == 0,
          "get_plural('base_only', 1) falls back to base key");

    // 7. get_plural for missing key returns singular fallback
    check(std::strcmp(i18n.get_plural("missing", 1, "s", "p"), "s") == 0,
          "get_plural('missing', 1) returns singular fallback");

    // 8. get_plural for missing key n!=1 returns plural fallback
    check(std::strcmp(i18n.get_plural("missing", 5, "s", "p"), "p") == 0,
          "get_plural('missing', 5) returns plural fallback");

    // 9. locale() returns 'en'
    check(i18n.locale() == "en", "locale() returns 'en'");

    // 10. load invalid path returns false
    check(!i18n.load("/nonexistent/path.json"), "load invalid path returns false");

    // 11. after failed load, get returns fallback (strings cleared)
    check(std::strcmp(i18n.get("save", "fb"), "fb") == 0,
          "after failed load, get returns fallback");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
