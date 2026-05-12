#include "runtime/audio/vst3_scanner.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

static std::string rvst3_expand_home(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = getenv("HOME");
    if (!home) return p;
    return std::string(home) + p.substr(1);
}

static void rvst3_collect_bundles(const std::string& dir,
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
                rvst3_collect_bundles(full, out, depth + 1);
        }
    }
    closedir(d);
}

// Read CFBundleName from Contents/Info.plist without loading the binary.
// Returns empty string if not found.
static std::string rvst3_read_bundle_name(const std::string& bundle) {
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

static std::string rvst3_bundle_stem(const std::string& bundle) {
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

// ---------------------------------------------------------------------------
// Scan state
// ---------------------------------------------------------------------------

static std::vector<RuntimeVst3PluginInfo> g_vst3_plugins;
static std::once_flag                      g_vst3_scan_once;

static void rvst3_do_scan() {
    std::vector<std::string> search_paths = {
        "/Library/Audio/Plug-Ins/VST3",
        rvst3_expand_home("~/Library/Audio/Plug-Ins/VST3"),
    };

    std::vector<std::string> bundles;
    for (const auto& dir : search_paths)
        rvst3_collect_bundles(dir, bundles);

    std::vector<RuntimeVst3PluginInfo> results;
    for (const auto& bundle : bundles) {
        RuntimeVst3PluginInfo pi;
        pi.path = bundle;
        pi.name = rvst3_read_bundle_name(bundle);
        if (pi.name.empty())
            pi.name = rvst3_bundle_stem(bundle);
        pi.vendor = {};
        pi.key    = pi.name;
        results.push_back(std::move(pi));
    }

    std::sort(results.begin(), results.end(),
        [](const RuntimeVst3PluginInfo& a, const RuntimeVst3PluginInfo& b) {
            return a.name < b.name;
        });

    g_vst3_plugins = std::move(results);
}

void runtime_vst3_scan_plugins() {
    std::call_once(g_vst3_scan_once, rvst3_do_scan);
}

const std::vector<RuntimeVst3PluginInfo>& runtime_vst3_get_plugins() {
    return g_vst3_plugins;
}
