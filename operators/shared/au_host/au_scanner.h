#pragma once
// au_scanner.h — Enumerate installed AU instruments via Core Audio's component registry.
// Header-only, anonymous namespace. macOS only.

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

namespace {

struct AUPluginInfo {
    std::string               name;  // from AudioComponentCopyName, e.g. "Toontrack: EZdrummer 3"
    AudioComponentDescription desc;  // component description for matching
    AudioComponent            comp;  // stable registry handle
};

static std::vector<AUPluginInfo> kAUPluginCache;
static std::once_flag            kAUScanOnce;

static std::string au_cfstring_to_std(CFStringRef s) {
    if (!s) return {};
    char buf[512] = {};
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

static void au_scan_impl() {
    std::vector<AUPluginInfo> results;

    AudioComponentDescription search = {};
    search.componentType         = kAudioUnitType_MusicDevice;
    search.componentSubType      = 0;
    search.componentManufacturer = 0;
    search.componentFlags        = 0;
    search.componentFlagsMask    = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &search);
    while (comp) {
        CFStringRef cfname = nullptr;
        if (AudioComponentCopyName(comp, &cfname) == noErr && cfname) {
            AUPluginInfo info;
            info.name = au_cfstring_to_std(cfname);
            CFRelease(cfname);
            AudioComponentGetDescription(comp, &info.desc);
            info.comp = comp;
            results.push_back(std::move(info));
        }
        comp = AudioComponentFindNext(comp, &search);
    }

    std::sort(results.begin(), results.end(),
        [](const AUPluginInfo& a, const AUPluginInfo& b) { return a.name < b.name; });

    kAUPluginCache = std::move(results);
}

static void au_scan_plugins() {
    std::call_once(kAUScanOnce, au_scan_impl);
}

static AudioComponent au_find_by_name(const std::string& name) {
    for (const auto& info : kAUPluginCache) {
        if (info.name == name) return info.comp;
    }
    return nullptr;
}

static const std::vector<AUPluginInfo>& au_get_plugins() {
    return kAUPluginCache;
}

} // namespace
#endif // __APPLE__
