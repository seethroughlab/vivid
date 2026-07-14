// Plugin discovery: enumerate installed plugin bundles (VST3 + CLAP) without loading any dylib.
// The scan is deliberately dumb and fast — a plugin's CLASS (instrument vs effect) comes from the
// background probe (audio/plugin_probe.h), the only thing allowed to open a plugin.
//
// (The old comment here said the category was "intentionally not guessed" and left to the loader at
// add time. That's why nothing could group or filter the catalog. The probe reads the plugin's
// factory descriptor — no instantiation — which is both cheap and authoritative.)
#include "audio/plugin_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

namespace vivid::session {
namespace {

std::vector<PluginInfo> g_plugins;
bool g_scanned = false;

std::string bundle_display_name(const std::string& file, const char* ext) {
    const std::size_t elen = std::string(ext).size();
    std::string b = file;
    if (b.size() > elen && b.compare(b.size() - elen, elen, ext) == 0) b.resize(b.size() - elen);
    return b;
}

bool is_dir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Enumerate `dir` for `ext` bundles. Vendors may nest one level down —
// /Library/Audio/Plug-Ins/VST3/<Vendor>/Thing.vst3 is a legal layout (Newfangled Audio ships that
// way, which is why `Generate` used to be invisible) — so a plain directory is descended into.
// `depth` bounds that: we never walk into a plugin bundle's own guts.
void scan_dir(const std::string& dir, const char* ext, int format, int depth,
              std::vector<PluginInfo>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    const std::size_t elen = std::string(ext).size();
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string n = e->d_name;
        const std::string full = dir + "/" + n;
        if (n.size() > elen && n.compare(n.size() - elen, elen, ext) == 0) {
            PluginInfo pi;
            pi.path = full;
            pi.name = bundle_display_name(n, ext);
            pi.format = format;
            out.push_back(std::move(pi));
        } else if (depth > 0 && is_dir(full)) {
            scan_dir(full, ext, format, depth - 1, out);   // a vendor folder
        }
    }
    closedir(d);
}

bool less_ci(const PluginInfo& a, const PluginInfo& b) {
    const std::string& x = a.name; const std::string& y = b.name;
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) {
        const int cx = std::tolower(static_cast<unsigned char>(x[i]));
        const int cy = std::tolower(static_cast<unsigned char>(y[i]));
        if (cx != cy) return cx < cy;
    }
    return x.size() < y.size();
}

void ensure_scanned() { if (!g_scanned) rescan_plugins(); }

constexpr int kVendorDepth = 1;   // descend one level: <root>/<Vendor>/Thing.vst3

}  // namespace

const char* plugin_class_name(int cls) {
    switch (cls) {
        case kClassInstrument: return "instrument";
        case kClassEffect:     return "effect";
        case kClassNoteEffect: return "note-effect";
        case kClassFailed:     return "failed";
        case kClassCrashed:    return "crashed";
        default:               return "unknown";
    }
}

const char* plugin_format_name(int fmt) {
    switch (fmt) {
        case kFmtCLAP: return "CLAP";
        case kFmtVST3: return "VST3";
        default:       return "?";
    }
}

void rescan_plugins() {
    std::vector<PluginInfo> found;
    scan_dir("/Library/Audio/Plug-Ins/VST3", ".vst3", kFmtVST3, kVendorDepth, found);
    scan_dir("/Library/Audio/Plug-Ins/CLAP", ".clap", kFmtCLAP, kVendorDepth, found);
    if (const char* home = std::getenv("HOME")) {
        const std::string h = home;
        scan_dir(h + "/Library/Audio/Plug-Ins/VST3", ".vst3", kFmtVST3, kVendorDepth, found);
        scan_dir(h + "/Library/Audio/Plug-Ins/CLAP", ".clap", kFmtCLAP, kVendorDepth, found);
    }
    std::sort(found.begin(), found.end(), less_ci);
    g_plugins.swap(found);
    g_scanned = true;
}

int plugin_count() { ensure_scanned(); return static_cast<int>(g_plugins.size()); }

const PluginInfo& plugin_at(int i) {
    ensure_scanned();
    static const PluginInfo empty{};
    return (i >= 0 && i < static_cast<int>(g_plugins.size())) ? g_plugins[i] : empty;
}

bool plugin_set_probe_result(const std::string& path, const std::string& name,
                             const std::string& vendor, const std::string& uid, int cls) {
    for (PluginInfo& p : g_plugins) {
        if (p.path != path) continue;
        // NOTE: `name` (the plugin's own) is deliberately NOT written over the bundle stem. The
        // list is sorted by name and the UI addresses rows by index, so renaming a row as its
        // probe lands would re-sort the list under the user's cursor mid-scroll/mid-drag.
        (void)name;
        p.vendor = vendor;
        p.uid    = uid;
        p.cls    = cls;
        p.probed = true;
        return true;
    }
    return false;
}

}  // namespace vivid::session
