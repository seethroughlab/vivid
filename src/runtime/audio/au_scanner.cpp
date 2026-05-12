#ifdef __APPLE__
#include "runtime/audio/au_scanner.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <mutex>

static std::vector<RuntimeAUPluginInfo> g_au_plugins;
static std::once_flag                   g_au_scan_once;

static std::string cfstring_to_std(CFStringRef s) {
    if (!s) return {};
    char buf[512] = {};
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

static void do_scan() {
    std::vector<RuntimeAUPluginInfo> results;

    AudioComponentDescription search = {};
    search.componentType = kAudioUnitType_MusicDevice;

    AudioComponent comp = AudioComponentFindNext(nullptr, &search);
    while (comp) {
        CFStringRef cfname = nullptr;
        if (AudioComponentCopyName(comp, &cfname) == noErr && cfname) {
            RuntimeAUPluginInfo info;
            info.name = cfstring_to_std(cfname);
            CFRelease(cfname);
            if (!info.name.empty()) results.push_back(std::move(info));
        }
        comp = AudioComponentFindNext(comp, &search);
    }

    std::sort(results.begin(), results.end(),
        [](const RuntimeAUPluginInfo& a, const RuntimeAUPluginInfo& b) {
            return a.name < b.name;
        });

    g_au_plugins = std::move(results);
}

void runtime_au_scan_plugins() {
    std::call_once(g_au_scan_once, do_scan);
}

const std::vector<RuntimeAUPluginInfo>& runtime_au_get_plugins() {
    return g_au_plugins;
}

#endif // __APPLE__
