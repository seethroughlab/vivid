// Plugin discovery: enumerate installed plugin bundles (VST3 today) without loading
// any dylib. Category (instrument vs effect) is intentionally NOT guessed here —
// VST3 moduleinfo.json is present on a minority of plugins and non-strict (comments +
// trailing commas), so it's unreliable. Instead the loader decides at add time
// (session_add_instrument_track validates a MIDI-in bus; otherwise it's an effect).
#include "audio/plugin_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
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

void scan_dir(const std::string& dir, const char* ext, int format, std::vector<PluginInfo>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    const std::size_t elen = std::string(ext).size();
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string n = e->d_name;
        if (n.size() > elen && n.compare(n.size() - elen, elen, ext) == 0)
            out.push_back({ dir + "/" + n, bundle_display_name(n, ext), format });
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

}  // namespace

void rescan_plugins() {
    std::vector<PluginInfo> found;
    scan_dir("/Library/Audio/Plug-Ins/VST3", ".vst3", kFmtVST3, found);
    scan_dir("/Library/Audio/Plug-Ins/CLAP", ".clap", kFmtCLAP, found);
    if (const char* home = std::getenv("HOME")) {
        const std::string h = home;
        scan_dir(h + "/Library/Audio/Plug-Ins/VST3", ".vst3", kFmtVST3, found);
        scan_dir(h + "/Library/Audio/Plug-Ins/CLAP", ".clap", kFmtCLAP, found);
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

}  // namespace vivid::session
