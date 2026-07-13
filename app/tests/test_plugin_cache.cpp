// The plugin probe's on-disk memory (audio/plugin_cache.h): what re-probes, what doesn't, and
// whether a verdict survives a round-trip. The staleness rule is the load-bearing part — get it
// wrong in one direction and an updated plugin keeps its old class forever; get it wrong in the
// other and a plugin that CRASHES the probe re-crashes the app on every launch.
#include "audio/plugin_cache.h"
#include "audio/plugin_catalog.h"
#include "test_helpers.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace vivid::session;

namespace {

PluginCacheEntry make_entry(const std::string& path, int cls, std::int64_t mtime = 100,
                            std::int64_t size = 2000) {
    PluginCacheEntry e;
    e.path = path; e.cls = cls; e.exe_mtime = mtime; e.exe_size = size;
    e.format = kFmtVST3; e.name = "Thing"; e.vendor = "Acme"; e.uid = "ABCD";
    return e;
}

void test_staleness() {
    PluginCache c;
    c.version = kPluginCacheVersion;
    c.put(make_entry("/p/A.vst3", kClassInstrument, 100, 2000));

    // Unchanged executable -> no re-probe.
    CHECK(!plugin_cache_is_stale(c, "/p/A.vst3", 100, 2000));
    // Never seen -> probe.
    CHECK(plugin_cache_is_stale(c, "/p/NEW.vst3", 1, 1));
    // Reinstalled: mtime or size moved -> re-probe.
    CHECK(plugin_cache_is_stale(c, "/p/A.vst3", 101, 2000));
    CHECK(plugin_cache_is_stale(c, "/p/A.vst3", 100, 2001));

    // A cache written by an older schema is entirely re-probed (the classifier may have been fixed).
    PluginCache old = c;
    old.version = kPluginCacheVersion - 1;
    CHECK(plugin_cache_is_stale(old, "/p/A.vst3", 100, 2000));
}

// The one that protects the app: a plugin that took us down is never probed again on its own.
void test_crashed_is_never_retried() {
    PluginCache c;
    c.version = kPluginCacheVersion;
    c.put(make_entry("/p/BAD.vst3", kClassCrashed, 100, 2000));
    CHECK(!plugin_cache_is_stale(c, "/p/BAD.vst3", 100, 2000));
    // Even a *changed* binary doesn't auto-retry a crasher... except that it does: a reinstall is
    // the user's fix. Assert the intended behavior explicitly so it can't drift silently.
    CHECK(!plugin_cache_is_stale(c, "/p/BAD.vst3", 999, 999));

    // A merely FAILED probe (unreadable bundle) is a normal verdict and does re-check on reinstall.
    c.put(make_entry("/p/UGLY.vst3", kClassFailed, 100, 2000));
    CHECK(!plugin_cache_is_stale(c, "/p/UGLY.vst3", 100, 2000));
    CHECK(plugin_cache_is_stale(c, "/p/UGLY.vst3", 101, 2000));
}

void test_put_replaces_by_path() {
    PluginCache c;
    c.put(make_entry("/p/A.vst3", kClassUnknown));
    c.put(make_entry("/p/A.vst3", kClassEffect));
    CHECK(c.entries.size() == 1);
    CHECK(c.find("/p/A.vst3") && c.find("/p/A.vst3")->cls == kClassEffect);
    CHECK(c.find("/p/missing.vst3") == nullptr);
}

void test_round_trip() {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "vivid_plugin_cache_test.json";
    fs::remove(p);

    PluginCache c;
    c.version = kPluginCacheVersion;
    c.put(make_entry("/p/A.vst3", kClassInstrument, 42, 4242));
    c.put(make_entry("/p/B.clap", kClassEffect, 7, 77));
    CHECK(save_plugin_cache(p.string(), c));

    const PluginCache r = load_plugin_cache(p.string());
    CHECK(r.version == kPluginCacheVersion);
    CHECK(r.entries.size() == 2);
    const PluginCacheEntry* a = r.find("/p/A.vst3");
    CHECK(a != nullptr);
    CHECK(a && a->cls == kClassInstrument);
    CHECK(a && a->exe_mtime == 42 && a->exe_size == 4242);
    CHECK(a && a->vendor == "Acme" && a->uid == "ABCD");
    // A saved verdict must actually suppress a re-probe (the whole point of the file).
    CHECK(!plugin_cache_is_stale(r, "/p/A.vst3", 42, 4242));

    fs::remove(p);
}

void test_corrupt_cache_is_empty_not_fatal() {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "vivid_plugin_cache_corrupt.json";
    { std::FILE* f = std::fopen(p.c_str(), "w"); CHECK(f != nullptr); if (f) { std::fputs("{not json", f); std::fclose(f); } }
    const PluginCache r = load_plugin_cache(p.string());
    CHECK(r.entries.empty());
    CHECK(plugin_cache_is_stale(r, "/p/A.vst3", 1, 1));   // -> everything re-probes, nothing crashes
    fs::remove(p);
    // A missing file behaves the same.
    const PluginCache m = load_plugin_cache((fs::temp_directory_path() / "vivid_nope.json").string());
    CHECK(m.entries.empty());
}

}  // namespace

int main() {
    test_staleness();
    test_crashed_is_never_retried();
    test_put_replaces_by_path();
    test_round_trip();
    test_corrupt_cache_is_empty_not_fatal();
    return vivid::test::summary("test_plugin_cache");
}
