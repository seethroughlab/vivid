// ADR-0025 (vst3_host split, PR-C): the per-track PRESET browse/load C API, extracted verbatim from
// vst3_host.cpp. Pure adapter glue over a track's instrument handle (CLAP preset-discovery ext / VST3
// `.vstpreset` + native adapters) into the track's preset_cache — no session/graph engine state. The
// functions are the public session C API (declared in vst3_host.h), so no header changes are needed;
// they just move to their own TU. Behaviour unchanged.
#include "audio/vst3_host_internal.h"   // Session/Track, PresetEntry (via vst3_presets.h), clap_list_presets, vst3_scan_presets, the C API decls

namespace vivid::session {

// --- Preset browse / load for a track's instrument (generic; no per-plugin code). ---
// Scan the instrument's presets into the track cache. CLAP: the plugin's preset-discovery
// factory. VST3: `.vstpreset` files + native-format adapters (Serum/Pigments). Returns the count.
// Each entry carries {name, id, category, tags[], loadable} read by the accessors below.
int session_track_preset_scan(Session* s, int t, const char* filter) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    Track& tr = *s->tracks[t];
    tr.preset_cache.clear();
    if (tr.clap_inst) {
        std::vector<ClapPresetInfo> pl;
        clap_list_presets(tr.clap_inst, pl, filter ? filter : "");
        tr.preset_cache.reserve(pl.size());
        for (auto& p : pl) { PresetEntry e; e.name = std::move(p.name); e.id = std::move(p.id);
                             e.source = "clap"; e.loadable = true; tr.preset_cache.push_back(std::move(e)); }
    } else if (tr.handle) {
        vst3_scan_presets(tr.handle, filter, tr.preset_cache);
    }
    return static_cast<int>(tr.preset_cache.size());
}
static const PresetEntry* preset_at(Session* s, int t, int i) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    const auto& c = s->tracks[t]->preset_cache;
    return (i >= 0 && i < static_cast<int>(c.size())) ? &c[i] : nullptr;
}
int session_track_preset_count(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    return static_cast<int>(s->tracks[t]->preset_cache.size());
}
const char* session_track_preset_name(Session* s, int t, int i)     { const PresetEntry* e = preset_at(s, t, i); return e ? e->name.c_str() : ""; }
const char* session_track_preset_id(Session* s, int t, int i)       { const PresetEntry* e = preset_at(s, t, i); return e ? e->id.c_str() : ""; }
const char* session_track_preset_category(Session* s, int t, int i) { const PresetEntry* e = preset_at(s, t, i); return e ? e->category.c_str() : ""; }
int         session_track_preset_loadable(Session* s, int t, int i) { const PresetEntry* e = preset_at(s, t, i); return (e && e->loadable) ? 1 : 0; }
int         session_track_preset_tag_count(Session* s, int t, int i){ const PresetEntry* e = preset_at(s, t, i); return e ? static_cast<int>(e->tags.size()) : 0; }
const char* session_track_preset_tag(Session* s, int t, int i, int k) {
    const PresetEntry* e = preset_at(s, t, i);
    return (e && k >= 0 && k < static_cast<int>(e->tags.size())) ? e->tags[k].c_str() : "";
}
// Load a preset by its id (from the scan). CLAP: preset-load ext. VST3: `.vstpreset` container or
// an adapter-owned native file -> setState. Returns true on success (browse-only presets => false).
bool session_track_preset_load(Session* s, int t, const char* id) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !id) return false;
    Track& tr = *s->tracks[t];
    if (tr.clap_inst) return clap_load_preset(tr.clap_inst, id);
    if (tr.handle)    return vst3_load_preset(tr.handle, id);
    return false;
}

}  // namespace vivid::session
