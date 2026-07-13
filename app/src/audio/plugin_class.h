#pragma once
#include "audio/plugin_catalog.h"   // PluginClass

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

// What a plugin IS, decided from metadata the plugin's factory hands out — NOT by instantiating it.
//
// Pure string logic, deliberately split out of the probe so it can be unit-tested headlessly with no
// VST3/CLAP SDK, no dylib, and no plugins installed. This is the decision that lets the catalog
// group instruments vs effects; getting it backwards silently routes a synth into an FX slot.
namespace vivid::session {

// CLAP: the descriptor's NULL-terminated `features` array (CLAP_PLUGIN_FEATURE_*). A plugin may
// list several ("instrument", "synthesizer", "stereo"), so we look for the class ones by name.
// Instrument wins over audio-effect if a plugin somehow claims both (a synth with an FX mode is
// still something you add as a source).
inline int class_from_clap_features(const char* const* features) {
    if (!features) return kClassUnknown;
    bool inst = false, fx = false, note_fx = false;
    for (const char* const* f = features; *f; ++f) {
        const std::string_view s{*f};
        if (s == "instrument")   inst = true;
        else if (s == "audio-effect") fx = true;
        else if (s == "note-effect" || s == "note-detector") note_fx = true;
    }
    if (inst)    return kClassInstrument;
    if (fx)      return kClassEffect;
    if (note_fx) return kClassNoteEffect;
    return kClassUnknown;
}

// VST3: the class's `subCategories`, a '|'-separated list — "Instrument|Synth", "Fx|Delay",
// "Fx|Instrument" (an FX-capable instrument). Matching is per-token (a substring test would call
// "Fx|Instrumental" an instrument), and case-insensitive because the string comes from third-party
// plugins and the SDK's own constants are only a convention.
inline bool vst3_subcategory_has(std::string_view cats, std::string_view wanted) {
    const auto lower_eq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
            const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
            if (ca != cb) return false;
        }
        return true;
    };
    std::size_t start = 0;
    while (start <= cats.size()) {
        std::size_t end = cats.find('|', start);
        if (end == std::string_view::npos) end = cats.size();
        if (lower_eq(cats.substr(start, end - start), wanted)) return true;
        if (end == cats.size()) break;
        start = end + 1;
    }
    return false;
}

inline int class_from_vst3_subcategories(const char* subcategories) {
    if (!subcategories || !*subcategories) return kClassUnknown;
    const std::string_view cats{subcategories};
    if (vst3_subcategory_has(cats, "Instrument")) return kClassInstrument;   // beats Fx if both
    if (vst3_subcategory_has(cats, "Fx"))        return kClassEffect;
    return kClassUnknown;
}

}  // namespace vivid::session
