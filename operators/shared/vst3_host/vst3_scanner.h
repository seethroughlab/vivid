#pragma once
// vst3_scanner.h — Scan standard VST3 plugin directories via filesystem +
// Info.plist only. No dlopen at scan time — avoids crashing on plugins with
// buggy ObjC +load methods. dlopen happens only in vst3_load_plugin().

#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

namespace {

struct Vst3PluginInfo {
    std::string path;   // full path to .vst3 bundle
    std::string name;   // display name, e.g. "Serum2"
    std::string vendor; // empty — vendor requires dlopen; omitted for safety
    std::string uid_hex; // empty — requires dlopen; vst3_load_plugin uses first class
};

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

static std::string vst3_expand_home(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = getenv("HOME");
    if (!home) return p;
    return std::string(home) + p.substr(1);
}

static void vst3_collect_bundles(const std::string& dir,
                                  std::vector<std::string>& out,
                                  int depth = 0) {
    if (depth > 3) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name = ent->d_name;
        std::string full = dir + "/" + name;
        const std::string ext = ".vst3";
        if (name.size() > ext.size() &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
            out.push_back(full);
        } else {
            struct stat st{};
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                vst3_collect_bundles(full, out, depth + 1);
        }
    }
    closedir(d);
}

static std::string vst3_resolve_binary(const std::string& bundle) {
    std::string b = bundle;
    while (!b.empty() && b.back() == '/') b.pop_back();
    size_t slash = b.rfind('/');
    std::string name = (slash == std::string::npos) ? b : b.substr(slash + 1);
    std::string stem = name;
    const std::string ext = ".vst3";
    if (stem.size() > ext.size() &&
        stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0)
        stem.resize(stem.size() - ext.size());
    return b + "/Contents/MacOS/" + stem;
}

// Read CFBundleName from Contents/Info.plist without loading the binary.
static std::string vst3_read_bundle_name(const std::string& bundle) {
    std::string plist_path = bundle + "/Contents/Info.plist";
    FILE* f = fopen(plist_path.c_str(), "r");
    if (!f) return {};
    std::string content;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f))
        content += buf;
    fclose(f);

    const char* key_tag = "<key>CFBundleName</key>";
    size_t pos = content.find(key_tag);
    if (pos == std::string::npos) return {};
    pos = content.find("<string>", pos + strlen(key_tag));
    if (pos == std::string::npos) return {};
    pos += 8;
    size_t end = content.find("</string>", pos);
    if (end == std::string::npos) return {};
    return content.substr(pos, end - pos);
}

static std::string vst3_bundle_stem(const std::string& bundle) {
    std::string b = bundle;
    while (!b.empty() && b.back() == '/') b.pop_back();
    size_t slash = b.rfind('/');
    std::string name = (slash == std::string::npos) ? b : b.substr(slash + 1);
    const std::string ext = ".vst3";
    if (name.size() > ext.size() &&
        name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
        name.resize(name.size() - ext.size());
    return name;
}

// Returns the display key for a plugin. Key = name (vendor omitted).
static std::string vst3_plugin_key(const std::string& name,
                                    const std::string& /*vendor*/) {
    return name;
}

// ---------------------------------------------------------------------------
// Scan state
// ---------------------------------------------------------------------------

static std::vector<Vst3PluginInfo> kVst3PluginCache;
static std::once_flag              kVst3ScanOnce;

static std::vector<std::string> vst3_search_paths() {
    return {
        "/Library/Audio/Plug-Ins/VST3",
        vst3_expand_home("~/Library/Audio/Plug-Ins/VST3"),
    };
}

static void vst3_scan_impl() {
    std::vector<std::string> bundles;
    for (const auto& dir : vst3_search_paths())
        vst3_collect_bundles(dir, bundles);

    std::vector<Vst3PluginInfo> results;
    for (const auto& bundle : bundles) {
        Vst3PluginInfo pi;
        pi.path    = bundle;
        pi.name    = vst3_read_bundle_name(bundle);
        if (pi.name.empty())
            pi.name = vst3_bundle_stem(bundle);
        pi.vendor  = {};
        pi.uid_hex = {};
        results.push_back(std::move(pi));
    }

    std::sort(results.begin(), results.end(),
        [](const Vst3PluginInfo& a, const Vst3PluginInfo& b) {
            return a.name < b.name;
        });

    kVst3PluginCache = std::move(results);
}

static void vst3_scan_plugins() {
    std::call_once(kVst3ScanOnce, vst3_scan_impl);
}

static const Vst3PluginInfo* vst3_find_by_key(const std::string& key) {
    for (const auto& pi : kVst3PluginCache) {
        if (pi.name == key) return &pi;
    }
    return nullptr;
}

} // namespace
